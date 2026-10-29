#include "radio76/document_channel.h"

#include "radio76/bridge_contract.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace radio76 {
namespace {

constexpr std::size_t kMaximumDocumentReadBytes = 4096;

constexpr std::int64_t kSeekJumpToleranceMs = 1500;

bool EnsureParentDirectory(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    if (parent.empty()) {
        return true;
    }
    std::error_code error;
    if (std::filesystem::exists(parent, error)) {
        return true;
    }
    std::filesystem::create_directories(parent, error);
    return !error;
}

std::string Narrow(const std::filesystem::path& path) {
    const auto wide = path.wstring();
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t character : wide) {
        narrow.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
    }
    return narrow;
}

bool ReadBoundedFile(const std::filesystem::path& path, std::string& content) {
    content.clear();
    const HANDLE file = CreateFileW(
        path.c_str(),
        GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    std::array<char, kMaximumDocumentReadBytes> buffer{};
    DWORD read = 0;
    const BOOL ok = ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr);
    CloseHandle(file);
    if (ok == FALSE) {
        return false;
    }
    content.assign(buffer.data(), read);
    return true;
}

std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

std::string LowerAscii(std::string_view text) {
    std::string lowered;
    lowered.reserve(text.size());
    for (const char character : text) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::string ReadPipboyColor(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return {};
    }
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
    bool haveRed = false;
    bool haveGreen = false;
    bool haveBlue = false;
    std::string section;
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto comment = line.find_first_of(";#");
        std::string_view text = comment == std::string::npos ? std::string_view{line} : std::string_view{line}.substr(0, comment);
        text = TrimAscii(text);
        if (text.empty()) {
            continue;
        }
        if (text.front() == '[' && text.back() == ']') {
            section = LowerAscii(TrimAscii(text.substr(1, text.size() - 2)));
            continue;
        }
        if (section != "pipboy") {
            continue;
        }
        const auto separator = text.find('=');
        if (separator == std::string_view::npos) {
            continue;
        }
        const std::string key = LowerAscii(TrimAscii(text.substr(0, separator)));
        const std::string number{TrimAscii(text.substr(separator + 1))};
        char* end = nullptr;
        const double value = std::strtod(number.c_str(), &end);
        if (end == number.c_str() || !std::isfinite(value)) {
            continue;
        }
        if (key == "fpipboyeffectcolorr") {
            red = value;
            haveRed = true;
        } else if (key == "fpipboyeffectcolorg") {
            green = value;
            haveGreen = true;
        } else if (key == "fpipboyeffectcolorb") {
            blue = value;
            haveBlue = true;
        }
    }
    if (!haveRed || !haveGreen || !haveBlue) {
        return {};
    }
    const auto toByte = [](double value) {
        const double clamped = std::clamp(value, 0.0, 1.0);
        return static_cast<int>(std::lround(clamped * 255.0));
    };
    const char* digits = "0123456789ABCDEF";
    const int channels[3] = {toByte(red), toByte(green), toByte(blue)};
    std::string hex;
    hex.reserve(6);
    for (const int channel : channels) {
        hex.push_back(digits[(channel >> 4) & 0xF]);
        hex.push_back(digits[channel & 0xF]);
    }
    return hex;
}

std::string AppendUiFields(std::string document, const ChannelUi& ui, std::string_view theme) {
    if (document.size() < 2 || document.back() != '}') {
        return document;
    }

    document.pop_back();
    document += ",\"uiStatus\":";
    document += ui.showStatus ? "1" : "0";
    document += ",\"uiArtStatus\":";
    document += ui.showArtStatus ? "1" : "0";
    document += ",\"uiArtwork\":";
    document += ui.showArtwork ? "1" : "0";
    document += ",\"uiKeepNowPlaying\":";
    document += ui.keepNowPlaying ? "1" : "0";
    if (!theme.empty()) {
        document += ",\"uiTheme\":\"";
        document += theme;
        document += "\"";
    }
    document += "}";
    return document;
}

bool WriteFileReplacing(const std::filesystem::path& path, const std::string& content) {
    if (!EnsureParentDirectory(path)) {
        return false;
    }
    std::filesystem::path temporary = path;
    temporary += L".tmp";
    {
        const HANDLE file = CreateFileW(
            temporary.c_str(),
            GENERIC_WRITE,
            0,
            nullptr,
            CREATE_ALWAYS,
            FILE_ATTRIBUTE_NORMAL,
            nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            return false;
        }
        DWORD written = 0;
        const BOOL ok = content.empty()
            ? TRUE
            : WriteFile(file, content.data(), static_cast<DWORD>(content.size()), &written, nullptr);
        const BOOL flushed = FlushFileBuffers(file);
        CloseHandle(file);
        if (ok == FALSE || flushed == FALSE || written != content.size()) {
            DeleteFileW(temporary.c_str());
            return false;
        }
    }
    if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == FALSE) {
        DeleteFileW(temporary.c_str());
        return false;
    }
    return true;
}

}

