#include "radio76/game_audio.h"
#include "radio76/icy_metadata.h"
#include "radio76/icy_metadata.h"
#include "radio76/media_service.h"
#include "radio76/media_types.h"
#include "radio76/scaleform_payload.h"
#include "radio76/stream_catalog.h"
#include "radio76/stream_resolver.h"

#include <atomic>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
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

using radio76::CommandStatus;
using radio76::MediaBackend;
using radio76::MediaService;
using radio76::PlaybackSnapshot;
using radio76::PlaybackState;
using radio76::TrackMetadata;
using radio76::TransportAction;
using radio76::TransportCommand;

PlaybackSnapshot BaseSnapshot() {
    PlaybackSnapshot snapshot;
    snapshot.spotifyRunning = true;
    snapshot.sessionAvailable = true;
    snapshot.state = PlaybackState::Playing;
    snapshot.metadata = radio76::NormalizeMetadata("Hoist The Colours", "The Wellermen", "Sea Shanties");
    snapshot.canPlay = true;
    snapshot.canPause = true;
    snapshot.canToggle = true;
    snapshot.canNext = true;
    snapshot.canPrevious = true;
    snapshot.canSeek = true;
    snapshot.durationMs = 210000;
    snapshot.positionMs = 42000;
    snapshot.sourceAppId = "Spotify.exe";
    return snapshot;
}

void BlockForever() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
}

class FakeBackend final : public MediaBackend {
public:
    PlaybackSnapshot Poll() override {
        bool hang = false;
        PlaybackSnapshot result;
        {
            std::lock_guard lock(mutex_);
            ++pollCount_;
            if (failNextPoll_) {
                failNextPoll_ = false;
                throw std::runtime_error("simulated backend failure");
            }
            result = snapshot_;

            hang = hangAfterPolls_ > 0 && pollsBeforeHang_ >= hangAfterPolls_;
            ++pollsBeforeHang_;
        }
        if (hang) {
            hangStarted_.store(true);
            BlockForever();
        }
        return result;
    }

    bool Execute(const TransportCommand& command) override {
        std::lock_guard lock(mutex_);
        executed_.push_back(command);
        switch (command.action) {
        case TransportAction::Play:
            snapshot_.state = PlaybackState::Playing;
            break;
        case TransportAction::Pause:
            snapshot_.state = PlaybackState::Paused;
            break;
        case TransportAction::Toggle:
            snapshot_.state = snapshot_.state == PlaybackState::Playing
                ? PlaybackState::Paused
                : PlaybackState::Playing;
            break;
        case TransportAction::Next:
        case TransportAction::Previous:
            snapshot_.metadata.title = command.action == TransportAction::Next ? "Next Track" : "Previous Track";
            break;
        case TransportAction::Seek:
            snapshot_.positionMs = command.positionMs;
            break;
        case TransportAction::Stream:
            snapshot_.streamActive = true;
            snapshot_.state = PlaybackState::Playing;
            snapshot_.metadata = radio76::NormalizeMetadata(command.streamUrl, "Internet Stream", "Live");
            break;
        case TransportAction::StopStream:
            snapshot_.streamActive = false;
            snapshot_.state = PlaybackState::Stopped;
            break;
        }
        return executeResult_;
    }

    void Shutdown() noexcept override {
        bool hang = false;
        {
            std::lock_guard lock(mutex_);
            shutdownCalled_ = true;
            hang = hangOnShutdown_;
        }
        if (hang) {
            hangStarted_.store(true);
            BlockForever();
        }
    }

    void HangOnShutdown() {
        std::lock_guard lock(mutex_);
        hangOnShutdown_ = true;
    }

    void HangAfterPolls(int polls) {
        std::lock_guard lock(mutex_);
        hangAfterPolls_ = polls;
        pollsBeforeHang_ = 0;
    }

    bool HangStarted() const {
        return hangStarted_.load();
    }

    void SetSnapshot(PlaybackSnapshot snapshot) {
        std::lock_guard lock(mutex_);
        snapshot_ = std::move(snapshot);
    }

    PlaybackSnapshot Snapshot() const {
        std::lock_guard lock(mutex_);
        return snapshot_;
    }

