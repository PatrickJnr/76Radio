#include "xscal/xscal.hpp"

#include "radio76/bridge_contract.h"
#include "radio76/document_channel.h"
#include "radio76/settings.h"

#include <windows.h>
#include <shlobj.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <thread>

namespace {

constexpr std::string_view kPluginVersion = "0.3.0";
constexpr char kPluginId[] = "patrickjnr_76radio";
constexpr char kPluginName[] = "76Radio";

const XScalHost* host = nullptr;
std::atomic<radio76::NativeRuntime*> runtime{nullptr};

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

std::string Narrow(const std::filesystem::path& path) {
    const auto wide = path.wstring();
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t character : wide) {
        narrow.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
    }
    return narrow;
}

void AppendLog(
    const std::filesystem::path& logFile,
    std::size_t maxBytes,
    std::string_view line) {
    if (logFile.empty()) {
        return;
    }
    radio76::AppendLogLine(logFile, line, maxBytes);
}

std::filesystem::path LogFile(const std::filesystem::path& root) {
    return root / L"Data" / L"XSCAL" / L"76Radio" / L"76Radio.log";
}

radio76::ChannelPaths BuildPaths(
    const std::filesystem::path& root,
    const std::filesystem::path& logFile,
    std::size_t logMaxBytes) {
    const auto modsData = root / L"Data" / L"modsdata";
    const auto moduleData = modsData / L"76radio";

    radio76::ChannelPaths paths;
    paths.commandDocument = modsData / L"r76radio.command.json";
    paths.diagDocument = modsData / L"r76radio.diag.json";
    paths.stateDocument = moduleData / L"r76radio.state.json";
    paths.prefsFile = PipboyPrefsFile();
    paths.lastStationDocument = moduleData / L"r76radio.last.json";
    paths.healthDocument = moduleData / L"r76radio.health.json";
    paths.logFile = logFile;
    paths.logMaxBytes = logMaxBytes;
    return paths;
}

void WorkerBody(std::filesystem::path root, radio76::RuntimeSettings settings) {
    const auto logFile = settings.loggingEnabled ? LogFile(root) : std::filesystem::path{};
    const auto log = [&logFile, &settings](std::string_view line) {
        AppendLog(logFile, settings.logMaxBytes, line);
    };
    if (!logFile.empty()) {
        radio76::ResetLogFile(logFile);
    }
    log("76Radio " + std::string{kPluginVersion} + " starting (xScal plugin)");
    log("process " + std::to_string(GetCurrentProcessId()) + ", game " + Narrow(root));
    log(radio76::DescribeSettings(settings));

    log("waiting " + std::to_string(settings.mediaStartupDelay.count()) + " ms before the media start");
    Sleep(static_cast<DWORD>(settings.mediaStartupDelay.count()));

    const auto moduleData = root / L"Data" / L"modsdata" / L"76radio";
    radio76::MediaService::Options options;
    options.pollInterval = settings.mediaPollInterval;
    options.artworkPath = moduleData / L"r76radio.art";
    options.stationsPath = moduleData / L"stations.json";
    options.prefsPath = PipboyPrefsFile();
    if (settings.loggingEnabled) {
        options.trace = [logFile, maxBytes = settings.logMaxBytes](std::string_view line) {
            AppendLog(logFile, maxBytes, std::string{"media: "} + std::string{line});
        };
    }

    auto activeRuntime = std::make_unique<radio76::NativeRuntime>(
        radio76::NativeRuntime::BackendFactory{}, options);
    log("media runtime starting");
    if (!activeRuntime->Start()) {
        log("media service failed to start");
        return;
    }
    runtime.store(activeRuntime.get());

    const auto paths = BuildPaths(root, logFile, settings.logMaxBytes);
    radio76::ChannelUi ui;
    ui.showStatus = settings.showStatus;
    ui.showArtStatus = settings.showArtStatus;
    ui.showArtwork = settings.showArtwork;
    ui.keepNowPlaying = settings.keepNowPlaying;
    ui.cardScale = settings.cardScale;
    ui.cardOpacity = settings.cardOpacity;
    ui.hideInVats = settings.hideInVats;
    ui.hotkeys = settings.hotkeyPlayPause + "|" + settings.hotkeyNext + "|" + settings.hotkeyPrevious + "|" + settings.hotkeyToggleCard;
    ui.resumeLastStation = settings.resumeLastStation;

    auto channel = std::make_unique<radio76::DocumentChannel>(
        *activeRuntime, paths, settings.commandPollInterval, ui);
    if (!channel->Start()) {
        log("channel failed to start");
        activeRuntime->Stop();
        runtime.store(nullptr);
        return;
    }

    log("media runtime and channel running");

    for (;;) {
        Sleep(1000);
    }
}

