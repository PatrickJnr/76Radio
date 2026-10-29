#include "radio76/check_report.h"
#include "radio76/log_file.h"
#include "radio76/settings.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

namespace {

std::filesystem::path TestDirectory() {
    const auto path = std::filesystem::temp_directory_path() / "radio76_settings_tests";
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

}

int main() {
    using radio76::testing::CheckReport;

    CheckReport report;

    const auto defaults = radio76::ParseSettings("");
    report.Check(defaults.loggingEnabled, "empty file keeps logging on");
    report.Check(defaults.mediaStartupDelay == std::chrono::milliseconds{4000}, "empty file keeps the four second start delay");
    report.Check(defaults.mediaPollInterval == std::chrono::milliseconds{900}, "empty file keeps the media poll interval");
    report.Check(defaults.commandPollInterval == std::chrono::milliseconds{150}, "empty file keeps the command poll interval");
    report.Check(!defaults.showStatus, "empty file hides the status line");
    report.Check(!defaults.showArtStatus, "empty file hides the art status line");
    report.Check(defaults.showArtwork, "empty file keeps the artwork on");
    report.Check(!defaults.keepNowPlaying, "empty file keeps the now-playing card as a timed toast");

    const auto parsed = radio76::ParseSettings(
        "; 76Radio settings\n"
        "\n"
        "[Logging]\n"
        "Enabled = 0\n"
        "\n"
        "[Media]\n"
        "StartupDelayMs=1500\n"
        "PollIntervalMs=1200\n"
        "\n"
        "[Channel]\n"
        "PollIntervalMs=250\n"
        "\n"
        "[UI]\n"
        "ShowStatus=1\n"
        "ShowArtStatus=on\n"
        "ShowArtwork=0\n"
        "KeepNowPlaying=1\n");
    report.Check(!parsed.loggingEnabled, "Enabled=0 turns logging off");
    report.Check(parsed.mediaStartupDelay == std::chrono::milliseconds{1500}, "startup delay is read");
    report.Check(parsed.mediaPollInterval == std::chrono::milliseconds{1200}, "media poll interval is read");
    report.Check(parsed.commandPollInterval == std::chrono::milliseconds{250}, "command poll interval is read");
    report.Check(parsed.showStatus, "ShowStatus=1 turns the status line on");
    report.Check(parsed.showArtStatus, "ShowArtStatus=on turns the art status line on");
    report.Check(!parsed.showArtwork, "ShowArtwork=0 turns the artwork off");
    report.Check(parsed.keepNowPlaying, "KeepNowPlaying=1 keeps the card on screen");

    const auto caseInsensitive = radio76::ParseSettings(
        "[logging]\n"
        "ENABLED=off\n"
        "[MEDIA]\n"
        "startupdelayms=0\n");
    report.Check(!caseInsensitive.loggingEnabled, "section and key names are case-insensitive");
    report.Check(caseInsensitive.mediaStartupDelay == std::chrono::milliseconds{0}, "zero start delay is honoured");

    const auto spellings = radio76::ParseSettings("[Logging]\nEnabled=no\n");
    report.Check(!spellings.loggingEnabled, "no turns logging off");
    report.Check(radio76::ParseSettings("[Logging]\nEnabled=false\n").loggingEnabled == false, "false turns logging off");
    report.Check(radio76::ParseSettings("[Logging]\nEnabled=1\n").loggingEnabled, "one keeps logging on");
    report.Check(radio76::ParseSettings("[Logging]\nEnabled=maybe\n").loggingEnabled, "an unknown word keeps the default");

    const auto comments = radio76::ParseSettings(
        "[Media]\n"
        "PollIntervalMs=1500 ; one and a half seconds\n"
        "StartupDelayMs=2000 # two seconds\n");
    report.Check(comments.mediaPollInterval == std::chrono::milliseconds{1500}, "an inline semicolon comment is ignored");
    report.Check(comments.mediaStartupDelay == std::chrono::milliseconds{2000}, "an inline hash comment is ignored");

    const auto unknown = radio76::ParseSettings(
        "[Logging]\n"
        "Enabled=0\n"
        "Verbose=1\n"
        "[Nothing]\n"
        "PollIntervalMs=50\n"
        "keyword\n");
    report.Check(!unknown.loggingEnabled, "unknown keys do not disturb known ones");
    report.Check(unknown.commandPollInterval == std::chrono::milliseconds{150}, "a property outside its section is ignored");

    const auto clamped = radio76::ParseSettings(
        "[Media]\n"
        "StartupDelayMs=900000\n"
        "PollIntervalMs=1\n"
        "[Channel]\n"
        "PollIntervalMs=9000\n");
    report.Check(clamped.mediaStartupDelay == std::chrono::milliseconds{60000}, "a huge start delay is clamped");
    report.Check(clamped.mediaPollInterval == std::chrono::milliseconds{100}, "a tiny media poll interval is clamped");
    report.Check(clamped.commandPollInterval == std::chrono::milliseconds{5000}, "a huge command poll interval is clamped");

    const auto negative = radio76::ParseSettings("[Media]\nStartupDelayMs=-4000\n");
    report.Check(negative.mediaStartupDelay == std::chrono::milliseconds{0}, "a negative start delay clamps to zero");

    const auto malformed = radio76::ParseSettings(
        "[Media]\n"
        "StartupDelayMs=soon\n"
        "PollIntervalMs=12x\n");
    report.Check(malformed.mediaStartupDelay == std::chrono::milliseconds{4000}, "an unparsable start delay keeps the default");
    report.Check(malformed.mediaPollInterval == std::chrono::milliseconds{900}, "a partly numeric poll interval keeps the default");

    const auto directory = TestDirectory();
    const auto missing = radio76::LoadSettings(directory / "absent.ini");
    report.Check(missing.loggingEnabled, "a missing file loads the defaults");

    const auto path = directory / "76Radio.ini";
    WriteDocument(path, "[Logging]\nEnabled=0\n[Channel]\nPollIntervalMs=300\n");
    const auto loaded = radio76::LoadSettings(path);
    report.Check(!loaded.loggingEnabled, "a real file is read");
    report.Check(loaded.commandPollInterval == std::chrono::milliseconds{300}, "a real file carries its values");

    WriteDocument(path, std::string(radio76::kMaximumSettingsBytes + 1, 'x'));
    const auto oversized = radio76::LoadSettings(path);
    report.Check(oversized.loggingEnabled, "an oversized file is ignored rather than parsed");

    report.Check(
        radio76::SettingsFile(L"C:\\Game") == std::filesystem::path{L"C:\\Game\\Data\\XSCAL\\76Radio\\76Radio.ini"},
        "the settings file lives beside the module log");

    const auto description = radio76::DescribeSettings(parsed);
    report.Check(description.find("logging off") != std::string::npos, "the description names the logging state");
    report.Check(description.find("startup 1500 ms") != std::string::npos, "the description names the start delay");
    report.Check(description.find("command poll 250 ms") != std::string::npos, "the description names the command poll");
    report.Check(description.find("log limit 1024 KB") != std::string::npos, "the description names the log limit");
    report.Check(description.find("ui status on") != std::string::npos, "the description names the status line");
    report.Check(description.find("ui art status on") != std::string::npos, "the description names the art status line");
    report.Check(description.find("ui artwork off") != std::string::npos, "the description names the artwork switch");
    report.Check(description.find("keep now playing on") != std::string::npos, "the description names the persistent card");

    const auto sized = radio76::ParseSettings("[Logging]\nMaxKb=64\n");
    report.Check(sized.logMaxBytes == 64u * 1024u, "MaxKb sets the rotation threshold");
    const auto unlimited = radio76::ParseSettings("[Logging]\nMaxKb=0\n");
    report.Check(unlimited.logMaxBytes == 0, "MaxKb=0 turns rotation off");
    const auto tiny = radio76::ParseSettings("[Logging]\nMaxKb=1\n");
    report.Check(tiny.logMaxBytes == 16u * 1024u, "a tiny rotation threshold is clamped");

    const auto logPath = directory / "76Radio.log";
    report.Check(radio76::AppendLogLine(logPath, "first line", 1024), "a log line is appended");
    report.Check(Contains(ReadDocument(logPath), "first line\r\n"), "the log line ends with CRLF");

    for (int index = 0; index < 40; ++index) {
        radio76::AppendLogLine(logPath, std::string(60, 'x'), 1024);
    }
    report.Check(std::filesystem::exists(radio76::LogArchivePath(logPath)), "a full log is rotated to its archive");
    report.Check(std::filesystem::file_size(logPath) <= 1024, "the rotated log starts under the limit");
    report.Check(std::filesystem::file_size(radio76::LogArchivePath(logPath)) <= 1024, "the archive stays under the limit");

    report.Check(radio76::ResetLogFile(logPath), "a reset truncates the log");
    report.Check(ReadDocument(logPath).empty(), "the reset log is empty");
    report.Check(!radio76::ResetLogFile({}), "an empty path resets nothing");

    const auto unboundedPath = directory / "unbounded.log";
    for (int index = 0; index < 40; ++index) {
        radio76::AppendLogLine(unboundedPath, std::string(60, 'y'), 0);
    }
    report.Check(!std::filesystem::exists(radio76::LogArchivePath(unboundedPath)), "a zero limit never rotates");
    report.Check(std::filesystem::file_size(unboundedPath) > 1024, "a zero limit lets the log grow");
    report.Check(!radio76::AppendLogLine({}, "nothing", 1024), "an empty path appends nothing");

    std::cout << report.Checks() << " checks, " << report.Failures() << " failures\n";
    return report.ExitCode();
}