    void SetCanNext(bool value) {
        std::lock_guard lock(mutex_);
        snapshot_.canNext = value;
    }

    void SetExecuteResult(bool value) {
        std::lock_guard lock(mutex_);
        executeResult_ = value;
    }

    void FailNextPoll() {
        std::lock_guard lock(mutex_);
        failNextPoll_ = true;
    }

    std::vector<TransportCommand> Executed() const {
        std::lock_guard lock(mutex_);
        return executed_;
    }

    int PollCount() const {
        std::lock_guard lock(mutex_);
        return pollCount_;
    }

    bool ShutdownCalled() const {
        std::lock_guard lock(mutex_);
        return shutdownCalled_;
    }

private:
    mutable std::mutex mutex_;
    PlaybackSnapshot snapshot_ = BaseSnapshot();
    std::vector<TransportCommand> executed_;
    int pollCount_ = 0;
    bool executeResult_ = true;
    bool failNextPoll_ = false;
    bool shutdownCalled_ = false;
    bool hangOnShutdown_ = false;
    int hangAfterPolls_ = 0;
    int pollsBeforeHang_ = 0;
    std::atomic<bool> hangStarted_{false};
};

bool WaitUntil(const MediaService& service, const std::function<bool(const PlaybackSnapshot&)>& predicate, int timeoutMs = 3000) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto snapshot = service.Current();
        if (snapshot && predicate(*snapshot)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return false;
}

MediaService::Options FastOptions() {
    MediaService::Options options;
    options.pollInterval = std::chrono::milliseconds(50);
    options.commandCapacity = 32;
    return options;
}

void TestDiscoveryAndMetadata() {
    auto backend = std::make_unique<FakeBackend>();
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable && snapshot.metadata.present;
    }));
    const auto snapshot = service.Current();
    CHECK(snapshot->spotifyRunning);
    CHECK(snapshot->state == PlaybackState::Playing);
    CHECK(snapshot->metadata.title == "Hoist The Colours");
    CHECK(snapshot->metadata.artist == "The Wellermen");
    CHECK(snapshot->metadata.album == "Sea Shanties");
    CHECK(snapshot->sourceAppId == "Spotify.exe");
    CHECK(snapshot->durationMs == 210000);
    CHECK(snapshot->positionMs == 42000);
    CHECK(snapshot->canNext);
    CHECK(!snapshot->backendError);
    service.Stop();
    CHECK(!service.IsRunning());
}

void TestSpotifyNotRunning() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    PlaybackSnapshot offline;
    offline.spotifyRunning = false;
    offline.sessionAvailable = false;
    raw->SetSnapshot(offline);
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.capturedAt.time_since_epoch().count() != 0;
    }));
    const auto snapshot = service.Current();
    CHECK(!snapshot->spotifyRunning);
    CHECK(!snapshot->sessionAvailable);
    CHECK(snapshot->state == PlaybackState::Unavailable);
    CHECK(!snapshot->canPlay);
    service.Stop();
}

void TestTransportCommands() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable;
    }));

    const auto play = service.Submit(TransportCommand{TransportAction::Play, 0}, std::chrono::milliseconds(2000));
    CHECK(play == CommandStatus::Accepted);
    const auto pause = service.Submit(TransportCommand{TransportAction::Pause, 0}, std::chrono::milliseconds(2000));
    CHECK(pause == CommandStatus::Accepted);
    const auto toggle = service.Submit(TransportCommand{TransportAction::Toggle, 0}, std::chrono::milliseconds(2000));
    CHECK(toggle == CommandStatus::Accepted);
    const auto next = service.Submit(TransportCommand{TransportAction::Next, 0}, std::chrono::milliseconds(2000));
    CHECK(next == CommandStatus::Accepted);
    const auto previous = service.Submit(TransportCommand{TransportAction::Previous, 0}, std::chrono::milliseconds(2000));
    CHECK(previous == CommandStatus::Accepted);
    const auto seek = service.Submit(TransportCommand{TransportAction::Seek, 30000}, std::chrono::milliseconds(2000));
    CHECK(seek == CommandStatus::Accepted);

    const auto executed = raw->Executed();
    CHECK(executed.size() == 6);
    if (executed.size() == 6) {
        CHECK(executed[0].action == TransportAction::Play);
        CHECK(executed[1].action == TransportAction::Pause);
        CHECK(executed[2].action == TransportAction::Toggle);
        CHECK(executed[3].action == TransportAction::Next);
        CHECK(executed[4].action == TransportAction::Previous);
        CHECK(executed[5].action == TransportAction::Seek);
        CHECK(executed[5].positionMs == 30000);
    }

    raw->SetCanNext(false);
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return !snapshot.canNext;
    }));
    const auto rejected = service.Submit(TransportCommand{TransportAction::Next, 0}, std::chrono::milliseconds(2000));
    CHECK(rejected == CommandStatus::Rejected);
    CHECK(raw->Executed().size() == 6);
    service.Stop();
}

