#pragma once

#include "radio76/media_backend.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <filesystem>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <string_view>
#include <thread>

namespace radio76 {

class MediaService {
public:
    struct Options {
        std::chrono::milliseconds pollInterval{900};
        std::size_t commandCapacity = 32;

        std::function<void(std::string_view)> trace;

        std::chrono::milliseconds shutdownTimeout{4000};

        std::filesystem::path artworkPath;

        std::filesystem::path stationsPath;
    };

    using Listener = std::function<void(std::shared_ptr<const PlaybackSnapshot>)>;

    explicit MediaService(std::unique_ptr<MediaBackend> backend, Options options = {});

    ~MediaService();

    MediaService(const MediaService&) = delete;

    MediaService& operator=(const MediaService&) = delete;

    void Start();

    void Stop();

    [[nodiscard]] bool ShutdownTimedOut() const noexcept;

    bool IsRunning() const noexcept;

    std::shared_ptr<const PlaybackSnapshot> Current() const noexcept;

    [[nodiscard]] bool HasPublished() const noexcept;

    bool Post(const TransportCommand& command);

    CommandStatus Submit(const TransportCommand& command, std::chrono::milliseconds timeout);

    void SetListener(Listener listener);

private:
    struct QueuedCommand {
        TransportCommand command;
        std::shared_ptr<std::promise<bool>> completion;
    };

    void Run();

    PlaybackSnapshot PollOnce();

    bool CommandAllowed(const PlaybackSnapshot& snapshot, const TransportCommand& command) const noexcept;

    bool ExecuteCommand(const TransportCommand& command);

    void Publish(PlaybackSnapshot snapshot);

    void Trace(std::string_view line) const noexcept;

    std::unique_ptr<MediaBackend> backend_;
    Options options_;
    std::atomic<bool> running_{false};
    std::thread worker_;
    std::thread::id workerId_{};
    mutable std::mutex queueMutex_;
    std::condition_variable wake_;
    std::deque<QueuedCommand> queue_;
    std::atomic<std::shared_ptr<const PlaybackSnapshot>> current_;
    std::atomic<bool> hasPublished_{false};
    std::mutex listenerMutex_;
    Listener listener_;
    std::mutex completionMutex_;
    std::condition_variable completed_;
    bool workerFinished_ = false;
    bool firstReadingDone_ = false;
    std::atomic<bool> shutdownTimedOut_{false};
};

}