DocumentChannel::DocumentChannel(
    NativeRuntime& runtime,
    ChannelPaths paths,
    std::chrono::milliseconds pollInterval,
    ChannelUi ui)
    : runtime_(runtime), paths_(std::move(paths)), ui_(ui), pollInterval_(pollInterval) {
    if (pollInterval_.count() < 20) {
        pollInterval_ = std::chrono::milliseconds(20);
    }
}

DocumentChannel::~DocumentChannel() {
    Stop();
}

bool DocumentChannel::Start() {
    if (running_.load()) {
        return true;
    }
    if (!EnsureParentDirectory(paths_.stateDocument) || !EnsureParentDirectory(paths_.commandDocument)) {
        Log("channel could not create its document directory");
        return false;
    }
    std::string document;
    if (ReadCommandDocument(document)) {
        bridge::CommandDocument parsed;
        if (bridge::ParseCommandDocument(document, parsed)) {
            std::scoped_lock lock{seenMutex_};
            seenStamp_ = parsed.stamp;
            seenCommand_ = parsed.command;
        }
    }

    {
        std::scoped_lock lock{seenMutex_};
        seenDiagStamp_ = 0;
        seenDiagCommand_.clear();
    }
    std::string diag;
    if (ReadDiagDocument(diag)) {
        bridge::CommandDocument parsed;
        if (bridge::ParseCommandDocument(diag, parsed)) {
            std::scoped_lock lock{seenMutex_};
            seenDiagStamp_ = parsed.stamp;
            seenDiagCommand_ = parsed.command;
        }
    }
    stopRequested_.store(false);
    running_.store(true);
    worker_ = std::thread(&DocumentChannel::WorkerMain, this);
    Log("channel started");
    Log("reading " + Narrow(paths_.commandDocument));
    Log("writing " + Narrow(paths_.stateDocument));
    return true;
}

void DocumentChannel::Stop() noexcept {
    if (worker_.joinable()) {
        stopRequested_.store(true);
        worker_.join();
    }
    if (running_.exchange(false)) {
        Log("channel stopped");
    }
}

bool DocumentChannel::Running() const noexcept {
    return running_.load();
}

void DocumentChannel::WorkerMain() {
    const auto started = std::chrono::steady_clock::now();
    PublishOnce(true);
    auto lastWaitingLog = started;
    bool answered = false;
    while (!stopRequested_.load()) {
        PollDiagDocument();
        PollOnce();
        PublishOnce(false);
        const auto now = std::chrono::steady_clock::now();
        if (!answered && runtime_.HasPublished()) {
            answered = true;
            const auto seconds = std::chrono::duration_cast<std::chrono::seconds>(now - started).count();
            Log("media service answered after " + std::to_string(seconds) + " s");
        }
        if (!answered && now - started > std::chrono::seconds(5) &&
            now - lastWaitingLog > std::chrono::seconds(30)) {

            Log("no media reading yet, the system media service has not answered");
            lastWaitingLog = now;
        }
        std::this_thread::sleep_for(pollInterval_);
    }
}

bool DocumentChannel::PollOnce() {
    std::string document;
    if (!ReadCommandDocument(document)) {
        return false;
    }
    bridge::CommandDocument parsed;
    if (!bridge::ParseCommandDocument(document, parsed)) {
        commandsRejected_.fetch_add(1);
        Log("command document rejected as malformed");
        return false;
    }
    {
        std::scoped_lock lock{seenMutex_};

        if (parsed.stamp == seenStamp_ && parsed.command == seenCommand_) {
            return false;
        }
        seenStamp_ = parsed.stamp;
        seenCommand_ = parsed.command;
    }
    constexpr std::string_view diagnosticPrefix = "diag:";
    if (parsed.command.rfind(diagnosticPrefix, 0) == 0) {

        Log("page: " + parsed.command.substr(diagnosticPrefix.size()));
        return true;
    }
    const auto outcome = runtime_.HandleCommand(parsed.command);
    if (outcome.accepted()) {
        commandsExecuted_.fetch_add(1);
        Log("command executed: " + parsed.command);
        return true;
    }
    commandsRejected_.fetch_add(1);
    Log("command rejected: " + parsed.command + " (" + outcome.detail + ")");
    return false;
}

void DocumentChannel::PollDiagDocument() {
    if (paths_.diagDocument.empty()) {
        return;
    }
    std::string document;
    if (!ReadDiagDocument(document)) {
        return;
    }
    bridge::CommandDocument parsed;
    if (!bridge::ParseCommandDocument(document, parsed)) {
        return;
    }
    {
        std::scoped_lock lock{seenMutex_};
        if (parsed.stamp == seenDiagStamp_ && parsed.command == seenDiagCommand_) {
            return;
        }
        seenDiagStamp_ = parsed.stamp;
        seenDiagCommand_ = parsed.command;
    }
    constexpr std::string_view diagnosticPrefix = "diag:";
    std::string_view line = parsed.command;
    if (line.rfind(diagnosticPrefix, 0) == 0) {
        line.remove_prefix(diagnosticPrefix.size());
    }
    Log("page: " + std::string{line});
}

