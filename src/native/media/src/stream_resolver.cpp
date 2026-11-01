#include "radio76/stream_resolver.h"

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cwchar>
#include <set>
#include <string>
#include <utility>

namespace radio76 {
namespace {

constexpr DWORD kConnectTimeoutMs = 5000;
constexpr DWORD kRequestTimeoutMs = 8000;
constexpr DWORD kMaximumRedirects = 5;
constexpr std::size_t kMaximumPlaylistBytes = 256 * 1024;
constexpr std::size_t kMaximumCandidates = 32;
constexpr int kMaximumPlaylistDepth = 2;

class WinHttpHandle {
public:
    explicit WinHttpHandle(HINTERNET value = nullptr) : value_(value) {}
    ~WinHttpHandle() {
        if (value_) {
            WinHttpCloseHandle(value_);
        }
    }
    WinHttpHandle(const WinHttpHandle&) = delete;
    WinHttpHandle& operator=(const WinHttpHandle&) = delete;
    [[nodiscard]] HINTERNET Get() const noexcept { return value_; }
    void Release() noexcept { value_ = nullptr; }
    [[nodiscard]] explicit operator bool() const noexcept { return value_ != nullptr; }

private:
    HINTERNET value_{};
};

std::string LowerAscii(std::string_view value) {
    std::string lowered;
    lowered.reserve(value.size());
    for (const char character : value) {
        lowered.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(character))));
    }
    return lowered;
}

std::string Trim(std::string_view value) {
    std::size_t first = 0;
    std::size_t last = value.size();
    while (first < last && std::isspace(static_cast<unsigned char>(value[first])) != 0) {
        ++first;
    }
    while (last > first && std::isspace(static_cast<unsigned char>(value[last - 1])) != 0) {
        --last;
    }
    return std::string{value.substr(first, last - first)};
}

std::wstring Widen(std::string_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (size <= 0) {
        return {};
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), wide.data(), size);
    return wide;
}

std::string Narrow(std::wstring_view value) {
    if (value.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (size <= 0) {
        return {};
    }
    std::string narrow(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(
        CP_UTF8, 0, value.data(), static_cast<int>(value.size()), narrow.data(), size, nullptr, nullptr);
    return narrow;
}

bool IsHttpUrl(std::string_view value) {
    const std::string lowered = LowerAscii(value);
    return lowered.rfind("http://", 0) == 0 || lowered.rfind("https://", 0) == 0;
}

std::string ContentType(HINTERNET request) {
    DWORD length = 0;
    WinHttpQueryHeaders(
        request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
        WINHTTP_NO_OUTPUT_BUFFER, &length, WINHTTP_NO_HEADER_INDEX);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER || length == 0) {
        return {};
    }
    std::wstring value(length / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryHeaders(
            request, WINHTTP_QUERY_CONTENT_TYPE, WINHTTP_HEADER_NAME_BY_INDEX,
            value.data(), &length, WINHTTP_NO_HEADER_INDEX)) {
        return {};
    }
    value.resize(std::wcslen(value.c_str()));
    return LowerAscii(Narrow(value));
}

std::string FinalUrl(HINTERNET request, std::string_view fallback) {
    DWORD length = 0;
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, nullptr, &length) || length == 0) {
        if (GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return std::string{fallback};
        }
    }
    std::wstring value(length / sizeof(wchar_t), L'\0');
    if (!WinHttpQueryOption(request, WINHTTP_OPTION_URL, value.data(), &length)) {
        return std::string{fallback};
    }
    value.resize(std::wcslen(value.c_str()));
    const std::string result = Narrow(value);
    return result.empty() ? std::string{fallback} : result;
}

std::string CombineUrl(std::string_view base, std::string_view candidate) {
    if (IsHttpUrl(candidate)) {
        return std::string{candidate};
    }
    const auto scheme = base.find("://");
    if (scheme == std::string_view::npos) {
        return {};
    }
    const auto authorityEnd = base.find('/', scheme + 3);
    const std::string origin = authorityEnd == std::string_view::npos
        ? std::string{base}
        : std::string{base.substr(0, authorityEnd)};
    if (!candidate.empty() && candidate.front() == '/') {
        return origin + std::string{candidate};
    }
    std::string directory = authorityEnd == std::string_view::npos
        ? origin + "/"
        : std::string{base.substr(0, base.rfind('/') + 1)};
    return directory + std::string{candidate};
}

struct Response {
    bool success = false;
    std::string finalUrl;
    std::string contentType;
    std::string body;
};

