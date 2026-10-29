#include "radio76/bridge_contract.h"
#include "radio76/mock_backend.h"
#include "radio76/native_runtime.h"

#include <chrono>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace {

int checks = 0;
int failures = 0;

void Check(bool condition, const char* expression, int line) {
    ++checks;
    if (!condition) {
        ++failures;
        std::cout << "FAIL line " << line << ": " << expression << '\n';
    }
}

#define CHECK(expression) Check((expression), #expression, __LINE__)

using radio76::MediaService;
using radio76::NativeRuntime;
using radio76::PlaybackSnapshot;
using radio76::PlaybackState;
using radio76::TransportAction;
using radio76::bridge::BuildStatePayload;
using radio76::bridge::CommandOutcome;
using radio76::bridge::InterpretCommand;
using radio76::bridge::kMaxCommandBytes;
using radio76::bridge::kMaxCommandDocumentBytes;
using radio76::bridge::kMaxMetadataChars;
using radio76::bridge::kMaxStreamCommandBytes;
using radio76::bridge::kMaxSeekMs;
using radio76::bridge::kMaxStateBytes;
using radio76::bridge::OutcomeCode;
using radio76::testing::MockMediaBackend;

std::map<std::string, std::string> ParseFields(const std::string& payload) {
    std::map<std::string, std::string> fields;
    std::string line;
    for (const char character : payload) {
        if (character != '\n') {
            line.push_back(character);
            continue;
        }
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            fields[line.substr(0, separator)] = line.substr(separator + 1);
        }
        line.clear();
    }
    return fields;
}

int BridgeDocumentStreamFlag(const std::string& document);