void WorkerMain(std::filesystem::path root, radio76::RuntimeSettings settings) noexcept {
    try {
        WorkerBody(root, settings);
    } catch (...) {
        try {
            AppendLog(
                settings.loggingEnabled ? LogFile(root) : std::filesystem::path{},
                settings.logMaxBytes,
                "worker stopped after an unexpected error");
        } catch (...) {
        }
    }
}

int32_t __cdecl getVersion(
    const XScalScaleformValue*,
    uint32_t argumentCount,
    XScalScaleformValue* result) noexcept {
    if (argumentCount != 0 || result == nullptr) {
        return 0;
    }
    *result = {};
    result->type = XSCAL_SCALEFORM_VALUE_STRING;
    result->stringLength = static_cast<uint32_t>(kPluginVersion.size());
    result->value.stringValue = kPluginVersion.data();
    return 1;
}

int32_t __cdecl getState(
    const XScalScaleformValue*,
    uint32_t argumentCount,
    XScalScaleformValue* result) noexcept {
    if (argumentCount != 0 || result == nullptr) {
        return 0;
    }

    static thread_local std::string payload;
    try {
        const auto* active = runtime.load();
        payload = active != nullptr ? active->StatePayload() : radio76::bridge::BuildOfflinePayload();
        *result = {};
        result->type = XSCAL_SCALEFORM_VALUE_STRING;
        result->stringLength = static_cast<uint32_t>(payload.size());
        result->value.stringValue = payload.c_str();
        return 1;
    } catch (...) {
        return 0;
    }
}

int32_t __cdecl runCommand(
    const XScalScaleformValue* arguments,
    uint32_t argumentCount,
    XScalScaleformValue* result) noexcept {
    if (arguments == nullptr || argumentCount != 1 || result == nullptr) {
        return 0;
    }
    if (arguments[0].type != XSCAL_SCALEFORM_VALUE_STRING || arguments[0].value.stringValue == nullptr) {
        return 0;
    }

    static thread_local std::string outcomeText;
    try {
        const std::string_view command{arguments[0].value.stringValue, arguments[0].stringLength};
        auto* active = runtime.load();
        if (active == nullptr) {
            outcomeText = "unavailable: media service is not running";
        } else {
            const auto outcome = active->HandleCommand(command);
            outcomeText = radio76::bridge::OutcomeCodeName(outcome.code);
            if (!outcome.detail.empty()) {
                outcomeText += ": ";
                outcomeText += outcome.detail;
            }
        }
        *result = {};
        result->type = XSCAL_SCALEFORM_VALUE_STRING;
        result->stringLength = static_cast<uint32_t>(outcomeText.size());
        result->value.stringValue = outcomeText.c_str();
        return 1;
    } catch (...) {
        return 0;
    }
}

constexpr XScalScaleformCallback scaleformCallbacks[]{
    {"76Radio.GetState", &getState},
    {"76Radio.Command", &runCommand},
    {"76Radio.Version", &getVersion},
};

int32_t __cdecl load(const XScalHost* loadingHost) noexcept {
    if (!xscal::isHostUsable(loadingHost)) {
        return 0;
    }
    host = loadingHost;

    try {
        const auto root = ExecutableDirectory();
        if (root.empty()) {
            xscal::log(host, "could not resolve the game directory");
            return 0;
        }

        const auto settings = radio76::LoadSettings(radio76::SettingsFile(root));
        if (!settings.loggingEnabled) {
            xscal::log(host, "76Radio logging is disabled in 76Radio.ini");
        }

        std::thread(WorkerMain, root, settings).detach();
        xscal::log(host, "loaded, waiting for the media runtime");
        return 1;
    } catch (...) {
        xscal::log(host, "load failed before the media runtime started");
        return 0;
    }
}

}

extern "C" XSCAL_PLUGIN_EXPORT int32_t __cdecl XScalPlugin_Query(
    uint32_t hostAbiVersion,
    uint32_t descriptorSize,
    XScalPlugin* descriptor) {
    return xscal::describePlugin(hostAbiVersion, descriptorSize, descriptor,
        {
            .id = kPluginId,
            .name = kPluginName,
            .load = &load,
            .scaleformCallbacks = scaleformCallbacks,
        });
}
