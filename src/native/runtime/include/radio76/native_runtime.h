#pragma once

#include "radio76/bridge_contract.h"
#include "radio76/media_service.h"

#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace radio76 {

class NativeRuntime {
public:
    using BackendFactory = std::function<std::unique_ptr<MediaBackend>()>;

    explicit NativeRuntime(BackendFactory factory = {}, MediaService::Options options = {});

    ~NativeRuntime();

    NativeRuntime(const NativeRuntime&) = delete;

    NativeRuntime& operator=(const NativeRuntime&) = delete;

    bool Start();

    void Stop() noexcept;

    [[nodiscard]] bool ShutdownTimedOut() const noexcept;

    [[nodiscard]] bool IsRunning() const noexcept;

    bridge::CommandOutcome HandleCommand(std::string_view text);

    std::string StatePayload() const;

    [[nodiscard]] std::shared_ptr<const PlaybackSnapshot> Current() const noexcept;

    void SetListener(MediaService::Listener listener);

    [[nodiscard]] std::string LastError() const;

    [[nodiscard]] bool HasPublished() const noexcept;

    static std::unique_ptr<MediaBackend> CreateDefaultBackend(
        std::function<void(std::string_view)> trace = {});

private:
    bool CapabilityAvailable(const PlaybackSnapshot& snapshot, const TransportCommand& command) const noexcept;

    void RecordFailure(const bridge::CommandOutcome& outcome);

    std::unique_ptr<MediaService> service_;
    mutable std::mutex errorMutex_;
    std::string lastError_;
};

}
