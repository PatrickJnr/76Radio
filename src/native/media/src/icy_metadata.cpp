#include "radio76/icy_metadata.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cwchar>
#include <mutex>
#include <string>
#include <thread>

namespace radio76 {
namespace {

constexpr int kReceiveTimeoutMs = 1000;
constexpr int kConnectTimeoutMs = 5000;
constexpr int kReconnectDelayMs = 3000;
constexpr std::size_t kMaxMetadataInterval = 1024 * 1024;
constexpr std::size_t kMaxTitleLength = 300;

bool IsBlank(char character) {
    const auto byte = static_cast<unsigned char>(character);
    return byte <= 0x20 || byte == 0x7f;
}

std::string Trim(std::string_view text) {
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && IsBlank(text[first])) {
        ++first;
    }
    while (last > first && IsBlank(text[last - 1])) {
        --last;
    }
    return std::string{text.substr(first, last - first)};
}

std::wstring Widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string Narrow(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), narrow.data(), size, nullptr,
        nullptr);
    return narrow;
}

class WinHttpHandle {
public:
    WinHttpHandle() = default;

    explicit WinHttpHandle(HINTERNET handle) : handle_(handle) {}

    ~WinHttpHandle() {
        if (handle_ != nullptr) {
            WinHttpCloseHandle(handle_);
        }
    }

    WinHttpHandle(const WinHttpHandle&) = delete;

    WinHttpHandle& operator=(const WinHttpHandle&) = delete;

    [[nodiscard]] HINTERNET Get() const noexcept { return handle_; }

    [[nodiscard]] explicit operator bool() const noexcept { return handle_ != nullptr; }

private:
    HINTERNET handle_{};
};

bool ReadExact(
    HINTERNET request,
    std::string& buffer,
    std::size_t size,
    const std::atomic<bool>& stop) {
    buffer.assign(size, '\0');
    std::size_t filled = 0;
    int timeouts = 0;
    while (filled < size) {
        if (stop.load(std::memory_order_relaxed)) {
            return false;
        }
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available)) {
            if (GetLastError() == ERROR_WINHTTP_TIMEOUT && ++timeouts < 120) {
                continue;
            }
            return false;
        }
        if (available == 0) {
            return false;
        }
        timeouts = 0;
        const auto wanted = static_cast<DWORD>(
            std::min<std::size_t>(available, size - filled));
        DWORD received = 0;
        if (!WinHttpReadData(request, buffer.data() + filled, wanted, &received)) {
            if (GetLastError() == ERROR_WINHTTP_TIMEOUT && ++timeouts < 120) {
                continue;
            }
            return false;
        }
        filled += received;
    }
    return true;
}

std::string QueryHeader(HINTERNET request, const wchar_t* name) {
    DWORD length = 0;
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_CUSTOM, name, WINHTTP_NO_OUTPUT_BUFFER, &length,
        WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        return {};
    }
    std::wstring value(length / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request, WINHTTP_QUERY_CUSTOM, name, value.data(), &length,
            WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    value.resize(std::wcslen(value.c_str()));
    return Narrow(value);
}

}

std::string ParseIcyStreamTitle(std::string_view block) {
    constexpr std::string_view key = "StreamTitle='";
    const auto start = block.find(key);
    if (start == std::string_view::npos) {
        return {};
    }
    const auto valueStart = start + key.size();
    const auto end = block.find("';", valueStart);
    if (end == std::string_view::npos) {
        return {};
    }
    auto title = Trim(block.substr(valueStart, end - valueStart));
    if (title.size() > kMaxTitleLength) {
        title.resize(kMaxTitleLength);
    }
    return title;
}

std::string ParseIcyStationName(std::string_view header) {
    auto name = Trim(header);
    if (name.size() > kMaxTitleLength) {
        name.resize(kMaxTitleLength);
    }
    return name;
}

struct IcyMetadataReader::Impl {
    explicit Impl(std::function<void(std::string_view)> sink) : trace(std::move(sink)) {}

    ~Impl() { Stop(); }

    void Trace(std::string_view line) const {
        if (!trace) {
            return;
        }
        try {
            trace(line);
        } catch (...) {
        }
    }

    void SetStation(std::string value) {
        std::scoped_lock lock{mutex};
        station = std::move(value);
    }

    void SetTitle(std::string value) {
        std::scoped_lock lock{mutex};
        if (title == value) {
            return;
        }
        title = std::move(value);
    }

    void Start(std::string_view url) {
        Stop();
        {
            std::scoped_lock lock{mutex};
            title.clear();
            station.clear();
            address = std::string{url};
        }
        stop.store(false, std::memory_order_relaxed);
        thread = std::thread([this] { Run(); });
    }

    void Stop() noexcept {
        stop.store(true, std::memory_order_relaxed);
        if (thread.joinable()) {
            thread.join();
        }
    }