void TestSelectMediaWithoutSession() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    PlaybackSnapshot idle;
    idle.sessionAvailable = false;
    idle.state = PlaybackState::Unavailable;
    raw->SetSnapshot(idle);
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    TransportCommand select;
    select.action = TransportAction::SelectMedia;
    select.mediaSource = "auto";
    CHECK(service.Submit(select, std::chrono::milliseconds(2000)) == CommandStatus::Accepted);
    CHECK(raw->Executed().size() == 1);
    CHECK(service.Submit(TransportCommand{TransportAction::Toggle, 0}, std::chrono::milliseconds(2000)) == CommandStatus::Rejected);
    CHECK(raw->Executed().size() == 1);
    service.Stop();
}

void TestSessionLossAndReconnect() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable;
    }));

    PlaybackSnapshot lost;
    lost.spotifyRunning = false;
    lost.sessionAvailable = false;
    raw->SetSnapshot(lost);
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return !snapshot.sessionAvailable && !snapshot.spotifyRunning;
    }));

    PlaybackSnapshot restored = BaseSnapshot();
    restored.metadata = radio76::NormalizeMetadata("New Song", "New Artist", "New Album");
    raw->SetSnapshot(restored);
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable && snapshot.metadata.title == "New Song";
    }));
    CHECK(service.Current()->metadata.artist == "New Artist");
    service.Stop();
}

void TestBackendErrorRecovery() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable;
    }));
    raw->FailNextPoll();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.backendError;
    }));
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return !snapshot.backendError && snapshot.sessionAvailable;
    }));
    CHECK(service.IsRunning());
    service.Stop();
}

void TestShutdown() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    MediaService service(std::move(backend), FastOptions());
    std::atomic<int> notifications{0};
    service.SetListener([&notifications](std::shared_ptr<const PlaybackSnapshot>) {
        ++notifications;
    });
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable;
    }));
    service.Stop();
    CHECK(raw->ShutdownCalled());
    CHECK(!service.IsRunning());
    const int settled = notifications.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(120));
    CHECK(notifications.load() == settled);
    const auto unavailable = service.Submit(TransportCommand{TransportAction::Toggle, 0}, std::chrono::milliseconds(100));
    CHECK(unavailable == CommandStatus::Unavailable);
    CHECK(!service.Post(TransportCommand{TransportAction::Toggle, 0}));
}

void TestShutdownSurvivesBlockingBackend() {
    MediaService::Options options = FastOptions();
    options.shutdownTimeout = std::chrono::milliseconds(300);

    {
        auto backend = std::make_unique<FakeBackend>();
        FakeBackend* raw = backend.get();
        raw->HangOnShutdown();

        auto* service = new MediaService(std::move(backend), options);
        service->Start();
        CHECK(WaitUntil(*service, [](const PlaybackSnapshot& snapshot) {
            return snapshot.sessionAvailable;
        }));
        const auto started = std::chrono::steady_clock::now();
        service->Stop();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        CHECK(service->ShutdownTimedOut());
        CHECK(!service->IsRunning());
        CHECK(elapsed >= std::chrono::milliseconds(250));
        CHECK(elapsed < std::chrono::milliseconds(2500));
    }

    {
        auto backend = std::make_unique<FakeBackend>();
        FakeBackend* raw = backend.get();
        raw->HangAfterPolls(1);
        auto* service = new MediaService(std::move(backend), options);
        service->Start();
        CHECK(WaitUntil(*service, [](const PlaybackSnapshot& snapshot) {
            return snapshot.sessionAvailable;
        }));
        const auto waitDeadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
        while (!raw->HangStarted() && std::chrono::steady_clock::now() < waitDeadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        CHECK(raw->HangStarted());
        const auto started = std::chrono::steady_clock::now();
        service->Stop();
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started);
        CHECK(service->ShutdownTimedOut());
        CHECK(!service->IsRunning());
        CHECK(elapsed < std::chrono::milliseconds(2500));
    }

    MediaService::Options defaults;
    CHECK(defaults.shutdownTimeout == std::chrono::milliseconds(4000));
}

