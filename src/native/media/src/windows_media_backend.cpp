#include "radio76/windows_media_backend.h"

#include "radio76/stream_catalog.h"
#include "radio76/stream_player.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>

#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace radio76 {
namespace {

using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

constexpr std::int64_t kTicksPerMillisecond = 10000;

std::string ToUtf8(const winrt::hstring& text) {
    return winrt::to_string(text);
}

bool IsSpotifyApplication(const std::string& applicationId) {
    std::string lower;
    lower.reserve(applicationId.size());
    for (const char character : applicationId) {
        lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lower.find("spotify") != std::string::npos;
}

bool IsSpotifyProcessRunning() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry) != FALSE) {
        do {
            if (_wcsicmp(entry.szExeFile, L"Spotify.exe") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry) != FALSE);
    }
    CloseHandle(snapshot);
    return found;
}

PlaybackState ToPlaybackState(GlobalSystemMediaTransportControlsSessionPlaybackStatus status) {
    switch (status) {
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
        return PlaybackState::Closed;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened:
        return PlaybackState::Opened;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
        return PlaybackState::Changing;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
        return PlaybackState::Playing;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
        return PlaybackState::Paused;
    case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
        return PlaybackState::Stopped;
    }
    return PlaybackState::Unavailable;
}

bool StartsWith(
    const std::vector<std::uint8_t>& bytes,
    std::initializer_list<std::uint8_t> magic) {
    if (bytes.size() < magic.size()) {
        return false;
    }
    return std::equal(magic.begin(), magic.end(), bytes.begin());
}

void RemoveArtworkFiles(const std::filesystem::path& basePath) {
    for (const wchar_t* extension : {L".png", L".jpg", L".pixels"}) {
        std::filesystem::path path = basePath;
        path += extension;
        DeleteFileW(path.c_str());
    }
}

bool DecodeArtworkPixels(
    const std::vector<std::uint8_t>& bytes,
    std::uint32_t& width,
    std::uint32_t& height,
    std::vector<std::uint8_t>& pixels) {
    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(&factory)))) {
        return false;
    }
    ComPtr<IWICStream> stream;
    if (FAILED(factory->CreateStream(&stream)) ||
        FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(bytes.data()),
            static_cast<DWORD>(bytes.size())))) {
        return false;
    }
    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromStream(
            stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder))) {
        return false;
    }
    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame))) {
        return false;
    }
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(
            frame.Get(),
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom))) {
        return false;
    }
    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    if (FAILED(converter->GetSize(&sourceWidth, &sourceHeight)) ||
        sourceWidth == 0 || sourceHeight == 0) {
        return false;
    }
    const UINT longest = sourceWidth > sourceHeight ? sourceWidth : sourceHeight;
    const UINT targetWidth = longest > 96
        ? static_cast<UINT>(static_cast<std::uint64_t>(sourceWidth) * 96 / longest)
        : sourceWidth;
    const UINT targetHeight = longest > 96
        ? static_cast<UINT>(static_cast<std::uint64_t>(sourceHeight) * 96 / longest)
        : sourceHeight;
    ComPtr<IWICBitmapSource> source = converter;
    ComPtr<IWICBitmapScaler> scaler;
    if (targetWidth != sourceWidth || targetHeight != sourceHeight) {
        if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
            FAILED(scaler->Initialize(
                converter.Get(), targetWidth, targetHeight,
                WICBitmapInterpolationModeFant))) {
            return false;
        }
        source = scaler;
    }
    const UINT stride = targetWidth * 4;
    std::vector<std::uint8_t> bgra(static_cast<std::size_t>(stride) * targetHeight);
    if (FAILED(source->CopyPixels(
            nullptr, stride, static_cast<UINT>(bgra.size()), bgra.data()))) {
        return false;
    }
    pixels.resize(bgra.size());
    for (std::size_t index = 0; index < bgra.size(); index += 4) {
        pixels[index] = 0xFF;
        pixels[index + 1] = bgra[index + 2];
        pixels[index + 2] = bgra[index + 1];
        pixels[index + 3] = bgra[index];
    }
    width = targetWidth;
    height = targetHeight;
    return true;
}