class AttachedRequest {
public:
    AttachedRequest(ResolveCancellation* cancellation, WinHttpHandle& request)
        : cancellation_(cancellation), request_(request) {
        attached_ = cancellation_ == nullptr || cancellation_->Attach(request_.Get());
    }
    ~AttachedRequest() {
        if (cancellation_ != nullptr && attached_ && cancellation_->Detach()) {
            request_.Release();
        }
    }
    AttachedRequest(const AttachedRequest&) = delete;
    AttachedRequest& operator=(const AttachedRequest&) = delete;
    [[nodiscard]] bool Attached() const noexcept { return attached_; }

private:
    ResolveCancellation* cancellation_;
    WinHttpHandle& request_;
    bool attached_ = false;
};

Response Fetch(std::string_view url, ResolveCancellation* cancellation) {
    Response response;
    if (cancellation != nullptr && cancellation->Cancelled()) {
        return response;
    }
    const std::wstring wideUrl = Widen(url);
    if (wideUrl.empty()) {
        return response;
    }
    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wideUrl.c_str(), 0, 0, &parts)) {
        return response;
    }
    const std::wstring host{parts.lpszHostName, parts.dwHostNameLength};
    std::wstring path{parts.lpszUrlPath, parts.dwUrlPathLength};
    if (parts.dwExtraInfoLength > 0 && parts.lpszExtraInfo) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    WinHttpHandle session{WinHttpOpen(
        L"76Radio/0.3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session) {
        return response;
    }
    WinHttpSetTimeouts(
        session.Get(), kConnectTimeoutMs, kConnectTimeoutMs,
        kRequestTimeoutMs, kRequestTimeoutMs);
    WinHttpHandle connection{WinHttpConnect(session.Get(), host.c_str(), parts.nPort, 0)};
    if (!connection) {
        return response;
    }
    WinHttpHandle request{WinHttpOpenRequest(
        connection.Get(), L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0)};
    if (!request) {
        return response;
    }
    AttachedRequest attached{cancellation, request};
    if (!attached.Attached()) {
        return response;
    }
    DWORD redirects = kMaximumRedirects;
    WinHttpSetOption(
        request.Get(), WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS,
        &redirects, sizeof(redirects));
    const wchar_t* headers = L"Accept: audio/*, application/vnd.apple.mpegurl, audio/x-mpegurl, audio/x-scpls, */*\r\n";
    if (!WinHttpSendRequest(
            request.Get(), headers, static_cast<DWORD>(-1),
            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request.Get(), nullptr)) {
        return response;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.Get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status < 200 || status >= 300) {
        return response;
    }
    response.finalUrl = FinalUrl(request.Get(), url);
    response.contentType = ContentType(request.Get());
    PlaylistFormat format = DetectPlaylistFormat(response.finalUrl, response.contentType, {});
    if (format == PlaylistFormat::None) {
        response.success = true;
        return response;
    }
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.Get(), &available)) {
            return {};
        }
        if (available == 0) {
            break;
        }
        if (response.body.size() + available > kMaximumPlaylistBytes) {
            return {};
        }
        const std::size_t offset = response.body.size();
        response.body.resize(offset + available);
        DWORD received = 0;
        if (!WinHttpReadData(request.Get(), response.body.data() + offset, available, &received)) {
            return {};
        }
        response.body.resize(offset + received);
    }
    response.success = true;
    return response;
}

StreamResolveResult Resolve(
    std::string_view url,
    int depth,
    std::set<std::string>& visited,
    const std::function<void(std::string_view)>& trace,
    ResolveCancellation* cancellation) {
    if (cancellation != nullptr && cancellation->Cancelled()) {
        return {StreamResolveStatus::Failed, {}, {}};
    }
    if (!IsHttpUrl(url)) {
        return {StreamResolveStatus::Unsupported, {}, {}};
    }
    const std::string key = LowerAscii(url);
    if (depth > kMaximumPlaylistDepth || !visited.insert(key).second) {
        return {StreamResolveStatus::Failed, {}, {}};
    }
    Response response = Fetch(url, cancellation);
    if (cancellation != nullptr && cancellation->Cancelled()) {
        return {StreamResolveStatus::Failed, {}, {}};
    }
    if (!response.success) {
        if (trace) {
            trace("stream address could not be resolved");
        }
        if (DetectPlaylistFormat(url, {}, {}) == PlaylistFormat::None) {
            return {
                StreamResolveStatus::Resolved,
                std::string{url},
                std::string{url}};
        }
        return {StreamResolveStatus::Failed, {}, {}};
    }
    const PlaylistFormat format = DetectPlaylistFormat(
        response.finalUrl, response.contentType, response.body);
    if (format == PlaylistFormat::None || format == PlaylistFormat::Hls) {
        return {
            StreamResolveStatus::Resolved,
            response.finalUrl,
            format == PlaylistFormat::Hls ? std::string{} : response.finalUrl};
    }
    const auto candidates = ParsePlaylist(response.body, format, response.finalUrl);
    for (const auto& candidate : candidates) {
        StreamResolveResult result = Resolve(candidate, depth + 1, visited, trace, cancellation);
        if (result.status == StreamResolveStatus::Resolved) {
            return result;
        }
    }
    return {StreamResolveStatus::Failed, {}, {}};
}

}