void TestListenerDeduplication() {
    auto backend = std::make_unique<FakeBackend>();
    MediaService service(std::move(backend), FastOptions());
    std::atomic<int> notifications{0};
    service.SetListener([&notifications](std::shared_ptr<const PlaybackSnapshot>) {
        ++notifications;
    });
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable;
    }));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    CHECK(notifications.load() == 1);
    service.Stop();
}

void TestPollCadence() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    MediaService::Options options;
    options.pollInterval = std::chrono::milliseconds(80);
    MediaService service(std::move(backend), options);
    service.Start();
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    const int polls = raw->PollCount();
    CHECK(polls >= 3);
    CHECK(polls <= 12);
    service.Stop();
}

void TestThreadSafety() {
    auto backend = std::make_unique<FakeBackend>();
    FakeBackend* raw = backend.get();
    MediaService service(std::move(backend), FastOptions());
    service.Start();
    CHECK(WaitUntil(service, [](const PlaybackSnapshot& snapshot) {
        return snapshot.sessionAvailable;
    }));

    std::atomic<bool> stopReaders{false};
    std::vector<std::thread> readers;
    for (int index = 0; index < 4; ++index) {
        readers.emplace_back([&service, &stopReaders] {
            while (!stopReaders.load()) {
                const auto snapshot = service.Current();
                if (snapshot && snapshot->metadata.present) {
                    (void)snapshot->metadata.title.size();
                }
            }
        });
    }

    int accepted = 0;
    for (int index = 0; index < 40; ++index) {
        if (service.Submit(TransportCommand{TransportAction::Toggle, 0}, std::chrono::milliseconds(1000)) ==
            CommandStatus::Accepted) {
            ++accepted;
        }
    }
    stopReaders.store(true);
    for (auto& reader : readers) {
        reader.join();
    }
    CHECK(accepted == 40);
    CHECK(raw->Executed().size() == 40);
    service.Stop();
}

