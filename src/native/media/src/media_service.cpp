#include "radio76/media_service.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string>
#include <utility>

namespace radio76 {
namespace {

constexpr std::chrono::milliseconds kMinimumInterval{50};

}

MediaService::MediaService(std::unique_ptr<MediaBackend> backend, Options options)
    : backend_(std::move(backend)),
      options_(options) {
    if (!backend_) {
        throw std::invalid_argument("media backend is required");
    }
    if (options_.pollInterval < kMinimumInterval) {
        options_.pollInterval = kMinimumInterval;
    }
    if (options_.commandCapacity == 0) {
        options_.commandCapacity = 1;
    }
    current_.store(std::make_shared<const PlaybackSnapshot>(PlaybackSnapshot{}));
}

MediaService::~MediaService() {
    Stop();
    if (worker_.joinable()) {
        if (std::this_thread::get_id() == workerId_) {
            worker_.detach();
        } else {
            worker_.join();
        }
    }
    if (shutdownTimedOut_.load() && backend_) {

        (void)backend_.release();
    }
}

void MediaService::Start() {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true)) {
        return;
    }
    worker_ = std::thread(&MediaService::Run, this);
}

void MediaService::Stop() {
    if (std::this_thread::get_id() == workerId_) {
        running_.store(false);
        wake_.notify_all();
        return;
    }
    const bool wasRunning = running_.exchange(false);
    if (!wasRunning && !worker_.joinable()) {
        return;
    }
    wake_.notify_all();
    if (!worker_.joinable()) {
        return;
    }
    {
        std::unique_lock lock(completionMutex_);
        if (!completed_.wait_for(lock, options_.shutdownTimeout, [this] { return workerFinished_; })) {
            shutdownTimedOut_.store(true);
            worker_.detach();
            return;
        }
    }
    worker_.join();
}

bool MediaService::ShutdownTimedOut() const noexcept {
    return shutdownTimedOut_.load();
}

bool MediaService::IsRunning() const noexcept {
    return running_.load();
}

std::shared_ptr<const PlaybackSnapshot> MediaService::Current() const noexcept {
    return current_.load();
}

bool MediaService::HasPublished() const noexcept {
    return hasPublished_.load();
}

bool MediaService::Post(const TransportCommand& command) {
    if (!running_.load()) {
        return false;
    }
    {
        std::lock_guard lock(queueMutex_);
        if (queue_.size() >= options_.commandCapacity) {
            return false;
        }
        queue_.push_back(QueuedCommand{command, nullptr});
    }
    wake_.notify_all();
    return true;
}

CommandStatus MediaService::Submit(const TransportCommand& command, std::chrono::milliseconds timeout) {
    if (!running_.load()) {
        return CommandStatus::Unavailable;
    }
    if (std::this_thread::get_id() == workerId_) {
        return CommandStatus::Rejected;
    }
    auto completion = std::make_shared<std::promise<bool>>();
    auto future = completion->get_future();
    {
        std::lock_guard lock(queueMutex_);
        if (queue_.size() >= options_.commandCapacity) {
            return CommandStatus::Rejected;
        }
        queue_.push_back(QueuedCommand{command, completion});
    }
    wake_.notify_all();
    if (future.wait_for(timeout) != std::future_status::ready) {
        return CommandStatus::TimedOut;
    }
    return future.get() ? CommandStatus::Accepted : CommandStatus::Rejected;
}

void MediaService::SetListener(Listener listener) {
    std::lock_guard lock(listenerMutex_);
    listener_ = std::move(listener);
}

void MediaService::Trace(std::string_view line) const noexcept {
    if (!options_.trace) {
        return;
    }
    try {
        options_.trace(line);
    } catch (...) {
    }
}

