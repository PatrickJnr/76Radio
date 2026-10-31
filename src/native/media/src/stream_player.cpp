#include "radio76/stream_player.h"

#include "radio76/icy_metadata.h"
#include "radio76/stream_resolver.h"

#include <windows.h>
#include <mferror.h>

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace radio76 {
namespace {

using winrt::Windows::Foundation::Uri;
using winrt::Windows::Media::Core::MediaSource;
using winrt::Windows::Media::Playback::MediaPlaybackState;
using winrt::Windows::Media::Playback::MediaPlayer;
using winrt::Windows::Media::Playback::MediaPlayerAudioCategory;
using winrt::Windows::Media::Playback::MediaPlayerError;

constexpr std::array<std::chrono::seconds, 4> kRetryDelays{
    std::chrono::seconds{2},
    std::chrono::seconds{4},
    std::chrono::seconds{8},
    std::chrono::seconds{16}};
constexpr std::chrono::seconds kStartupBudget{20};
constexpr std::size_t kMaximumRetired = 4;

bool IsFormatFailure(MediaPlayerError error, winrt::hresult code) noexcept {
    if (error != MediaPlayerError::SourceNotSupported && error != MediaPlayerError::DecodingError) {
        return false;
    }
    const std::int32_t value = code.value;
    return value == MF_E_UNSUPPORTED_BYTESTREAM_TYPE || value == MF_E_INVALIDMEDIATYPE ||
        value == MF_E_TOPO_CODEC_NOT_FOUND || value == MF_E_UNSUPPORTED_FORMAT;
}

std::string ToUtf8(const winrt::hstring& text) {
    return winrt::to_string(text);
}

}

struct StreamPlayer::Impl {
    explicit Impl(std::function<void(std::string_view)> sink) : trace(std::move(sink)) {}

    void Trace(std::string_view line) const {
        if (!trace) {
            return;
        }
        try {
            trace(line);
        } catch (...) {
        }
    }

    void DisposeAttempt() noexcept {
        icy.Stop();
        if (player) {
            try {
                player.Pause();
            } catch (...) {
            }
            try {
                player.Source(nullptr);
            } catch (...) {
            }
            try {
                player.Close();
            } catch (...) {
            }
        }
        source = nullptr;
        player = nullptr;
    }

    struct ResolveJob {
        ResolveCancellation cancellation;
        std::atomic<bool> done{false};
        StreamResolveResult result;
        std::thread worker;
    };

    void BeginResolve() {
        DisposeAttempt();
        ++attemptGeneration;
        failure.store(0, std::memory_order_relaxed);
        startedAt = std::chrono::steady_clock::now();
        ReapRetired(false);
        auto next = std::make_shared<ResolveJob>();
        try {
            next->worker = std::thread([next, url = address] {
                next->result = ResolveStreamUrl(url, {}, &next->cancellation);
                next->done.store(true, std::memory_order_release);
            });
        } catch (...) {
            failure.store(1, std::memory_order_relaxed);
            return;
        }
        job = std::move(next);
    }

    void RetireJob() noexcept {
        if (!job) {
            return;
        }
        job->cancellation.Cancel();
        retired.push_back(std::move(job));
        job = nullptr;
    }

    void ReapRetired(bool all) noexcept {
        for (auto it = retired.begin(); it != retired.end();) {
            if (all || (*it)->done.load(std::memory_order_acquire) || retired.size() > kMaximumRetired) {
                if ((*it)->worker.joinable()) {
                    (*it)->worker.join();
                }
                it = retired.erase(it);
            } else {
                ++it;
            }
        }
    }

    bool PollResolve(std::chrono::steady_clock::time_point now) {
        if (!job) {
            return false;
        }
        if (!job->done.load(std::memory_order_acquire)) {
            if (now - startedAt < kStartupBudget) {
                return true;
            }
            RetireJob();
            Trace("stream address resolution timed out");
            failure.store(1, std::memory_order_relaxed);
            return false;
        }
        job->worker.join();
        const StreamResolveResult resolved = std::move(job->result);
        job = nullptr;
        ApplyResolved(resolved);
        return false;
    }

    void ApplyResolved(const StreamResolveResult& resolved) {
        if (resolved.status == StreamResolveStatus::Unsupported) {
            terminalState = PlaybackState::Unsupported;
            failure.store(2, std::memory_order_relaxed);
            return;
        }
        if (resolved.status != StreamResolveStatus::Resolved) {
            failure.store(1, std::memory_order_relaxed);
            return;
        }
        const std::uint64_t attempt = attemptGeneration.load(std::memory_order_relaxed);
        startedAt = std::chrono::steady_clock::now();
        try {
            player = MediaPlayer();
            player.CommandManager().IsEnabled(false);
            player.AudioCategory(MediaPlayerAudioCategory::Media);
            player.Volume(volume);
            player.MediaFailed([this, attempt](const MediaPlayer&, const auto& args) {
                if (attempt != attemptGeneration.load(std::memory_order_relaxed)) {
                    return;
                }
                failure.store(IsFormatFailure(args.Error(), args.ExtendedErrorCode()) ? 2 : 1, std::memory_order_relaxed);
            });
            source = MediaSource::CreateFromUri(Uri{winrt::to_hstring(resolved.playbackUrl)});
            player.Source(source);
            player.Play();
            if (!resolved.metadataUrl.empty()) {
                icy.Start(resolved.metadataUrl);
            }
            return;
        } catch (const winrt::hresult_error& error) {
            Trace("open failed: " + ToUtf8(error.message()));
        } catch (...) {
            Trace("open failed");
        }
        DisposeAttempt();
        failure.store(1, std::memory_order_relaxed);
    }

    void Reset() noexcept {
        ++attemptGeneration;
        active = false;
        terminalState = PlaybackState::Stopped;
        reconnecting = false;
        sawPlaying = false;
        retryIndex = 0;
        retryAt = {};
        address.clear();
        failure.store(0, std::memory_order_relaxed);
        RetireJob();
        DisposeAttempt();
    }

    void ScheduleRetry(std::chrono::steady_clock::time_point now) {
        DisposeAttempt();
        sawPlaying = false;
        failure.store(0, std::memory_order_relaxed);
        if (retryIndex >= kRetryDelays.size()) {
            terminalState = PlaybackState::Failed;
            Trace("stream stopped after 4 reconnect attempts");
            return;
        }
        reconnecting = true;
        retryAt = now + kRetryDelays[retryIndex];
        ++retryIndex;
        Trace("stream lost, reconnect scheduled");
    }

    std::function<void(std::string_view)> trace;
    IcyMetadataReader icy{[this](std::string_view line) { Trace(std::string{"icy: "} + std::string{line}); }};
    MediaPlayer player{nullptr};
    MediaSource source{nullptr};
    std::string address;
    double volume = 1.0;
    std::shared_ptr<ResolveJob> job;
    std::vector<std::shared_ptr<ResolveJob>> retired;
    std::atomic<std::uint64_t> attemptGeneration{0};
    std::atomic<int> failure{0};
    bool active = false;
    bool reconnecting = false;
    bool sawPlaying = false;
    std::size_t retryIndex = 0;
    PlaybackState terminalState = PlaybackState::Stopped;
    std::chrono::steady_clock::time_point startedAt{};
    std::chrono::steady_clock::time_point retryAt{};
};

StreamPlayer::StreamPlayer(std::function<void(std::string_view)> trace)
    : impl_(std::make_unique<Impl>(std::move(trace))) {}

StreamPlayer::~StreamPlayer() {
    Shutdown();
}

bool StreamPlayer::Start(std::string_view address) {
    impl_->Reset();
    if (address.empty()) {
        return false;
    }
    impl_->address.assign(address);
    impl_->active = true;
    impl_->terminalState = PlaybackState::Connecting;
    impl_->BeginResolve();
    return true;
}

void StreamPlayer::SetVolume(double volume) noexcept {
    if (!impl_) {
        return;
    }
    impl_->volume = std::clamp(volume, 0.0, 1.0);
    if (impl_->player) {
        try {
            impl_->player.Volume(impl_->volume);
        } catch (...) {
        }
    }
}

void StreamPlayer::Stop() noexcept {
    if (impl_) {
        impl_->Reset();
    }
}

void StreamPlayer::Shutdown() noexcept {
    if (impl_) {
        impl_->Reset();
        impl_->ReapRetired(true);
    }
}

bool StreamPlayer::Active() const noexcept {
    return impl_ != nullptr && impl_->active;
}

bool StreamPlayer::Failed() const noexcept {
    return impl_ != nullptr &&
        (impl_->terminalState == PlaybackState::Failed ||
         impl_->terminalState == PlaybackState::Unsupported);
}

std::string StreamPlayer::Title() const {
    return impl_ ? impl_->icy.Title() : std::string{};
}

std::string StreamPlayer::Station() const {
    return impl_ ? impl_->icy.Station() : std::string{};
}

PlaybackState StreamPlayer::State() {
    if (!impl_ || !impl_->active) {
        return PlaybackState::Stopped;
    }
    if (impl_->terminalState == PlaybackState::Failed ||
        impl_->terminalState == PlaybackState::Unsupported) {
        return impl_->terminalState;
    }

    const auto now = std::chrono::steady_clock::now();
    impl_->ReapRetired(false);
    if (impl_->PollResolve(now)) {
        return impl_->reconnecting ? PlaybackState::Reconnecting : PlaybackState::Connecting;
    }
    const int failure = impl_->failure.load(std::memory_order_relaxed);
    if (failure == 2) {
        impl_->DisposeAttempt();
        impl_->terminalState = PlaybackState::Unsupported;
        impl_->Trace("stream format is unsupported");
        return PlaybackState::Unsupported;
    }
    if (failure == 1) {
        impl_->ScheduleRetry(now);
        return impl_->terminalState == PlaybackState::Failed
            ? PlaybackState::Failed
            : PlaybackState::Reconnecting;
    }

    if (impl_->reconnecting && now < impl_->retryAt) {
        return PlaybackState::Reconnecting;
    }
    if (impl_->reconnecting && !impl_->player) {
        impl_->BeginResolve();
        return PlaybackState::Reconnecting;
    }

    try {
        if (!impl_->player || !impl_->source) {
            impl_->ScheduleRetry(now);
            return impl_->terminalState == PlaybackState::Failed
                ? PlaybackState::Failed
                : PlaybackState::Reconnecting;
        }
        const auto status = impl_->player.PlaybackSession().PlaybackState();
        switch (status) {
        case MediaPlaybackState::Playing:
            impl_->sawPlaying = true;
            impl_->reconnecting = false;
            impl_->retryIndex = 0;
            impl_->terminalState = PlaybackState::Playing;
            return PlaybackState::Playing;
        case MediaPlaybackState::Paused:
            return impl_->sawPlaying ? PlaybackState::Paused : PlaybackState::Buffering;
        case MediaPlaybackState::Opening:
            if (now - impl_->startedAt < kStartupBudget) {
                return impl_->reconnecting ? PlaybackState::Reconnecting : PlaybackState::Connecting;
            }
            break;
        case MediaPlaybackState::Buffering:
            if (now - impl_->startedAt < kStartupBudget) {
                return impl_->reconnecting ? PlaybackState::Reconnecting : PlaybackState::Buffering;
            }
            break;
        case MediaPlaybackState::None:
        default:
            if (!impl_->sawPlaying && now - impl_->startedAt < kStartupBudget) {
                return impl_->reconnecting ? PlaybackState::Reconnecting : PlaybackState::Connecting;
            }
            break;
        }
    } catch (...) {
        impl_->Trace("stream playback state failed");
    }

    impl_->ScheduleRetry(now);
    return impl_->terminalState == PlaybackState::Failed
        ? PlaybackState::Failed
        : PlaybackState::Reconnecting;
}

}