void DocumentChannel::UpdatePipboyTheme() {
    if (paths_.prefsFile.empty()) {
        return;
    }
    std::error_code error;
    const auto stamp = std::filesystem::last_write_time(paths_.prefsFile, error);
    if (error) {
        if (!pipboyThemeRead_) {
            pipboyThemeRead_ = true;
            Log("pipboy color: prefs file not readable");
        }
        return;
    }
    if (pipboyThemeRead_ && stamp == pipboyThemeStamp_) {
        return;
    }
    pipboyThemeRead_ = true;
    pipboyThemeStamp_ = stamp;
    const std::string color = ReadPipboyColor(paths_.prefsFile);
    if (color.empty()) {
        if (!pipboyTheme_.empty()) {
            Log("pipboy color: prefs no longer has one");
        }
        pipboyTheme_.clear();
        return;
    }
    if (color != pipboyTheme_) {
        pipboyTheme_ = color;
        Log("pipboy color: " + color);
    }
}

bool DocumentChannel::PublishOnce(bool force) {
    const auto snapshot = runtime_.Current();
    UpdatePipboyTheme();
    const std::string document = AppendUiFields(bridge::BuildStateDocument(snapshot.get()), ui_, pipboyTheme_);

    const std::string signature = AppendUiFields(bridge::BuildStateSignature(snapshot.get()), ui_, pipboyTheme_);
    bool seekDetected = false;
    {
        std::scoped_lock lock{publishedMutex_};

        if (snapshot != nullptr) {
            if (observedSnapshotValid_) {
                std::int64_t elapsedMs = 0;
                if (snapshot->state == PlaybackState::Playing &&
                    observedPlaybackState_ == PlaybackState::Playing) {
                    elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        snapshot->capturedAt - observedCapturedAt_).count();
                    if (elapsedMs < 0) {
                        elapsedMs = 0;
                    }
                }
                const std::int64_t expected = observedPositionMs_ + elapsedMs;
                std::int64_t delta = snapshot->positionMs - expected;
                if (delta < 0) {
                    delta = -delta;
                }
                seekDetected = delta > kSeekJumpToleranceMs;
            }
            observedSnapshotValid_ = true;
            observedPositionMs_ = snapshot->positionMs;
            observedPlaybackState_ = snapshot->state;
            observedCapturedAt_ = snapshot->capturedAt;
        } else {
            observedSnapshotValid_ = false;
        }
        if (!force && !seekDetected && signature == publishedSignature_) {
            return false;
        }
        publishedSignature_ = signature;
    }
    if (!WriteStateDocument(document)) {
        Log("state document could not be written");
        return false;
    }
    stateWrites_.fetch_add(1);
    const std::string line = DescribeState(snapshot.get());
    bool announce = force;
    {
        std::scoped_lock lock{publishedMutex_};
        if (line != lastStateLine_) {
            announce = true;
            lastStateLine_ = line;
        }
    }
    if (announce) {
        Log(line);
    }
    return true;
}

std::string DocumentChannel::DescribeState(const PlaybackSnapshot* snapshot) {
    if (snapshot == nullptr) {
        return "state published: no snapshot";
    }
    if (snapshot->capturedAt.time_since_epoch().count() == 0) {

        return "state published: waiting for the media service";
    }
    std::string line = "state published: ";
    line += PlaybackStateName(snapshot->state);
    if (snapshot->sessionAvailable) {
        line += " | ";
        line += snapshot->metadata.artist.empty() ? std::string{"unknown artist"} : bridge::ClampMetadata(snapshot->metadata.artist);
        line += " - ";
        line += snapshot->metadata.title.empty() ? std::string{"unknown track"} : bridge::ClampMetadata(snapshot->metadata.title);
        return line;
    }
    line += snapshot->spotifyRunning ? " | spotify running, no session" : " | spotify not running";
    return line;
}

std::uint64_t DocumentChannel::CommandsExecuted() const noexcept {
    return commandsExecuted_.load();
}

std::uint64_t DocumentChannel::CommandsRejected() const noexcept {
    return commandsRejected_.load();
}

std::uint64_t DocumentChannel::StateWrites() const noexcept {
    return stateWrites_.load();
}

std::string DocumentChannel::LastCommand() const {
    std::scoped_lock lock{seenMutex_};
    return seenCommand_;
}

bool DocumentChannel::ReadCommandDocument(std::string& document) const {
    return ReadBoundedFile(paths_.commandDocument, document);
}

bool DocumentChannel::ReadDiagDocument(std::string& document) const {
    if (paths_.diagDocument.empty()) {
        return false;
    }
    return ReadBoundedFile(paths_.diagDocument, document);
}

bool DocumentChannel::WriteStateDocument(const std::string& document) {
    return WriteFileReplacing(paths_.stateDocument, document);
}

void DocumentChannel::Log(std::string_view line) const {
    AppendLogLine(paths_.logFile, line, paths_.logMaxBytes);
}

}
