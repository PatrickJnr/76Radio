#include "radio76/document_channel.h"
#include "radio76/settings.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace {

std::atomic<bool> stopRequested{false};
std::atomic<bool> loggingEnabled{true};
std::atomic<std::size_t> logMaxBytes{radio76::kDefaultLogMaxBytes};
HANDLE startEvent{};
HMODULE selfModule{};
HANDLE workerHandle{};
std::unique_ptr<radio76::NativeRuntime> runtime;
std::unique_ptr<radio76::DocumentChannel> channel;

std::filesystem::path ExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path{path}.parent_path();
}

std::filesystem::path PipboyPrefsFile() {
    wchar_t documents[MAX_PATH]{};
    if (FAILED(SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents))) {
        return {};
    }

    return std::filesystem::path{documents} / L"My Games" / L"Fallout 76" / L"Fallout76Prefs.ini";
}

std::filesystem::path LogFile() {
    const auto root = ExecutableDirectory();
    if (root.empty()) {
        return {};
    }
    return root / L"Data" / L"XSCAL" / L"76Radio" / L"76Radio.log";
}

std::string Narrow(const std::filesystem::path& path) {
    const auto wide = path.wstring();
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t character : wide) {
        narrow.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
    }
    return narrow;
}

void AppendLog(const std::filesystem::path& logFile, std::string_view line) {
    if (logFile.empty() || !loggingEnabled.load()) {
        return;
    }
    radio76::AppendLogLine(logFile, line, logMaxBytes.load());
}

DWORD WINAPI WorkerMain(LPVOID) {
    WaitForSingleObject(startEvent, 5000);
    const auto root = ExecutableDirectory();
    if (root.empty()) {
        return 1;
    }
    const auto modsData = root / L"Data" / L"modsdata";

    const auto moduleData = modsData / L"76radio";

    const radio76::RuntimeSettings settings = radio76::LoadSettings(radio76::SettingsFile(root));
    loggingEnabled.store(settings.loggingEnabled);
    logMaxBytes.store(settings.logMaxBytes);
    const auto logFile = settings.loggingEnabled ? LogFile() : std::filesystem::path{};

    if (!logFile.empty()) {
        radio76::ResetLogFile(logFile);
    }
    AppendLog(logFile, "76Radio surface module starting");
    AppendLog(logFile, "process " + std::to_string(GetCurrentProcessId()) + ", game " + Narrow(root));
    if (selfModule != nullptr) {
        wchar_t modulePath[MAX_PATH]{};
        if (GetModuleFileNameW(selfModule, modulePath, MAX_PATH) != 0) {
            AppendLog(logFile, "module " + Narrow(std::filesystem::path{modulePath}));
        }
    }
    AppendLog(logFile, radio76::DescribeSettings(settings));
    radio76::ChannelPaths paths;
    paths.commandDocument = modsData / L"r76radio.command.json";

    paths.diagDocument = modsData / L"r76radio.diag.json";
    paths.stateDocument = moduleData / L"r76radio.state.json";
    paths.prefsFile = PipboyPrefsFile();
    paths.logFile = logFile;
    paths.logMaxBytes = settings.logMaxBytes;

    const DWORD startupDelay = static_cast<DWORD>(settings.mediaStartupDelay.count());
    AppendLog(logFile, "waiting " + std::to_string(startupDelay) + " ms before the media start");
    Sleep(startupDelay);
    radio76::MediaService::Options options;
    options.pollInterval = settings.mediaPollInterval;
    options.artworkPath = moduleData / L"r76radio.art";

    options.stationsPath = moduleData / L"stations.json";
    if (settings.loggingEnabled) {
        options.trace = [logFile](std::string_view line) {
            AppendLog(logFile, std::string{"media: "} + std::string{line});
        };
    }
    runtime = std::make_unique<radio76::NativeRuntime>(radio76::NativeRuntime::BackendFactory{}, options);
    AppendLog(logFile, "media runtime starting");
    if (!runtime->Start()) {
        AppendLog(logFile, "media service failed to start");
        return 1;
    }
    AppendLog(logFile, "media runtime started");
    AppendLog(logFile, "pipboy prefs " + Narrow(paths.prefsFile));
    radio76::ChannelUi ui;
    ui.showStatus = settings.showStatus;
    ui.showArtStatus = settings.showArtStatus;
    ui.showArtwork = settings.showArtwork;
    ui.keepNowPlaying = settings.keepNowPlaying;
    channel = std::make_unique<radio76::DocumentChannel>(*runtime, paths, settings.commandPollInterval, ui);
    if (!channel->Start()) {
        AppendLog(logFile, "channel failed to start");
        runtime->Stop();
        return 1;
    }
    AppendLog(logFile, "channel running");
    while (!stopRequested.load()) {
        Sleep(100);
    }
    channel->Stop();
    runtime->Stop();
    if (runtime->ShutdownTimedOut()) {

        AppendLog(logFile, "media stop timed out, runtime left to the process exit");
        (void)runtime.release();
    } else {
        AppendLog(logFile, "runtime stopped");
        runtime.reset();
    }
    channel.reset();
    AppendLog(logFile, "surface module stopped");
    return 0;
}

}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        selfModule = module;
        startEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        workerHandle = CreateThread(nullptr, 0, &WorkerMain, nullptr, 0, nullptr);
        if (startEvent != nullptr) {
            SetEvent(startEvent);
        }
    } else if (reason == DLL_PROCESS_DETACH) {
        stopRequested.store(true);

        if (reserved == nullptr && workerHandle != nullptr) {
            if (WaitForSingleObject(workerHandle, 3000) == WAIT_TIMEOUT) {
                AppendLog(LogFile(), "worker still busy when the module was unloaded");
            }
        }
        if (startEvent != nullptr) {
            CloseHandle(startEvent);
            startEvent = nullptr;
        }
        if (workerHandle != nullptr) {
            CloseHandle(workerHandle);
            workerHandle = nullptr;
        }
    }
    return TRUE;
}
