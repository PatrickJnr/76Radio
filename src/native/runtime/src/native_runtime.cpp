#include "radio76/native_runtime.h"

#include "radio76/windows_media_backend.h"

#include <utility>

namespace radio76 {
namespace {

bool CapabilityFor(const PlaybackSnapshot& snapshot, TransportAction action) noexcept {
    switch (action) {
    case TransportAction::Play:
        return snapshot.canPlay;
    case TransportAction::Pause:
        return snapshot.canPause;
    case TransportAction::Toggle:
        return snapshot.canToggle;
    case TransportAction::Next:
        return snapshot.canNext;
    case TransportAction::Previous:
        return snapshot.canPrevious;
    case TransportAction::Seek:
        return snapshot.canSeek;
    case TransportAction::Stream:
    case TransportAction::StopStream:

        return true;
    }
    return false;
}

}

std::unique_ptr<MediaBackend> NativeRuntime::CreateDefaultBackend(
    std::function<void(std::string_view)> trace) {
    return CreateWindowsMediaBackend(std::move(trace));
}

NativeRuntime::NativeRuntime(BackendFactory factory, MediaService::Options options) {
    std::unique_ptr<MediaBackend> backend = factory ? factory() : CreateDefaultBackend(options.trace);
    if (backend) {
        backend->SetArtworkPath(options.artworkPath);
        backend->SetStationsPath(options.stationsPath);
        service_ = std::make_unique<MediaService>(std::move(backend), options);
    }
}

NativeRuntime::~NativeRuntime() {
    Stop();
}

bool NativeRuntime::Start() {
    if (!service_) {
        return false;
    }
    service_->Start();
    return service_->IsRunning();
}

void NativeRuntime::Stop() noexcept {
    if (service_) {
        service_->Stop();
    }
}

bool NativeRuntime::ShutdownTimedOut() const noexcept {
    return service_ != nullptr && service_->ShutdownTimedOut();
}

bool NativeRuntime::HasPublished() const noexcept {
    return service_ != nullptr && service_->HasPublished();
}

bool NativeRuntime::IsRunning() const noexcept {
    return service_ != nullptr && service_->IsRunning();
}

std::shared_ptr<const PlaybackSnapshot> NativeRuntime::Current() const noexcept {
    return service_ ? service_->Current() : nullptr;
}

void NativeRuntime::SetListener(MediaService::Listener listener) {
    if (service_) {
        service_->SetListener(std::move(listener));
    }
}

std::string NativeRuntime::StatePayload() const {
    if (!service_ || !service_->HasPublished()) {
        return bridge::BuildOfflinePayload();
    }
    const auto snapshot = Current();
    return bridge::BuildStatePayload(snapshot.get());
}

std::string NativeRuntime::LastError() const {
    std::lock_guard lock(errorMutex_);
    return lastError_;
}

void NativeRuntime::RecordFailure(const bridge::CommandOutcome& outcome) {
    std::lock_guard lock(errorMutex_);
    lastError_.clear();
    lastError_.append(bridge::OutcomeCodeName(outcome.code));
    if (!outcome.detail.empty()) {
        lastError_.append(": ");
        lastError_.append(outcome.detail);
    }
}

bool NativeRuntime::CapabilityAvailable(const PlaybackSnapshot& snapshot,
                                       const TransportCommand& command) const noexcept {
    return CapabilityFor(snapshot, command.action);
}

bridge::CommandOutcome NativeRuntime::HandleCommand(std::string_view text) {
    bridge::CommandOutcome outcome = bridge::InterpretCommand(text);
    if (!outcome.accepted()) {
        RecordFailure(outcome);
        return outcome;
    }
    if (!service_ || !service_->IsRunning()) {
        outcome.code = bridge::OutcomeCode::Unavailable;
        outcome.detail = "media service is not running";
        RecordFailure(outcome);
        return outcome;
    }
    const auto snapshot = service_->Current();
    if (service_->HasPublished() && snapshot && !CapabilityAvailable(*snapshot, outcome.command)) {
        outcome.code = bridge::OutcomeCode::Unsupported;
        outcome.detail = std::string{"session does not support "} + bridge::ActionName(outcome.command.action);
        RecordFailure(outcome);
        return outcome;
    }
    if (!service_->Post(outcome.command)) {
        outcome.code = bridge::OutcomeCode::QueueFull;
        outcome.detail = "command queue rejected the command";
        RecordFailure(outcome);
        return outcome;
    }
    return outcome;
}

}