PlaylistFormat DetectPlaylistFormat(
    std::string_view url,
    std::string_view contentType,
    std::string_view body) {
    const std::string loweredBody = LowerAscii(body.substr(0, std::min<std::size_t>(body.size(), 4096)));
    if (loweredBody.find("#ext-x-") != std::string::npos) {
        return PlaylistFormat::Hls;
    }
    const std::string type = LowerAscii(contentType);
    if (type.find("scpls") != std::string::npos || type.find("pls+xml") != std::string::npos) {
        return PlaylistFormat::Pls;
    }
    if (type.find("mpegurl") != std::string::npos || type.find("m3u") != std::string::npos) {
        return PlaylistFormat::M3u;
    }
    std::string path = LowerAscii(url);
    const auto query = path.find_first_of("?#");
    if (query != std::string::npos) {
        path.resize(query);
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".pls") {
        return PlaylistFormat::Pls;
    }
    if ((path.size() >= 4 && path.substr(path.size() - 4) == ".m3u") ||
        (path.size() >= 5 && path.substr(path.size() - 5) == ".m3u8")) {
        return PlaylistFormat::M3u;
    }
    return PlaylistFormat::None;
}

std::vector<std::string> ParsePlaylist(
    std::string_view body,
    PlaylistFormat format,
    std::string_view baseUrl) {
    std::vector<std::string> candidates;
    std::size_t cursor = 0;
    while (cursor <= body.size() && candidates.size() < kMaximumCandidates) {
        const auto end = body.find_first_of("\r\n", cursor);
        const auto lineView = body.substr(
            cursor, end == std::string_view::npos ? body.size() - cursor : end - cursor);
        std::string line = Trim(lineView);
        if (format == PlaylistFormat::Pls) {
            const std::string lowered = LowerAscii(line);
            if (lowered.rfind("file", 0) == 0) {
                const auto separator = line.find('=');
                if (separator != std::string::npos) {
                    line = Trim(std::string_view{line}.substr(separator + 1));
                } else {
                    line.clear();
                }
            } else {
                line.clear();
            }
        } else if (line.empty() || line.front() == '#') {
            line.clear();
        }
        if (!line.empty()) {
            const std::string candidate = CombineUrl(baseUrl, line);
            if (IsHttpUrl(candidate) &&
                std::find(candidates.begin(), candidates.end(), candidate) == candidates.end()) {
                candidates.push_back(candidate);
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        cursor = end + 1;
        if (cursor < body.size() && body[end] == '\r' && body[cursor] == '\n') {
            ++cursor;
        }
    }
    return candidates;
}

void ResolveCancellation::Cancel() noexcept {
    std::lock_guard lock(mutex_);
    cancelled_ = true;
    if (handle_ != nullptr && !closed_) {
        closed_ = true;
        WinHttpCloseHandle(static_cast<HINTERNET>(handle_));
    }
}

bool ResolveCancellation::Cancelled() const noexcept {
    std::lock_guard lock(mutex_);
    return cancelled_;
}

bool ResolveCancellation::Attach(void* handle) noexcept {
    std::lock_guard lock(mutex_);
    if (cancelled_) {
        return false;
    }
    handle_ = handle;
    closed_ = false;
    return true;
}

bool ResolveCancellation::Detach() noexcept {
    std::lock_guard lock(mutex_);
    const bool wasClosed = closed_;
    handle_ = nullptr;
    closed_ = false;
    return wasClosed;
}

StreamResolveResult ResolveStreamUrl(
    std::string_view url,
    std::function<void(std::string_view)> trace,
    ResolveCancellation* cancellation) {
    std::set<std::string> visited;
    return Resolve(url, 0, visited, trace, cancellation);
}

}
