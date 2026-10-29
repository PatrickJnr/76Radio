#include "radio76/stream_player.h"

#include "radio76/icy_metadata.h"

#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Media.Core.h>
#include <winrt/Windows.Media.Playback.h>

#include <chrono>
#include <string>
#include <utility>

namespace radio76 {
namespace {

using winrt::Windows::Foundation::Uri;
using winrt::Windows::Media::Core::MediaSource;
using winrt::Windows::Media::Playback::MediaPlayer;
using winrt::Windows::Media::Playback::MediaPlaybackState;
using winrt::Windows::Media::Playback::MediaPlayerAudioCategory;

constexpr std::chrono::seconds kRetryDelay{4};
constexpr int kMaxRetries = 4;
constexpr std::chrono::seconds kStartupBudget{20};

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

    bool EnsurePlayer() {
        if (player) {
            return true;
        }
        try {
            player = MediaPlayer();

            player.CommandManager().IsEnabled(false);
            player.AudioCategory(MediaPlayerAudioCategory::Media);
        } catch (const winrt::hresult_error& error) {
            Trace("create failed: " + ToUtf8(error.message()));
            player = nullptr;
            return false;
        } catch (...) {
            player = nullptr;
            return false;
        }
        return true;
    }

    bool Open(std::string_view url) {
        if (!EnsurePlayer()) {
            return false;
        }
        try {
            source = MediaSource::CreateFromUri(Uri{winrt::to_hstring(std::string(url))});
            player.Source(source);
            player.Play();
        } catch (const winrt::hresult_error& error) {
            Trace("open failed: " + ToUtf8(error.message()));
            source = nullptr;
            return false;
        } catch (...) {
            source = nullptr;
            return false;
        }
        return true;
    }

    void Stop() noexcept {
        active = false;
        failed = false;
        sawActive = false;
        retries = 0;
        retryAt = {};
        address.clear();
        if (!player) {
            source = nullptr;
            return;
        }
        try {
            player.Pause();
        } catch (...) {
        }
        try {
            player.Source(nullptr);
        } catch (...) {
        }
        source = nullptr;
    }

    std::function<void(std::string_view)> trace;

    IcyMetadataReader icy{[this](std::string_view line) { Trace(std::string{"icy: "} + std::string{line}); }};
    MediaPlayer player{nullptr};
    MediaSource source{nullptr};
    std::string address;
    bool active = false;
    bool failed = false;
    bool sawActive = false;
    int retries = 0;
    std::chrono::steady_clock::time_point startedAt{};
    std::chrono::steady_clock::time_point retryAt{};
};

StreamPlayer::StreamPlayer(std::function<void(std::string_view)> trace)
    : impl_(std::make_unique<Impl>(std::move(trace))) {}

StreamPlayer::~StreamPlayer() {
    Shutdown();
}

bool StreamPlayer::Start(std::string_view address) {
    impl_->Stop();
    if (address.empty()) {
        return false;
    }
    impl_->address.assign(address);
    impl_->startedAt = std::chrono::steady_clock::now();
    if (!impl_->Open(address)) {
        impl_->failed = true;
        return false;
    }
    impl_->active = true;
    impl_->icy.Start(address);
    return true;
}

void StreamPlayer::Stop() noexcept {
    if (!impl_) {
        return;
    }
    impl_->icy.Stop();
    impl_->Stop();
}

void StreamPlayer::Shutdown() noexcept {
    if (!impl_) {
        return;
    }
    impl_->icy.Stop();
    impl_->Stop();
    if (impl_->player) {
        try {
            impl_->player.Close();
        } catch (...) {
        }
        impl_->player = nullptr;
    }
}

bool StreamPlayer::Active() const noexcept {
    return impl_ != nullptr && impl_->active;
}

bool StreamPlayer::Failed() const noexcept {
    return impl_ != nullptr && impl_->failed;
}

std::string StreamPlayer::Title() const {
    return impl_ != nullptr ? impl_->icy.Title() : std::string{};
}

std::string StreamPlayer::Station() const {
    return impl_ != nullptr ? impl_->icy.Station() : std::string{};
}

PlaybackState StreamPlayer::State() {
    if (!impl_ || !impl_->active || impl_->failed) {
        return PlaybackState::Stopped;
    }
    try {
        if (!impl_->player || !impl_->source) {
            return PlaybackState::Stopped;
        }
        const auto status = impl_->player.PlaybackSession().PlaybackState();
        switch (status) {
        case MediaPlaybackState::Playing:
            impl_->sawActive = true;
            impl_->retries = 0;
            return PlaybackState::Playing;
        case MediaPlaybackState::Paused:
            return PlaybackState::Paused;
        case MediaPlaybackState::Opening:
        case MediaPlaybackState::Buffering:
            impl_->sawActive = true;
            return PlaybackState::Playing;
        case MediaPlaybackState::None:
        default:
            break;
        }
    } catch (...) {
        impl_->failed = true;
        impl_->Trace("stream failed");
        return PlaybackState::Stopped;
    }

    const auto now = std::chrono::steady_clock::now();
    if (!impl_->sawActive && now - impl_->startedAt < kStartupBudget) {

        return PlaybackState::Playing;
    }
    if (impl_->retries < kMaxRetries) {
        if (impl_->retryAt.time_since_epoch().count() == 0 || now - impl_->retryAt >= kRetryDelay) {
            ++impl_->retries;
            impl_->retryAt = now;
            impl_->Trace("stream lost, reconnecting");
            try {
                impl_->player.Source(impl_->source);
                impl_->player.Play();
            } catch (...) {
            }
        }
        return PlaybackState::Playing;
    }
    impl_->failed = true;
    impl_->Trace("stream stopped after " + std::to_string(impl_->retries) + " reconnects");
    return PlaybackState::Stopped;
}

}
