#include "radio76/check_report.h"
#include "radio76/document_channel.h"
#include "radio76/mock_backend.h"
#include "radio76/native_runtime.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

namespace {

std::filesystem::path TestDirectory() {
    const auto path = std::filesystem::temp_directory_path() / "radio76_channel_tests";
    std::filesystem::remove_all(path);
    std::filesystem::create_directories(path);
    return path;
}

void WriteDocument(const std::filesystem::path& path, const std::string& content) {
    std::ofstream stream{path, std::ios::binary | std::ios::trunc};
    stream << content;
}

std::string ReadDocument(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

bool Contains(const std::string& text, const std::string& needle) {
    return text.find(needle) != std::string::npos;
}

std::size_t CountOccurrences(const std::string& text, const std::string& needle) {
    std::size_t count = 0;
    std::size_t position = text.find(needle);
    while (position != std::string::npos) {
        ++count;
        position = text.find(needle, position + needle.size());
    }
    return count;
}

class SharedMockBackend final : public radio76::MediaBackend {
public:
    explicit SharedMockBackend(std::shared_ptr<radio76::testing::MockMediaBackend> inner)
        : inner_(std::move(inner)) {}

    radio76::PlaybackSnapshot Poll() override {
        return inner_->Poll();
    }

    bool Execute(const radio76::TransportCommand& command) override {
        return inner_->Execute(command);
    }

    void Shutdown() noexcept override {
        inner_->Shutdown();
    }

private:
    std::shared_ptr<radio76::testing::MockMediaBackend> inner_;
};

bool WaitForLogLine(const std::filesystem::path& path, const std::string& needle) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (Contains(ReadDocument(path), needle)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return Contains(ReadDocument(path), needle);
}

bool WaitForCommandCount(radio76::testing::MockMediaBackend& backend, std::size_t expected) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (backend.CommandCount() >= expected) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return backend.CommandCount() >= expected;
}

}