void TestSubmitFromListener() {
    auto backend = std::make_unique<FakeBackend>();
    MediaService service(std::move(backend), FastOptions());
    std::atomic<int> listenerStatus{-1};
    service.SetListener([&service, &listenerStatus](std::shared_ptr<const PlaybackSnapshot>) {
        const auto status = service.Submit(TransportCommand{TransportAction::Toggle, 0}, std::chrono::milliseconds(50));
        listenerStatus.store(static_cast<int>(status));
    });
    service.Start();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (listenerStatus.load() == -1 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    CHECK(listenerStatus.load() == static_cast<int>(CommandStatus::Rejected));
    service.Stop();
}

void TestMetadataNormalization() {
    const auto cleaned = radio76::NormalizeMetadata("  A\nB\t ", " Artist  ", " Album ");
    CHECK(cleaned.title == "A B");
    CHECK(cleaned.artist == "Artist");
    CHECK(cleaned.album == "Album");
    CHECK(cleaned.present);

    const auto empty = radio76::NormalizeMetadata(std::string("\0\0", 2), "", "");
    CHECK(!empty.present);
    CHECK(empty.title.empty());

    const auto longTitle = radio76::NormalizeMetadata(std::string(1000, 'a'), "", "");
    CHECK(longTitle.title.size() == 256);

    const std::string unicodeTail = std::string(255, 'a') + "\xC3\xA9";
    const auto truncated = radio76::NormalizeMetadata(unicodeTail, "", "");
    CHECK(truncated.title.size() == 255);
    CHECK(truncated.title.back() == 'a');

    const auto kept = radio76::NormalizeMetadata(std::string(254, 'a') + "\xC3\xA9", "", "");
    CHECK(kept.title.size() == 256);
}

std::map<std::string, std::string> ParsePayload(const std::string& payload) {
    std::map<std::string, std::string> fields;
    std::size_t start = 0;
    while (start < payload.size()) {
        const auto end = payload.find('\n', start);
        const auto line = payload.substr(start, end == std::string::npos ? std::string::npos : end - start);
        const auto separator = line.find('=');
        if (separator != std::string::npos) {
            fields[line.substr(0, separator)] = radio76::UnescapePayloadValue(line.substr(separator + 1));
        }
        if (end == std::string::npos) {
            break;
        }
        start = end + 1;
    }
    return fields;
}

void TestPayloadRoundTrip() {
    PlaybackSnapshot snapshot = BaseSnapshot();
    snapshot.metadata = radio76::NormalizeMetadata("Line\\Break", "Artist\nName", "Album");
    snapshot.backendError = false;
    const std::string payload = radio76::SnapshotToPayload(snapshot);
    std::size_t lines = 0;
    for (const char character : payload) {
        if (character == '\n') {
            ++lines;
        }
    }
    CHECK(lines == 22);
    const auto fields = ParsePayload(payload);
    CHECK(fields.at("stream") == "0");
    CHECK(fields.at("state") == "playing");
    CHECK(fields.at("session") == "1");
    CHECK(fields.at("spotify") == "1");
    CHECK(fields.at("title") == "Line\\Break");
    CHECK(fields.at("artist") == "Artist Name");
    CHECK(fields.at("canNext") == "1");
    CHECK(fields.at("durationMs") == "210000");
    CHECK(fields.at("source") == "Spotify.exe");

    CHECK(radio76::EscapePayloadValue("a\\b\nc\rd") == "a\\\\b\\nc\\rd");
    CHECK(radio76::UnescapePayloadValue("a\\\\b\\nc\\rd") == "a\\b\nc\rd");
}

void TestCommandParsing() {
    const auto toggle = radio76::ParseCommandPayload(" toggle ");
    CHECK(toggle.has_value());
    CHECK(toggle->action == TransportAction::Toggle);

    const auto seek = radio76::ParseCommandPayload("seek:12345");
    CHECK(seek.has_value());
    CHECK(seek->action == TransportAction::Seek);
    CHECK(seek->positionMs == 12345);

    const auto spotifyMedia = radio76::ParseCommandPayload("media|spotify");
    CHECK(spotifyMedia.has_value() && spotifyMedia->action == radio76::TransportAction::SelectMedia);
    CHECK(spotifyMedia.has_value() && spotifyMedia->toggleAfterSelect);
    const auto autoMedia = radio76::ParseCommandPayload("media|auto");
    CHECK(autoMedia.has_value() && autoMedia->mediaSource == "auto" && autoMedia->toggleAfterSelect);
    const auto nextMedia = radio76::ParseCommandPayload("media|next");
    CHECK(nextMedia.has_value() && nextMedia->mediaSource == "next" && !nextMedia->toggleAfterSelect);
    CHECK(!radio76::ParseCommandPayload("media|chrome").has_value());
    CHECK(!radio76::ParseCommandPayload("").has_value());
    CHECK(!radio76::ParseCommandPayload("seek:").has_value());
    CHECK(!radio76::ParseCommandPayload("seek:-1").has_value());
    CHECK(!radio76::ParseCommandPayload("seek:12x").has_value());
    CHECK(!radio76::ParseCommandPayload("PLAY").has_value());
    CHECK(!radio76::ParseCommandPayload("seek").has_value());
    CHECK(!radio76::ParseCommandPayload("unknown").has_value());

    CHECK(!radio76::ParseCommandPayload("stream").has_value());
    const auto stop = radio76::ParseCommandPayload("streamstop");
    CHECK(stop.has_value());
    if (stop.has_value()) {
        CHECK(stop->action == TransportAction::StopStream);
    }
    CHECK(std::string(radio76::TransportActionName(TransportAction::Stream)) == "stream");
    CHECK(std::string(radio76::TransportActionName(TransportAction::StopStream)) == "streamstop");
}

void TestStreamCatalog() {
    const std::string document = R"({
        "stations": [
            { "name": "Classic FM", "streamurl": "https://ice-sov.musicradio.com/ClassicFMMP3" },
            { "streamurl": "https://example.com/live", "name": "Example Radio" },
            { "name": "Broken" }
        ]
    })";
    const auto stations = radio76::ParseStationsDocument(document);
    CHECK(stations.size() == 2);
    if (stations.size() == 2) {
        CHECK(stations[0].name == "Classic FM");
        CHECK(stations[0].url == "https://ice-sov.musicradio.com/ClassicFMMP3");
        CHECK(stations[1].name == "Example Radio");
    }
    CHECK(radio76::StationNameForUrl(stations, "https://ice-sov.musicradio.com/ClassicFMMP3") == "Classic FM");
    CHECK(radio76::StationNameForUrl(stations, "HTTPS://ICE-SOV.MUSICRADIO.COM/CLASSICFMMP3") == "Classic FM");
    CHECK(radio76::StationNameForUrl(stations, "https://unknown.example/stream").empty());

    CHECK(radio76::StreamTitleFromUrl("https://www.example.com/live.mp3") == "example.com");
    CHECK(radio76::StreamTitleFromUrl("http://stream.example.org:8000/radio") == "stream.example.org");
    CHECK(radio76::StreamTitleFromUrl("") == "Internet stream");

    CHECK(radio76::ParseStationsDocument(R"({"probe":1,"build":"0.3.0"})").empty());
    CHECK(radio76::ParseStationsDocument("not json").empty());

    const auto escaped = radio76::ParseStationsDocument(
        R"({"stations":[{"name":"Quote \" Radio \\ \u00e9","streamurl":"https:\/\/example.com\/live"},{"name":"Duplicate","streamurl":"HTTPS://EXAMPLE.COM/LIVE"},{"name":"Local","streamurl":"file:///radio"},{"name":12,"streamurl":"https://example.com/bad"}]})");
    CHECK(escaped.size() == 1);
    if (escaped.size() == 1) {
        CHECK(escaped[0].name == "Quote \" Radio \\ \xC3\xA9");
        CHECK(escaped[0].url == "https://example.com/live");
    }
    CHECK(radio76::ParseStationsDocument(R"({"stations":[]})").empty());
    CHECK(radio76::ParseStationsDocument(R"({"stations":[)").empty());
}