std::string Base64Encode(const std::vector<std::uint8_t>& bytes) {
    static constexpr char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((bytes.size() + 2) / 3) * 4);
    for (std::size_t index = 0; index < bytes.size(); index += 3) {
        const std::uint32_t first = bytes[index];
        const std::uint32_t second = index + 1 < bytes.size() ? bytes[index + 1] : 0;
        const std::uint32_t third = index + 2 < bytes.size() ? bytes[index + 2] : 0;
        const std::uint32_t value = (first << 16) | (second << 8) | third;
        output.push_back(kAlphabet[(value >> 18) & 0x3F]);
        output.push_back(kAlphabet[(value >> 12) & 0x3F]);
        output.push_back(index + 1 < bytes.size() ? kAlphabet[(value >> 6) & 0x3F] : '=');
        output.push_back(index + 2 < bytes.size() ? kAlphabet[value & 0x3F] : '=');
    }
    return output;
}

bool WriteArtworkFile(
    const std::filesystem::path& target,
    const std::vector<std::uint8_t>& bytes) {
    std::error_code error;
    const auto parent = target.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        std::filesystem::create_directories(parent, error);
    }
    std::filesystem::path temporary = target;
    temporary += L".tmp";
    std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size()));
    stream.close();
    if (!stream) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    if (MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) == FALSE) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

std::int64_t ClampPosition(std::int64_t value, std::int64_t duration) {
    if (value < 0) {
        return 0;
    }
    if (duration > 0 && value > duration) {
        return duration;
    }
    return value;
}

std::string TrimSpaces(std::string_view text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && static_cast<unsigned char>(text[first]) <= ' ') {
        ++first;
    }
    while (last > first && static_cast<unsigned char>(text[last - 1]) <= ' ') {
        --last;
    }
    return std::string{text.substr(first, last - first)};
}

std::pair<std::string, std::string> SplitStreamTitle(const std::string& text) {
    const auto separator = text.find(" - ");
    if (separator == std::string::npos) {
        return {"Internet Stream", TrimSpaces(text)};
    }
    return {
        TrimSpaces(std::string_view{text}.substr(0, separator)),
        TrimSpaces(std::string_view{text}.substr(separator + 3))};
}

class WindowsMediaBackend final : public MediaBackend {
public:
    explicit WindowsMediaBackend(std::function<void(std::string_view)> trace)
        : trace_(std::move(trace)), stream_([this](std::string_view line) {
              Trace(std::string{"stream: "} + std::string{line});
          }) {}

    PlaybackSnapshot Poll() override {
        PlaybackSnapshot snapshot;
        snapshot.spotifyRunning = RefreshSpotifyProcess();
        if (streamActive_) {
            return StreamSnapshot();
        }
        if (!EnsureApartment()) {
            snapshot.backendError = true;
            return snapshot;
        }
        if (!EnsureManager()) {
            snapshot.backendError = managerFailed_;
            return snapshot;
        }
        try {
            const auto session = FindSpotifySession();
            if (!session) {
                if (!noSessionReported_) {
                    noSessionReported_ = true;
                    Trace("no Spotify session in the session list");
                }
                return snapshot;
            }
            if (noSessionReported_) {
                noSessionReported_ = false;
                Trace("Spotify session found");
            }
            snapshot.sessionAvailable = true;
            snapshot.sourceAppId = ToUtf8(session.SourceAppUserModelId());
            const auto playback = session.GetPlaybackInfo();
            snapshot.state = ToPlaybackState(playback.PlaybackStatus());
            const auto controls = playback.Controls();
            snapshot.canPlay = controls.IsPlayEnabled();
            snapshot.canPause = controls.IsPauseEnabled();
            snapshot.canToggle = controls.IsPlayPauseToggleEnabled();
            snapshot.canNext = controls.IsNextEnabled();
            snapshot.canPrevious = controls.IsPreviousEnabled();
            snapshot.canSeek = controls.IsPlaybackPositionEnabled();
            const auto timeline = session.GetTimelineProperties();
            const auto start = timeline.StartTime().count();
            const auto end = timeline.EndTime().count();
            const auto position = timeline.Position().count();
            const auto durationTicks = end - start;
            snapshot.durationMs = durationTicks > 0 ? durationTicks / kTicksPerMillisecond : 0;
            snapshot.positionMs = ClampPosition(
                position > start ? (position - start) / kTicksPerMillisecond : 0,
                snapshot.durationMs);
            try {
                const auto properties = session.TryGetMediaPropertiesAsync().get();
                const std::string artist = properties.Artist().size() == 0
                    ? ToUtf8(properties.AlbumArtist())
                    : ToUtf8(properties.Artist());
                snapshot.metadata = NormalizeMetadata(
                    ToUtf8(properties.Title()),
                    artist,
                    ToUtf8(properties.AlbumTitle()));
                CaptureArtwork(properties);
            } catch (const winrt::hresult_error&) {
                snapshot.metadata = NormalizeMetadata({}, {}, {});
            }
        } catch (const winrt::hresult_error&) {
            manager_ = nullptr;
            snapshot.sessionAvailable = false;
            snapshot.state = PlaybackState::Unavailable;
        } catch (...) {
            manager_ = nullptr;
            snapshot.sessionAvailable = false;
            snapshot.state = PlaybackState::Unavailable;
        }
        return snapshot;
    }