int main() {
    using radio76::testing::CheckReport;
    using radio76::testing::MockMediaBackend;

    CheckReport report;
    const auto directory = TestDirectory();

    auto backend = std::make_shared<MockMediaBackend>();
    backend->SetSnapshot(MockMediaBackend::Playing("Channel Song", "Channel Artist", "Channel Album"));
    radio76::NativeRuntime runtime([backend]() {
        return std::make_unique<SharedMockBackend>(backend);
    });
    report.Check(runtime.Start(), "runtime starts with the scripted backend");

    radio76::ChannelPaths paths;
    paths.commandDocument = directory / "r76radio.command.json";
    paths.diagDocument = directory / "r76radio.diag.json";
    paths.stateDocument = directory / "r76radio.state.json";
    paths.logFile = directory / "76Radio.log";
    paths.prefsFile = directory / "Fallout76Prefs.ini";

    WriteDocument(
        paths.prefsFile,
        "[Display]\r\nfPipboyEffectColorR=0.1000\r\nfPipboyEffectColorG=0.2000\r\nfPipboyEffectColorB=0.3000\r\n"
        "\r\n[Pipboy]\r\nfPipboyEffectColorR=1.0000\r\nfPipboyEffectColorG=0.7176\r\nfPipboyEffectColorB=0.2588\r\n");

    radio76::ChannelUi ui;
    ui.showStatus = true;
    ui.showArtStatus = false;
    ui.showArtwork = false;
    ui.keepNowPlaying = true;
    radio76::DocumentChannel channel(runtime, paths, std::chrono::milliseconds(25), ui);
    report.Check(channel.Start(), "channel starts");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline && !std::filesystem::exists(paths.stateDocument)) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    report.Check(std::filesystem::exists(paths.stateDocument), "state document is published");

    const std::string state = ReadDocument(paths.stateDocument);
    report.Check(Contains(state, "\"ready\":1"), "state document reports ready");
    report.Check(Contains(state, "\"state\":\"playing\""), "state document carries the playback state");
    report.Check(Contains(state, "\"title\":\"Channel Song\""), "state document carries the track title");
    report.Check(Contains(state, "\"artist\":\"Channel Artist\""), "state document carries the artist");
    report.Check(Contains(state, "\"session\":1"), "state document reports the session");
    report.Check(Contains(state, "\"canNext\":1"), "state document reports transport capability");
    report.Check(Contains(state, "\"uiStatus\":1"), "state document carries the status line flag");
    report.Check(Contains(state, "\"uiArtStatus\":0"), "state document carries the art status flag");
    report.Check(Contains(state, "\"uiArtwork\":0"), "state document carries the artwork flag");
    report.Check(Contains(state, "\"uiKeepNowPlaying\":1"), "state document carries the persistent card flag");
    report.Check(Contains(state, "\"uiTheme\":\"FFB742\""), "state document carries the Pip-Boy color");
    report.Check(
        Contains(ReadDocument(paths.logFile), "pipboy color: FFB742"),
        "the Pip-Boy color is visible in the log");

    WriteDocument(paths.diagDocument, "{\"stamp\":100,\"command\":\"diag:probe line\"}");
    WriteDocument(paths.commandDocument, "{\"stamp\":1,\"command\":\"next\"}");
    report.Check(WaitForCommandCount(*backend, 1), "next command reaches the backend");
    report.Check(backend->Commands().size() == 1, "exactly one command arrived");
    report.Check(backend->Commands()[0].action == radio76::TransportAction::Next, "arrived command is next");

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    report.Check(backend->Commands().size() == 1, "identical command document is not executed twice");
    report.Check(WaitForLogLine(paths.logFile, "page: probe line"),
                 "a diagnostic reaches the log from its own document");
    report.Check(Contains(ReadDocument(paths.commandDocument), "next"),
                 "a diagnostic does not replace the command document");
    report.Check(Contains(ReadDocument(paths.commandDocument), "stamp"), "command document is left for the next reader");

    std::this_thread::sleep_for(std::chrono::milliseconds(5200));
    report.Check(backend->Commands().size() == 1, "an unchanged command document is not replayed later");

    WriteDocument(paths.commandDocument, "{\"stamp\":2,\"command\":\"next\"}");
    report.Check(WaitForCommandCount(*backend, 2), "the same verb with a new stamp executes again");

    WriteDocument(paths.commandDocument, "{\"stamp\":3,\"command\":\"toggle\"}");
    report.Check(WaitForCommandCount(*backend, 3), "toggle command reaches the backend");
    report.Check(backend->Commands()[2].action == radio76::TransportAction::Toggle, "arrived command is toggle");

    const auto executedBeforeMalformed = channel.CommandsExecuted();
    WriteDocument(paths.commandDocument, "not json at all");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    report.Check(channel.CommandsExecuted() == executedBeforeMalformed, "malformed document executes nothing");
    report.Check(channel.CommandsRejected() > 0, "malformed document is counted as rejected");

    WriteDocument(paths.commandDocument, "{\"stamp\":4,\"command\":\"unrecognised-verb\"}");
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    report.Check(channel.CommandsExecuted() == executedBeforeMalformed, "unknown verb executes nothing");

    WriteDocument(paths.commandDocument, std::string(4096, 'x'));
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    report.Check(channel.CommandsExecuted() == executedBeforeMalformed, "oversized document executes nothing");

    std::filesystem::remove(paths.commandDocument);
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    report.Check(channel.Running(), "channel survives a removed command document");

    WriteDocument(paths.commandDocument, "{\"stamp\":5,\"command\":\"previous\"}");
    report.Check(WaitForCommandCount(*backend, 4), "channel resumes after the document returns");
    report.Check(backend->Commands()[3].action == radio76::TransportAction::Previous, "arrived command is previous");

    WriteDocument(paths.commandDocument, "{\"stamp\":6,\"command\":\"diag:art doc 123\"}");
    const auto diagDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (std::chrono::steady_clock::now() < diagDeadline &&
           !Contains(ReadDocument(paths.logFile), "page: art doc 123")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    report.Check(backend->Commands().size() == 4, "diagnostic line is not sent to the transport");
    report.Check(Contains(ReadDocument(paths.logFile), "page: art doc 123"), "diagnostic line is written to the log");

    radio76::PlaybackSnapshot paused = MockMediaBackend::Paused("Channel Song", "Channel Artist");
    backend->SetSnapshot(paused);
    const auto pausedDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < pausedDeadline &&
           !Contains(ReadDocument(paths.stateDocument), "\"state\":\"paused\"")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    report.Check(Contains(ReadDocument(paths.stateDocument), "\"state\":\"paused\""), "state document follows the snapshot");

    const auto writesAfterPause = channel.StateWrites();
    const auto linesAfterPause = CountOccurrences(ReadDocument(paths.logFile), "state published:");
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    report.Check(channel.StateWrites() == writesAfterPause, "an unchanged snapshot is not republished");
    report.Check(
        CountOccurrences(ReadDocument(paths.logFile), "state published:") == linesAfterPause,
        "the log keeps one state line per real change");

    const auto writesBeforeSeek = channel.StateWrites();
    radio76::PlaybackSnapshot seeked = paused;
    seeked.positionMs = 155000;
    seeked.capturedAt = std::chrono::system_clock::now();
    backend->SetSnapshot(seeked);
    const auto seekDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < seekDeadline &&
           !Contains(ReadDocument(paths.stateDocument), "\"positionMs\":155000")) {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    report.Check(
        Contains(ReadDocument(paths.stateDocument), "\"positionMs\":155000"),
        "a seek republishes the new playback position");
    report.Check(channel.StateWrites() > writesBeforeSeek, "the seek counts as a state write");

    channel.Stop();
    report.Check(!channel.Running(), "channel stops");
    report.Check(channel.StateWrites() > 0, "state was written at least once");
    report.Check(std::filesystem::exists(paths.logFile), "channel wrote a log line");

    radio76::DocumentChannel direct(runtime, paths, std::chrono::milliseconds(25));
    report.Check(direct.PublishOnce(true), "a single publish writes without a worker");
    report.Check(Contains(ReadDocument(paths.stateDocument), "\"host\":\"76radio\""), "state document carries the host marker");
    direct.Stop();

    runtime.Stop();
    report.Check(!runtime.IsRunning(), "runtime stops");

    const auto emptyDirectory = std::filesystem::temp_directory_path() / "radio76_channel_tests_missing";
    std::filesystem::remove_all(emptyDirectory);
    radio76::ChannelPaths missingPaths;
    missingPaths.commandDocument = emptyDirectory / "r76radio.command.json";
    missingPaths.stateDocument = emptyDirectory / "nested" / "r76radio.state.json";
    missingPaths.logFile = emptyDirectory / "76Radio.log";
    radio76::NativeRuntime secondRuntime([]() {
        return std::unique_ptr<radio76::MediaBackend>{};
    });
    report.Check(!secondRuntime.Start(), "runtime without a backend refuses to start");
    radio76::DocumentChannel missingChannel(secondRuntime, missingPaths, std::chrono::milliseconds(25));
    report.Check(missingChannel.Start(), "channel creates missing directories");
    report.Check(missingChannel.PollOnce() == false, "polling without a command document is harmless");
    missingChannel.Stop();

    std::cout << report.Checks() << " checks, " << report.Failures() << " failures\n";
    return report.ExitCode();
}