void WriteCatalogFile(const std::filesystem::path& path, const std::string& text, int minutes) {
    {
        std::ofstream file{path, std::ios::binary | std::ios::trunc};
        file << text;
    }
    std::filesystem::last_write_time(
        path, std::filesystem::file_time_type::clock::now() + std::chrono::minutes(minutes));
}

void TestStationCatalogReload() {
    const auto directory = std::filesystem::temp_directory_path() / "radio76_catalog_test";
    std::filesystem::create_directories(directory);
    const auto path = directory / "stations.json";
    const std::string alpha = "https://example.com/alpha";
    const std::string beta = "https://example.com/beta";

    radio76::StationCatalog catalog{path};
    CHECK(catalog.ConfiguredTitleFor(alpha).empty());
    CHECK(catalog.TitleFor(alpha) == "example.com");

    WriteCatalogFile(path, R"({"stations":[{"name":"Alpha","streamurl":"https://example.com/alpha"}]})", 1);
    CHECK(catalog.ConfiguredTitleFor(alpha) == "Alpha");
    CHECK(catalog.ConfiguredTitleFor(beta).empty());

    WriteCatalogFile(path, R"({"stations":[{"name":"Alpha Renamed","streamurl":"https://example.com/alpha"},{"name":"Beta","streamurl":"https://example.com/beta"}]})", 2);
    CHECK(catalog.ConfiguredTitleFor(alpha) == "Alpha Renamed");
    CHECK(catalog.ConfiguredTitleFor(beta) == "Beta");

    WriteCatalogFile(path, R"({"stations":[{"name":"Alpha Renamed","streamurl":"https://example.com/al)", 3);
    CHECK(catalog.ConfiguredTitleFor(alpha) == "Alpha Renamed");
    CHECK(catalog.ConfiguredTitleFor(beta) == "Beta");

    WriteCatalogFile(path, "", 4);
    CHECK(catalog.ConfiguredTitleFor(beta) == "Beta");

    WriteCatalogFile(path, R"({"stations":[{"name":"Beta","streamurl":"https://example.com/beta"}]})", 5);
    CHECK(catalog.ConfiguredTitleFor(alpha).empty());
    CHECK(catalog.TitleFor(alpha) == "example.com");
    CHECK(catalog.ConfiguredTitleFor(beta) == "Beta");

    std::filesystem::remove_all(directory);
}

void TestPipboyRadioVolume() {
    const auto directory = std::filesystem::temp_directory_path() / "radio76_volume_test";
    std::filesystem::create_directories(directory);
    const auto path = directory / "Fallout76Prefs.ini";
    const std::string real =
        "[Display]\nfVal6=0.1\n\n[AudioMenu]\nfVal7=1.0000\nuID7=0\nfVal6=0.2041\nuID6=138006\nfVal5=0.7500\n"
        "uID5=1007612\nfVal4=0.4082\nuID4=94881\nfAudioMasterVolume=1.0000\n\n[Survival]\nfVal6=0.9\n";
    WriteCatalogFile(path, real, 0);
    auto volume = radio76::ReadPipboyRadioVolume(path);
    CHECK(volume.has_value() && std::abs(*volume - 0.2041) < 0.0001);

    WriteCatalogFile(path, "[AudioMenu]\r\nuID2 = 138006\r\nfVal2 = 0.5\r\nfAudioMasterVolume=0.5\r\n", 1);
    volume = radio76::ReadPipboyRadioVolume(path);
    CHECK(volume.has_value() && std::abs(*volume - 0.25) < 0.0001);

    WriteCatalogFile(path, "[AudioMenu]\nuID1=138006\nfVal1=7\nfAudioMasterVolume=3\n", 2);
    volume = radio76::ReadPipboyRadioVolume(path);
    CHECK(volume.has_value() && std::abs(*volume - 1.0) < 0.0001);

    WriteCatalogFile(path, "[AudioMenu]\nuID1=138006\nfVal1=0\n", 3);
    volume = radio76::ReadPipboyRadioVolume(path);
    CHECK(volume.has_value() && *volume == 0.0);

    WriteCatalogFile(path, "[AudioMenu]\nuID1=5\nfVal1=0.3\n", 4);
    CHECK(!radio76::ReadPipboyRadioVolume(path).has_value());
    WriteCatalogFile(path, "[AudioMenu]\nuID1=138006\nfVal1=abc\n", 5);
    CHECK(!radio76::ReadPipboyRadioVolume(path).has_value());
    WriteCatalogFile(path, "[Other]\nuID1=138006\nfVal1=0.3\n", 6);
    CHECK(!radio76::ReadPipboyRadioVolume(path).has_value());
    WriteCatalogFile(path, "", 7);
    CHECK(!radio76::ReadPipboyRadioVolume(path).has_value());
    CHECK(!radio76::ReadPipboyRadioVolume(directory / "missing.ini").has_value());
    std::filesystem::remove_all(directory);
}

void TestIcyMetadata() {
    CHECK(
        radio76::ParseIcyStreamTitle("StreamTitle='Edvard Grieg - Peer Gynt';StreamUrl='';") ==
        "Edvard Grieg - Peer Gynt");

    CHECK(radio76::ParseIcyStreamTitle("StreamTitle='Rose's Raiders - Song';") == "Rose's Raiders - Song");
    CHECK(radio76::ParseIcyStreamTitle("StreamTitle=' Makyo - Be Where Now ';") == "Makyo - Be Where Now");
    CHECK(radio76::ParseIcyStreamTitle("StreamTitle='';").empty());
    CHECK(radio76::ParseIcyStreamTitle("sTrEaMtItLe=\"Björk - Jóga\";") == "Björk - Jóga");
    CHECK(radio76::ParseIcyStreamTitle(std::string("STREAMTITLE='Caf\xE9 - D\xE9j\xE0 Vu';")) == "Caf\xC3\xA9 - D\xC3\xA9j\xC3\xA0 Vu");
    CHECK(radio76::ParseIcyStreamTitle("no title here").empty());
    CHECK(radio76::ParseIcyStreamTitle("StreamTitle='unterminated").empty());
    CHECK(radio76::ParseIcyStreamTitle(std::string("StreamTitle='Song';") + std::string(24, '\0')) == "Song");
    CHECK(radio76::ParseIcyStationName("  Classic FM \r\n") == "Classic FM");
    CHECK(radio76::ParseIcyStationName("").empty());
}

void TestPlaylists() {
    CHECK(radio76::DetectPlaylistFormat("https://example.com/list.m3u", "", "") == radio76::PlaylistFormat::M3u);
    CHECK(radio76::DetectPlaylistFormat("https://example.com/radio", "audio/x-scpls", "") == radio76::PlaylistFormat::Pls);
    CHECK(radio76::DetectPlaylistFormat("https://example.com/live.m3u8", "", "#EXTM3U\n#EXT-X-TARGETDURATION:6") == radio76::PlaylistFormat::Hls);
    CHECK(radio76::DetectPlaylistFormat("https://example.com/live.mp3", "audio/mpeg", "") == radio76::PlaylistFormat::None);

    const auto m3u = radio76::ParsePlaylist(
        "#EXTM3U\n/live.mp3\nrelative.aac\nhttps://other.example/radio\n/live.mp3\n",
        radio76::PlaylistFormat::M3u,
        "https://example.com/lists/stations.m3u");
    CHECK(m3u.size() == 3);
    if (m3u.size() == 3) {
        CHECK(m3u[0] == "https://example.com/live.mp3");
        CHECK(m3u[1] == "https://example.com/lists/relative.aac");
        CHECK(m3u[2] == "https://other.example/radio");
    }

    const auto pls = radio76::ParsePlaylist(
        "[playlist]\nNumberOfEntries=2\nFile1=https://example.com/one\nTitle1=One\nFile2=/two\n",
        radio76::PlaylistFormat::Pls,
        "https://radio.example/list.pls");
    CHECK(pls.size() == 2);
    if (pls.size() == 2) {
        CHECK(pls[0] == "https://example.com/one");
        CHECK(pls[1] == "https://radio.example/two");
    }
    CHECK(radio76::ParsePlaylist("#EXTM3U\n", radio76::PlaylistFormat::M3u, "https://example.com/list").empty());
}

void TestStateNames() {
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Playing)) == "playing");
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Paused)) == "paused");
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Connecting)) == "connecting");
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Buffering)) == "buffering");
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Reconnecting)) == "reconnecting");
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Failed)) == "failed");
    CHECK(std::string(radio76::PlaybackStateName(PlaybackState::Unsupported)) == "unsupported");
    CHECK(std::string(radio76::TransportActionName(TransportAction::Previous)) == "previous");
    TransportAction action = TransportAction::Seek;
    CHECK(radio76::ParseTransportAction("next", action));
    CHECK(action == TransportAction::Next);
}

}

int main() {
    std::cout << "radio76 media tests\n";
    TestDiscoveryAndMetadata();
    TestSpotifyNotRunning();
    TestTransportCommands();
    TestSelectMediaWithoutSession();
    TestSessionLossAndReconnect();
    TestBackendErrorRecovery();
    TestShutdown();
    TestShutdownSurvivesBlockingBackend();
    TestListenerDeduplication();
    TestPollCadence();
    TestThreadSafety();
    TestSubmitFromListener();
    TestMetadataNormalization();
    TestPayloadRoundTrip();
    TestCommandParsing();
    TestStreamCatalog();
    TestStationCatalogReload();
    TestPipboyRadioVolume();
    TestIcyMetadata();
    TestPlaylists();
    TestStateNames();
    std::cout << checks << " checks, " << failures << " failures\n";
    return failures == 0 ? 0 : 1;
}