    bool Execute(const TransportCommand& command) override {
        if (command.action == TransportAction::Stream) {
            return StartStream(command.streamUrl);
        }
        if (command.action == TransportAction::StopStream) {
            return StopStream();
        }
        bool leftStream = false;
        if (streamActive_) {

            leftStream = StopStream();
            if (command.action == TransportAction::Pause) {
                return leftStream;
            }
        }
        if (!EnsureApartment() || !EnsureManager()) {
            return leftStream;
        }
        try {
            const auto session = FindSpotifySession();
            if (!session) {
                return leftStream;
            }
            bool result = false;
            switch (command.action) {
            case TransportAction::Play:
                result = session.TryPlayAsync().get();
                break;
            case TransportAction::Pause:
                result = session.TryPauseAsync().get();
                break;
            case TransportAction::Toggle:
                result = session.TryTogglePlayPauseAsync().get();
                break;
            case TransportAction::Next:
                result = session.TrySkipNextAsync().get();
                break;
            case TransportAction::Previous:
                result = session.TrySkipPreviousAsync().get();
                break;
            case TransportAction::Seek:
                result = command.positionMs >= 0 &&
                    session.TryChangePlaybackPositionAsync(command.positionMs * kTicksPerMillisecond).get();
                break;
            case TransportAction::Stream:
            case TransportAction::StopStream:
                break;
            }
            return result || leftStream;
        } catch (const winrt::hresult_error&) {
            manager_ = nullptr;
        } catch (...) {
            manager_ = nullptr;
        }
        return leftStream;
    }

    void SetArtworkPath(const std::filesystem::path& path) override {
        artworkPath_ = path;
    }

    void SetStationsPath(const std::filesystem::path& path) override {
        catalog_.SetPath(path);
    }

    void Shutdown() noexcept override {
        stream_.Shutdown();
        streamActive_ = false;
        manager_ = nullptr;
        if (apartmentInitialized_) {
            try {
                winrt::uninit_apartment();
            } catch (...) {
            }
            apartmentInitialized_ = false;
        }
    }

private:
    bool RefreshSpotifyProcess() {
        const bool running = IsSpotifyProcessRunning();
        if (running != spotifyProcessSeen_) {
            spotifyProcessSeen_ = running;
            Trace(running ? "Spotify process present" : "Spotify process absent");
        }
        return running;
    }