bool WaitForCommandCount(const MockMediaBackend& backend,
                         std::size_t expected,
                         std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.CommandCount() >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

bool WaitFor(const NativeRuntime& runtime,
             const std::function<bool(const PlaybackSnapshot&)>& predicate,
             std::chrono::milliseconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = runtime.Current();
        if (snapshot && predicate(*snapshot)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

void TestCommandOutcomes() {
    CHECK(InterpretCommand("toggle").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("play").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("pause").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("next").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("previous").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("  next  ").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("").code == OutcomeCode::Empty);
    CHECK(InterpretCommand("     ").code == OutcomeCode::Empty);
    CHECK(InterpretCommand("banana").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("PLAY").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("seek").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("seek:").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("seek:-5").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("seek:12x").code == OutcomeCode::Malformed);

    const CommandOutcome toggle = InterpretCommand("toggle");
    CHECK(toggle.command.action == TransportAction::Toggle);
    CHECK(toggle.detail == "toggle");

    const CommandOutcome seek = InterpretCommand("seek:42000");
    CHECK(seek.code == OutcomeCode::Accepted);
    CHECK(seek.command.action == TransportAction::Seek);
    CHECK(seek.command.positionMs == 42000);
}

void TestStreamCommands() {
    const std::string address = "https://ice-sov.musicradio.com/ClassicFMMP3";
    const CommandOutcome opening = InterpretCommand("stream|" + address);
    CHECK(opening.code == OutcomeCode::Accepted);
    CHECK(opening.command.action == TransportAction::Stream);
    CHECK(opening.command.streamUrl == address);
    CHECK(opening.detail == "stream");

    CHECK(InterpretCommand("  stream|" + address + "  ").command.streamUrl == address);
    CHECK(InterpretCommand("stream").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("stream|").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("stream|   ").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("stream|ftp://example.com/radio").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("stream|https://").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("stream|https://example.com/a b").code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("stream|" + std::string(kMaxStreamCommandBytes, 'a')).code == OutcomeCode::TooLong);

    const std::string longAddress = "https://example.com/" + std::string(400, 'a');
    const std::string document = "{\"stamp\":1700000000000,\"command\":\"stream|" + longAddress + "\"}";
    CHECK(document.size() < kMaxCommandDocumentBytes);
    radio76::bridge::CommandDocument parsed;
    CHECK(radio76::bridge::ParseCommandDocument(document, parsed));
    CHECK(parsed.command == "stream|" + longAddress);

    CHECK(InterpretCommand("streamstop").code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("streamstop").command.action == TransportAction::StopStream);
}

void TestLimits() {
    const std::string tooLong(kMaxCommandBytes + 1, 'x');
    CHECK(InterpretCommand(tooLong).code == OutcomeCode::TooLong);

    std::string padded = "toggle";
    padded.append(kMaxCommandBytes - padded.size(), ' ');
    CHECK(InterpretCommand(padded).code == OutcomeCode::Accepted);

    CHECK(InterpretCommand("seek:" + std::to_string(kMaxSeekMs)).code == OutcomeCode::Accepted);
    CHECK(InterpretCommand("seek:" + std::to_string(kMaxSeekMs + 1)).code == OutcomeCode::Malformed);
    CHECK(InterpretCommand("seek:99999999999999999999").code == OutcomeCode::Malformed);

    CHECK(std::string(radio76::bridge::OutcomeCodeName(OutcomeCode::QueueFull)) == "queue_full");
    CHECK(std::string(radio76::bridge::OutcomeCodeName(OutcomeCode::Unsupported)) == "unsupported");
    CHECK(std::string(radio76::bridge::ActionName(TransportAction::Previous)) == "previous");
}

void TestMetadataClamping() {
    const std::string longText(600, 'a');
    CHECK(radio76::bridge::ClampMetadata(longText).size() == kMaxMetadataChars);
    CHECK(radio76::bridge::ClampMetadata("short") == "short");
    CHECK(radio76::bridge::ClampMetadata(std::string(10, '\xE2')).size() == 10);
}

void TestMockDefaults() {
    MockMediaBackend defaulted;
    const auto snapshot = defaulted.Poll();
    CHECK(snapshot.sessionAvailable);
    CHECK(snapshot.spotifyRunning);
    CHECK(snapshot.state == PlaybackState::Playing);
    CHECK(snapshot.canSeek);
    CHECK(snapshot.metadata.title == "Mock Track");
}

void TestStatePayload() {
    const auto playing = MockMediaBackend::Playing("Song Title", "Artist Name", "Album Name");
    const std::string payload = BuildStatePayload(&playing);
    const auto fields = ParseFields(payload);
    CHECK(fields.at("ready") == "1");
    CHECK(fields.at("state") == "playing");
    CHECK(fields.at("spotify") == "1");
    CHECK(fields.at("session") == "1");
    CHECK(fields.at("title") == "Song Title");
    CHECK(fields.at("artist") == "Artist Name");
    CHECK(fields.at("canSeek") == "1");
    CHECK(fields.at("source") == "Spotify.exe");

    PlaybackSnapshot multiline = MockMediaBackend::Playing("Line\nBreak", "Artist", "Album");
    const auto multilineFields = ParseFields(BuildStatePayload(&multiline));
    CHECK(multilineFields.at("title") == "Line\\nBreak");

    const std::string offline = BuildStatePayload(nullptr);
    const auto offlineFields = ParseFields(offline);
    CHECK(offlineFields.at("ready") == "0");
    CHECK(offlineFields.at("state") == "unavailable");
    CHECK(offlineFields.at("session") == "0");

    PlaybackSnapshot oversized = MockMediaBackend::Playing(std::string(4000, 't'), std::string(4000, 'a'), std::string(4000, 'b'));
    const auto clamped = BuildStatePayload(&oversized);
    CHECK(clamped.size() <= kMaxStateBytes);
    const auto clampedFields = ParseFields(clamped);
    CHECK(clampedFields.at("title").size() == kMaxMetadataChars);

    PlaybackSnapshot unicode = MockMediaBackend::Playing("Vintern ar har", "Sanger om Sverige", "Nordisk");
    const auto unicodeFields = ParseFields(BuildStatePayload(&unicode));
    CHECK(unicodeFields.at("title") == "Vintern ar har");
    CHECK(unicodeFields.at("artist") == "Sanger om Sverige");

    PlaybackSnapshot station = MockMediaBackend::Playing("Classic FM", "Internet Stream", "Live");
    station.streamActive = true;
    station.streamStation = "Classic FM";
    station.canNext = false;
    station.durationMs = 0;
    const auto stationFields = ParseFields(BuildStatePayload(&station));
    CHECK(stationFields.at("stream") == "1");
    CHECK(stationFields.at("canNext") == "0");
    const std::string stationDocument = radio76::bridge::BuildStateDocument(&station);
    CHECK(stationDocument.find("\"stream\":1") != std::string::npos);

    CHECK(stationDocument.find("\"station\":\"Classic FM\"") != std::string::npos);
    CHECK(radio76::bridge::BuildStateDocument(&playing).find("\"station\":\"\"") != std::string::npos);
    const auto spotifyFields = ParseFields(BuildStatePayload(&playing));
    CHECK(spotifyFields.at("stream") == "0");
    CHECK(BridgeDocumentStreamFlag(radio76::bridge::BuildStateDocument(&playing)) == 0);
}

int BridgeDocumentStreamFlag(const std::string& document) {
    const auto key = document.find("\"stream\":");
    if (key == std::string::npos) {
        return -1;
    }
    return document[key + 9] == '1' ? 1 : 0;
}

void TestRuntimeCommands() {
    auto backend = std::make_unique<MockMediaBackend>();
    MockMediaBackend* raw = backend.get();
    raw->SetSnapshot(MockMediaBackend::Playing("First Song", "First Artist", "First Album"));
    NativeRuntime runtime([&]() { return std::move(backend); },
                          MediaService::Options{std::chrono::milliseconds(10), 8});
    CHECK(runtime.Start());
    CHECK(WaitFor(runtime, [](const PlaybackSnapshot& snapshot) { return snapshot.sessionAvailable; },
                  std::chrono::milliseconds(1000)));

    CHECK(runtime.HandleCommand("toggle").accepted());
    CHECK(runtime.HandleCommand("pause").accepted());
    CHECK(runtime.HandleCommand("next").accepted());
    CHECK(runtime.HandleCommand("previous").accepted());
    CHECK(runtime.HandleCommand("seek:1000").accepted());
    CHECK(WaitForCommandCount(*raw, 5, std::chrono::milliseconds(1000)));

    const auto commands = raw->Commands();
    CHECK(commands.size() == 5);
    CHECK(commands[0].action == TransportAction::Toggle);
    CHECK(commands[4].action == TransportAction::Seek);
    CHECK(commands[4].positionMs == 1000);

    const auto fields = ParseFields(runtime.StatePayload());
    CHECK(fields.at("ready") == "1");
    CHECK(fields.at("title") == "First Song");

    raw->SetSnapshot(MockMediaBackend::Paused("Second Song", "Second Artist"));
    CHECK(WaitFor(runtime, [](const PlaybackSnapshot& snapshot) { return snapshot.state == PlaybackState::Paused; },
                  std::chrono::milliseconds(1000)));
    const auto pausedFields = ParseFields(runtime.StatePayload());
    CHECK(pausedFields.at("state") == "paused");
    CHECK(pausedFields.at("title") == "Second Song");

    raw->SetSnapshot(MockMediaBackend::Offline());
    CHECK(WaitFor(runtime, [](const PlaybackSnapshot& snapshot) { return !snapshot.spotifyRunning; },
                  std::chrono::milliseconds(1000)));
    const auto offlineFields = ParseFields(runtime.StatePayload());
    CHECK(offlineFields.at("spotify") == "0");
    CHECK(offlineFields.at("session") == "0");
}

void TestBootstrapState() {
    auto backend = std::make_unique<MockMediaBackend>();
    MockMediaBackend* raw = backend.get();
    raw->SetPollDelay(std::chrono::milliseconds(150));
    NativeRuntime runtime([&]() { return std::move(backend); },
                          MediaService::Options{std::chrono::milliseconds(10), 8});
    CHECK(runtime.Start());
    const auto beforePoll = ParseFields(runtime.StatePayload());
    CHECK(beforePoll.at("ready") == "0");
    CHECK(beforePoll.at("state") == "unavailable");
    CHECK(runtime.HandleCommand("toggle").accepted());
    CHECK(WaitFor(runtime, [](const PlaybackSnapshot& snapshot) { return snapshot.sessionAvailable; },
                  std::chrono::milliseconds(2000)));
    const auto afterPoll = ParseFields(runtime.StatePayload());
    CHECK(afterPoll.at("ready") == "1");
    CHECK(afterPoll.at("state") == "playing");
    CHECK(WaitForCommandCount(*raw, 1, std::chrono::milliseconds(2000)));
    runtime.Stop();
}

void TestCapabilityGating() {
    auto backend = std::make_unique<MockMediaBackend>();
    MockMediaBackend* raw = backend.get();
    PlaybackSnapshot limited = MockMediaBackend::Playing("Song", "Artist", "Album");
    limited.canSeek = false;
    limited.canNext = false;
    raw->SetSnapshot(limited);
    NativeRuntime runtime([&]() { return std::move(backend); },
                          MediaService::Options{std::chrono::milliseconds(10), 8});
    CHECK(runtime.Start());
    CHECK(WaitFor(runtime, [](const PlaybackSnapshot& snapshot) { return snapshot.sessionAvailable; },
                  std::chrono::milliseconds(1000)));

    const auto seek = runtime.HandleCommand("seek:1000");
    CHECK(seek.code == OutcomeCode::Unsupported);
    const auto next = runtime.HandleCommand("next");
    CHECK(next.code == OutcomeCode::Unsupported);
    CHECK(runtime.HandleCommand("pause").accepted());
    CHECK(WaitForCommandCount(*raw, 1, std::chrono::milliseconds(1000)));
    CHECK(runtime.LastError().find("unsupported") == 0);
}

void TestQueueRejection() {
    auto backend = std::make_unique<MockMediaBackend>();
    MockMediaBackend* raw = backend.get();
    raw->SetSnapshot(MockMediaBackend::Playing("Song", "Artist", "Album"));
    raw->SetExecuteDelay(std::chrono::milliseconds(100));
    NativeRuntime runtime([&]() { return std::move(backend); },
                          MediaService::Options{std::chrono::milliseconds(10), 1});
    CHECK(runtime.Start());
    CHECK(WaitFor(runtime, [](const PlaybackSnapshot& snapshot) { return snapshot.sessionAvailable; },
                  std::chrono::milliseconds(1000)));

    int rejected = 0;
    for (int index = 0; index < 16; ++index) {
        if (runtime.HandleCommand("toggle").code == OutcomeCode::QueueFull) {
            ++rejected;
        }
    }
    CHECK(rejected >= 10);
    CHECK(runtime.IsRunning());
    CHECK(runtime.LastError().find("queue_full") == 0);
}

void TestRuntimeLifecycle() {
    auto backend = std::make_unique<MockMediaBackend>();
    backend->SetSnapshot(MockMediaBackend::SpotifyIdle());
    NativeRuntime runtime([&]() { return std::move(backend); },
                          MediaService::Options{std::chrono::milliseconds(10), 8});
    CHECK(runtime.Start());
    CHECK(runtime.IsRunning());
    runtime.Stop();
    CHECK(!runtime.IsRunning());
    CHECK(runtime.HandleCommand("toggle").code == OutcomeCode::Unavailable);
    runtime.Stop();
    CHECK(runtime.Start());
    CHECK(runtime.IsRunning());
    runtime.Stop();
    CHECK(!runtime.IsRunning());
}

void TestRuntimeWithoutBackend() {
    NativeRuntime runtime([]() { return std::unique_ptr<radio76::MediaBackend>{}; });
    CHECK(!runtime.Start());
    CHECK(!runtime.IsRunning());
    CHECK(runtime.HandleCommand("toggle").code == OutcomeCode::Unavailable);
    const auto fields = ParseFields(runtime.StatePayload());
    CHECK(fields.at("ready") == "0");
}

}

int main() {
    std::cout << "radio76 bridge contract tests\n";
    TestCommandOutcomes();
    TestStreamCommands();
    TestLimits();
    TestMetadataClamping();
    TestMockDefaults();
    TestStatePayload();
    TestRuntimeCommands();
    TestBootstrapState();
    TestCapabilityGating();
    TestQueueRejection();
    TestRuntimeLifecycle();
    TestRuntimeWithoutBackend();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