    [[nodiscard]] std::string Title() const {
        std::scoped_lock lock{mutex};
        return title;
    }

    [[nodiscard]] std::string Station() const {
        std::scoped_lock lock{mutex};
        return station;
    }

    void Run() {
        while (!stop.load(std::memory_order_relaxed)) {
            const bool wantAnother = ReadOnce();
            if (!wantAnother) {
                break;
            }
            for (int waited = 0; waited < kReconnectDelayMs && !stop.load(std::memory_order_relaxed);
                 waited += 100) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }
    }

    bool ReadOnce() {
        std::string url;
        {
            std::scoped_lock lock{mutex};
            url = address;
        }
        if (url.empty()) {
            return false;
        }
        const auto wideUrl = Widen(url);
        if (wideUrl.empty()) {
            return false;
        }

        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof(parts);
        parts.dwHostNameLength = static_cast<DWORD>(-1);
        parts.dwUrlPathLength = static_cast<DWORD>(-1);
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        parts.dwSchemeLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
            Trace("icy: address not understood");
            return false;
        }
        const std::wstring host{parts.lpszHostName, parts.dwHostNameLength};
        std::wstring path{parts.lpszUrlPath, parts.dwUrlPathLength};
        if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo != nullptr) {
            path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        }
        const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

        WinHttpHandle session{WinHttpOpen(
            L"76Radio/0.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME,
            WINHTTP_NO_PROXY_BYPASS, 0)};
        if (!session) {
            return true;
        }
        WinHttpSetTimeouts(
            session.Get(), kConnectTimeoutMs, kConnectTimeoutMs, kReceiveTimeoutMs,
            kReceiveTimeoutMs);

        WinHttpHandle connection{
            WinHttpConnect(session.Get(), host.c_str(), parts.nPort, 0)};
        if (!connection) {
            return true;
        }
        WinHttpHandle request{WinHttpOpenRequest(
            connection.Get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
            WINHTTP_DEFAULT_ACCEPT_TYPES, secure ? WINHTTP_FLAG_SECURE : 0)};
        if (!request) {
            return true;
        }
        const wchar_t* headers = L"Icy-MetaData: 1\r\nAccept: */*\r\n";
        if (!WinHttpSendRequest(
                request.Get(), headers, static_cast<DWORD>(-1), WINHTTP_NO_REQUEST_DATA, 0, 0,
                0)) {
            return true;
        }
        if (!WinHttpReceiveResponse(request.Get(), nullptr)) {
            return true;
        }

        DWORD status = 0;
        DWORD statusSize = sizeof(status);
        if (!WinHttpQueryHeaders(
                request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
            status != 200) {
            return true;
        }

        const auto stationName = ParseIcyStationName(QueryHeader(request.Get(), L"icy-name"));
        if (!stationName.empty()) {
            SetStation(stationName);
        }

        const auto metaIntText = Trim(QueryHeader(request.Get(), L"icy-metaint"));
        if (metaIntText.empty()) {

            return false;
        }
        std::size_t metaInt = 0;
        try {
            metaInt = static_cast<std::size_t>(std::stoul(metaIntText));
        } catch (...) {
            return false;
        }
        if (metaInt == 0 || metaInt > kMaxMetadataInterval) {
            return false;
        }

        std::string audio;
        std::string block;
        std::string lengthByte;
        while (!stop.load(std::memory_order_relaxed)) {
            if (!ReadExact(request.Get(), audio, metaInt, stop)) {
                return true;
            }
            if (!ReadExact(request.Get(), lengthByte, 1, stop)) {
                return true;
            }
            const auto blockSize =
                static_cast<std::size_t>(static_cast<unsigned char>(lengthByte[0])) * 16;
            if (blockSize == 0) {
                continue;
            }
            if (!ReadExact(request.Get(), block, blockSize, stop)) {
                return true;
            }
            const auto parsedTitle = ParseIcyStreamTitle(block);
            if (!parsedTitle.empty()) {
                SetTitle(parsedTitle);
            }
        }
        return false;
    }

    std::function<void(std::string_view)> trace;
    mutable std::mutex mutex;
    std::string address;
    std::string title;
    std::string station;
    std::atomic<bool> stop{false};
    std::thread thread;
};

IcyMetadataReader::IcyMetadataReader(std::function<void(std::string_view)> trace)
    : impl_(std::make_unique<Impl>(std::move(trace))) {}

IcyMetadataReader::~IcyMetadataReader() = default;

void IcyMetadataReader::Start(std::string_view url) {
    impl_->Start(url);
}

void IcyMetadataReader::Stop() noexcept {
    impl_->Stop();
}

std::string IcyMetadataReader::Title() const {
    return impl_->Title();
}

std::string IcyMetadataReader::Station() const {
    return impl_->Station();
}

}