    PlaybackSnapshot StreamSnapshot() {
        PlaybackSnapshot snapshot;
        snapshot.spotifyRunning = spotifyProcessSeen_;
        snapshot.sessionAvailable = true;
        snapshot.streamActive = true;
        snapshot.state = stream_.State();
        snapshot.streamStation = streamTitle_;

        const std::string icyTitle = stream_.Title();
        if (!icyTitle.empty()) {
            const auto [artist, track] = SplitStreamTitle(icyTitle);
            snapshot.metadata = NormalizeMetadata(track, artist, "Live");
        } else {
            snapshot.metadata = NormalizeMetadata(streamTitle_, "Internet Stream", "Live");
        }

        snapshot.canPlay = true;
        snapshot.canPause = true;
        snapshot.canToggle = true;
        snapshot.canNext = false;
        snapshot.canPrevious = false;
        snapshot.canSeek = false;
        snapshot.durationMs = 0;
        snapshot.positionMs = 0;
        snapshot.sourceAppId = "76radio.stream";
        return snapshot;
    }

    bool StartStream(const std::string& url) {
        if (url.empty()) {
            return false;
        }
        if (!EnsureApartment()) {
            return false;
        }
        if (streamActive_) {
            StopStream();
        }
        streamUrl_ = url;
        streamTitle_ = catalog_.TitleFor(url);
        if (!stream_.Start(url)) {
            Trace("stream could not start: " + streamTitle_);
            streamActive_ = false;
            streamUrl_.clear();
            streamTitle_.clear();
            return false;
        }
        streamActive_ = true;
        Trace("stream starting " + streamTitle_ + " (" + url + ")");

        PauseSpotifyForStream();

        if (!artworkPath_.empty()) {
            RemoveArtworkFiles(artworkPath_);
            artworkSignature_.clear();
        }
        return true;
    }

    bool StopStream() {
        if (!streamActive_) {
            return false;
        }
        const std::string title = streamTitle_;
        stream_.Stop();
        streamActive_ = false;
        streamUrl_.clear();
        streamTitle_.clear();
        Trace("stream stopped " + title);
        return true;
    }

    void PauseSpotifyForStream() {
        if (!EnsureManager()) {
            return;
        }
        try {
            const auto session = FindSpotifySession();
            if (!session) {
                return;
            }
            if (session.GetPlaybackInfo().PlaybackStatus() !=
                GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                return;
            }
            if (!session.TryPauseAsync().get()) {
                Trace("Spotify did not accept the pause for the station");
            }
        } catch (const winrt::hresult_error&) {
            manager_ = nullptr;
        } catch (...) {
            manager_ = nullptr;
        }
    }

    template <typename Properties>
    void CaptureArtwork(const Properties& properties) {
        if (artworkPath_.empty()) {
            return;
        }
        std::string signature;
        try {
            signature = ToUtf8(properties.Title()) + "\n" +
                ToUtf8(properties.Artist()) + "\n" +
                ToUtf8(properties.AlbumTitle());
        } catch (...) {
            return;
        }
        if (signature == artworkSignature_) {
            return;
        }
        artworkSignature_ = signature;
        std::vector<std::uint8_t> bytes;
        try {
            const auto reference = properties.Thumbnail();
            if (reference) {
                const auto stream = reference.OpenReadAsync().get();
                const auto size = stream.Size();
                if (size > 0 && size <= 8ull * 1024ull * 1024ull) {
                    winrt::Windows::Storage::Streams::DataReader reader(
                        stream.GetInputStreamAt(0));
                    const auto loaded = reader.LoadAsync(static_cast<std::uint32_t>(size)).get();
                    if (loaded == size) {
                        bytes.resize(static_cast<std::size_t>(size));
                        reader.ReadBytes(bytes);
                    }
                }
            }
        } catch (const winrt::hresult_error&) {
            Trace("artwork read failed");
        } catch (...) {
            Trace("artwork read failed");
        }
        if (bytes.empty()) {
            RemoveArtworkFiles(artworkPath_);
            Trace("artwork cleared");
            return;
        }
        Trace("artwork thumbnail " + std::to_string(bytes.size()) + " bytes");
        const char* extension = nullptr;
        if (StartsWith(bytes, {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A})) {
            extension = ".png";
        } else if (StartsWith(bytes, {0xFF, 0xD8, 0xFF})) {
            extension = ".jpg";
        }
        if (extension == nullptr) {
            RemoveArtworkFiles(artworkPath_);
            Trace("artwork format unsupported");
            return;
        }
        std::filesystem::path target = artworkPath_;
        target += extension;
        if (!WriteArtworkFile(target, bytes)) {
            Trace("artwork write failed");
            return;
        }
        std::filesystem::path other = artworkPath_;
        other += std::strcmp(extension, ".png") == 0 ? ".jpg" : ".png";
        DeleteFileW(other.c_str());
        std::uint32_t pixelWidth = 0;
        std::uint32_t pixelHeight = 0;
        std::vector<std::uint8_t> pixels;
        if (DecodeArtworkPixels(bytes, pixelWidth, pixelHeight, pixels)) {
            const std::string header = std::to_string(pixelWidth) + " " +
                std::to_string(pixelHeight) + "\n";
            const std::string encoded = Base64Encode(pixels);
            std::vector<std::uint8_t> document(header.begin(), header.end());
            document.insert(document.end(), encoded.begin(), encoded.end());
            std::filesystem::path pixelPath = artworkPath_;
            pixelPath += ".pixels";
            if (WriteArtworkFile(pixelPath, document)) {
                Trace("artwork pixels written " + std::to_string(pixelWidth) +
                      "x" + std::to_string(pixelHeight) + ", " +
                      std::to_string(pixels.size()) + " raw, " +
                      std::to_string(document.size()) + " document, " +
                      pixelPath.string());
            } else {
                Trace("artwork pixels write failed " + pixelPath.string());
            }
        } else {
            std::filesystem::path pixelPath = artworkPath_;
            pixelPath += ".pixels";
            DeleteFileW(pixelPath.c_str());
            Trace("artwork pixels failed");
        }
        Trace(std::string{"artwork written "} + std::to_string(bytes.size()) +
              " bytes " + extension);
    }

