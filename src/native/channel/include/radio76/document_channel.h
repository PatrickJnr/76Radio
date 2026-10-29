#pragma once

#include "radio76/log_file.h"
#include "radio76/native_runtime.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

namespace radio76 {

struct ChannelPaths {
    std::filesystem::path commandDocument;

    std::filesystem::path diagDocument;
    std::filesystem::path stateDocument;
    std::filesystem::path logFile;

    std::filesystem::path prefsFile;
    std::size_t logMaxBytes = kDefaultLogMaxBytes;
};

struct ChannelUi {
    bool showStatus = false;
    bool showArtStatus = false;
    bool showArtwork = true;

    bool keepNowPlaying = false;
};

class DocumentChannel {
public:
    DocumentChannel(
        NativeRuntime& runtime,
        ChannelPaths paths,
        std::chrono::milliseconds pollInterval = std::chrono::milliseconds(150),
        ChannelUi ui = {});

    ~DocumentChannel();

    DocumentChannel(const DocumentChannel&) = delete;

    DocumentChannel& operator=(const DocumentChannel&) = delete;

    bool Start();

    void Stop() noexcept;

    [[nodiscard]] bool Running() const noexcept;

    bool PollOnce();

    bool PublishOnce(bool force);

    [[nodiscard]] std::uint64_t CommandsExecuted() const noexcept;

    [[nodiscard]] std::uint64_t CommandsRejected() const noexcept;

    [[nodiscard]] std::uint64_t StateWrites() const noexcept;

    [[nodiscard]] std::string LastCommand() const;

private:
    static std::string DescribeState(const PlaybackSnapshot* snapshot);

    void UpdatePipboyTheme();

    void WorkerMain();

    bool ReadCommandDocument(std::string& document) const;

    bool ReadDiagDocument(std::string& document) const;

    void PollDiagDocument();

    bool WriteStateDocument(const std::string& document);

    void Log(std::string_view line) const;

    NativeRuntime& runtime_;
    ChannelPaths paths_;
    ChannelUi ui_;
    std::chrono::milliseconds pollInterval_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> stopRequested_{false};
    std::atomic<std::uint64_t> commandsExecuted_{0};
    std::atomic<std::uint64_t> commandsRejected_{0};
    std::atomic<std::uint64_t> stateWrites_{0};
    mutable std::mutex seenMutex_;
    std::uint64_t seenStamp_ = 0;
    std::string seenCommand_;
    std::uint64_t seenDiagStamp_ = 0;
    std::string seenDiagCommand_;
    mutable std::mutex publishedMutex_;

    std::string publishedSignature_;
    std::string lastStateLine_;
    std::string pipboyTheme_;
    std::filesystem::file_time_type pipboyThemeStamp_{};
    bool pipboyThemeRead_ = false;

    bool observedSnapshotValid_ = false;
    std::int64_t observedPositionMs_ = 0;
    PlaybackState observedPlaybackState_ = PlaybackState::Unavailable;
    std::chrono::system_clock::time_point observedCapturedAt_{};
};

}