void MediaService::Run() {
    workerId_ = std::this_thread::get_id();
    Trace("media worker started");
    auto nextPoll = std::chrono::steady_clock::now();
    while (true) {
        std::deque<QueuedCommand> batch;
        {
            std::unique_lock lock(queueMutex_);
            wake_.wait_until(lock, nextPoll, [this] {
                return !running_.load() || !queue_.empty();
            });
            if (!running_.load()) {
                break;
            }
            batch.swap(queue_);
        }
        for (auto& item : batch) {
            const bool executed = ExecuteCommand(item.command);
            if (item.completion) {
                item.completion->set_value(executed);
            }
        }
        const bool firstReading = !firstReadingDone_;
        if (firstReading) {
            Trace("first media reading starting");
        }
        const auto readingStarted = std::chrono::steady_clock::now();
        Publish(PollOnce());
        const auto readingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - readingStarted).count();
        if (firstReading) {
            firstReadingDone_ = true;
            Trace("first media reading finished after " + std::to_string(readingMs) + " ms");
        } else if (readingMs > 5000) {
            Trace("slow media reading: " + std::to_string(readingMs) + " ms");
        }
        nextPoll = std::chrono::steady_clock::now() + options_.pollInterval;
    }
    {
        std::lock_guard lock(queueMutex_);
        for (auto& item : queue_) {
            if (item.completion) {
                item.completion->set_value(false);
            }
        }
        queue_.clear();
    }
    backend_->Shutdown();
    workerId_ = std::thread::id{};
    {
        std::lock_guard lock(completionMutex_);
        workerFinished_ = true;
    }
    completed_.notify_all();
}

PlaybackSnapshot MediaService::PollOnce() {
    try {
        PlaybackSnapshot snapshot = backend_->Poll();
        snapshot.capturedAt = std::chrono::system_clock::now();
        return snapshot;
    } catch (const std::exception&) {
    } catch (...) {
    }
    const auto previous = current_.load();
    PlaybackSnapshot snapshot = previous ? *previous : PlaybackSnapshot{};
    snapshot.backendError = true;
    snapshot.sessionAvailable = false;
    snapshot.state = PlaybackState::Unavailable;
    snapshot.canPlay = false;
    snapshot.canPause = false;
    snapshot.canToggle = false;
    snapshot.canNext = false;
    snapshot.canPrevious = false;
    snapshot.canSeek = false;
    snapshot.positionMs = 0;
    snapshot.sourceAppId.clear();
    snapshot.capturedAt = std::chrono::system_clock::now();
    return snapshot;
}

bool MediaService::CommandAllowed(const PlaybackSnapshot& snapshot, const TransportCommand& command) const noexcept {
    if (command.action == TransportAction::Stream || command.action == TransportAction::StopStream) {

        return true;
    }
    if (!snapshot.sessionAvailable) {
        return false;
    }
    switch (command.action) {
    case TransportAction::Play:
        return snapshot.canPlay;
    case TransportAction::Pause:
        return snapshot.canPause;
    case TransportAction::Toggle:
        return snapshot.canToggle || snapshot.canPlay || snapshot.canPause;
    case TransportAction::Next:
        return snapshot.canNext;
    case TransportAction::Previous:
        return snapshot.canPrevious;
    case TransportAction::Seek:
        return snapshot.canSeek && command.positionMs >= 0;
    case TransportAction::Stream:
    case TransportAction::StopStream:
        return true;
    }
    return false;
}

bool MediaService::ExecuteCommand(const TransportCommand& command) {
    if (command.action == TransportAction::Stream || command.action == TransportAction::StopStream) {

        try {
            return backend_->Execute(command);
        } catch (...) {
            return false;
        }
    }
    auto snapshot = current_.load();
    if (!snapshot || !snapshot->sessionAvailable) {
        PlaybackSnapshot fresh = PollOnce();
        Publish(fresh);
        if (!fresh.sessionAvailable) {
            return false;
        }
        snapshot = current_.load();
    }
    if (!snapshot || !CommandAllowed(*snapshot, command)) {
        return false;
    }
    try {
        return backend_->Execute(command);
    } catch (...) {
        return false;
    }
}

void MediaService::Publish(PlaybackSnapshot snapshot) {
    snapshot.capturedAt = std::chrono::system_clock::now();
    const auto previous = current_.load();
    if (hasPublished_.load() && previous && SnapshotsEquivalent(*previous, snapshot)) {
        return;
    }
    auto shared = std::make_shared<const PlaybackSnapshot>(std::move(snapshot));
    current_.store(shared);
    hasPublished_.store(true);
    Listener listener;
    {
        std::lock_guard lock(listenerMutex_);
        listener = listener_;
    }
    if (listener) {
        listener(std::move(shared));
    }
}

}