    void Trace(std::string_view line) const {
        if (!trace_) {
            return;
        }
        try {
            trace_(line);
        } catch (...) {
        }
    }

    bool EnsureApartment() {
        if (apartmentInitialized_) {
            return true;
        }
        Trace("apartment starting");
        try {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            apartmentInitialized_ = true;
        } catch (const winrt::hresult_error&) {
            Trace("apartment refused");
            return false;
        }
        Trace("apartment ready");
        return true;
    }

    bool EnsureManager() {
        if (manager_) {
            return true;
        }
        Trace("session manager request starting");
        try {
            manager_ = GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            managerFailed_ = false;
        } catch (const winrt::hresult_error&) {
            manager_ = nullptr;
            managerFailed_ = true;
        } catch (...) {
            manager_ = nullptr;
            managerFailed_ = true;
        }
        Trace(manager_ ? "session manager ready" : "session manager request failed");
        return manager_ != nullptr;
    }

    GlobalSystemMediaTransportControlsSession FindSpotifySession() {
        if (!manager_) {
            return nullptr;
        }
        GlobalSystemMediaTransportControlsSession fallback{nullptr};
        for (const auto& session : manager_.GetSessions()) {
            if (!IsSpotifyApplication(ToUtf8(session.SourceAppUserModelId()))) {
                continue;
            }
            try {
                if (session.GetPlaybackInfo().PlaybackStatus() ==
                    GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing) {
                    return session;
                }
            } catch (const winrt::hresult_error&) {
            } catch (...) {
            }
            if (!fallback) {
                fallback = session;
            }
        }
        return fallback;
    }

    std::function<void(std::string_view)> trace_;
    std::filesystem::path artworkPath_;
    std::string artworkSignature_;
    StreamPlayer stream_;
    StationCatalog catalog_;
    bool streamActive_ = false;
    std::string streamUrl_;
    std::string streamTitle_;
    GlobalSystemMediaTransportControlsSessionManager manager_{nullptr};
    bool apartmentInitialized_ = false;
    bool managerFailed_ = false;
    bool spotifyProcessSeen_ = false;
    bool noSessionReported_ = false;
};

}

std::unique_ptr<MediaBackend> CreateWindowsMediaBackend(
    std::function<void(std::string_view)> trace) {
    return std::make_unique<WindowsMediaBackend>(std::move(trace));
}

}
