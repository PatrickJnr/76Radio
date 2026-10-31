#pragma once

#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

namespace radio76 {

enum class PlaylistFormat {
    None,
    M3u,
    Pls,
    Hls
};

enum class StreamResolveStatus {
    Resolved,
    Failed,
    Unsupported
};

struct StreamResolveResult {
    StreamResolveStatus status = StreamResolveStatus::Failed;
    std::string playbackUrl;
    std::string metadataUrl;
};

class ResolveCancellation {
public:
    void Cancel() noexcept;

    [[nodiscard]] bool Cancelled() const noexcept;

    bool Attach(void* handle) noexcept;

    bool Detach() noexcept;

private:
    mutable std::mutex mutex_;
    bool cancelled_ = false;
    bool closed_ = false;
    void* handle_ = nullptr;
};

PlaylistFormat DetectPlaylistFormat(
    std::string_view url,
    std::string_view contentType,
    std::string_view body);

std::vector<std::string> ParsePlaylist(
    std::string_view body,
    PlaylistFormat format,
    std::string_view baseUrl);

StreamResolveResult ResolveStreamUrl(
    std::string_view url,
    std::function<void(std::string_view)> trace = {},
    ResolveCancellation* cancellation = nullptr);

}
