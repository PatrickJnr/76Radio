#include "radio76/check_report.h"

#include <windows.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::filesystem::path ExecutableDirectory() {
    wchar_t path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return {};
    }
    return std::filesystem::path{path}.parent_path();
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

bool WaitForFile(const std::filesystem::path& path, std::chrono::seconds timeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (std::filesystem::exists(path)) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(25));
    }
    return std::filesystem::exists(path);
}

HMODULE SystemVersionModule() {
    static HMODULE module = []() -> HMODULE {
        wchar_t directory[MAX_PATH]{};
        const UINT length = GetSystemDirectoryW(directory, MAX_PATH);
        if (length == 0 || length >= MAX_PATH) {
            return nullptr;
        }
        std::wstring path{directory};
        path += L"\\version.dll";
        return LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    }();
    return module;
}

template <typename T>
T Resolve(HMODULE module, const char* name) {
    if (module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

std::wstring SubjectPathWide() {
    wchar_t directory[MAX_PATH]{};
    GetSystemDirectoryW(directory, MAX_PATH);
    std::wstring path{directory};
    path += L"\\kernel32.dll";
    return path;
}

std::string SubjectPathNarrow() {
    const std::wstring wide = SubjectPathWide();
    std::string narrow;
    narrow.reserve(wide.size());
    for (const wchar_t character : wide) {
        narrow.push_back(character <= 0x7f ? static_cast<char>(character) : '?');
    }
    return narrow;
}

void CheckProxyParity(HMODULE module, radio76::testing::CheckReport& report) {
    const HMODULE system = SystemVersionModule();
    report.Check(system != nullptr, "system version library is reachable for comparison");
    if (system == nullptr) {
        return;
    }

    const std::wstring subject = SubjectPathWide();
    const std::string subjectNarrow = SubjectPathNarrow();

    using SizeWFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    using SizeAFn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    using SizeExWFn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    using SizeExAFn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    using InfoWFn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    using InfoAFn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    using QueryWFn = BOOL(WINAPI*)(LPCVOID, LPCWSTR, LPVOID*, PUINT);
    using QueryAFn = BOOL(WINAPI*)(LPCVOID, LPCSTR, LPVOID*, PUINT);
    using LangWFn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    using LangAFn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);

    const auto proxySizeW = Resolve<SizeWFn>(module, "GetFileVersionInfoSizeW");
    const auto systemSizeW = Resolve<SizeWFn>(system, "GetFileVersionInfoSizeW");
    const auto proxySizeA = Resolve<SizeAFn>(module, "GetFileVersionInfoSizeA");
    const auto systemSizeA = Resolve<SizeAFn>(system, "GetFileVersionInfoSizeA");
    const auto proxySizeExW = Resolve<SizeExWFn>(module, "GetFileVersionInfoSizeExW");
    const auto systemSizeExW = Resolve<SizeExWFn>(system, "GetFileVersionInfoSizeExW");
    const auto proxySizeExA = Resolve<SizeExAFn>(module, "GetFileVersionInfoSizeExA");
    const auto systemSizeExA = Resolve<SizeExAFn>(system, "GetFileVersionInfoSizeExA");
    const auto proxyInfoW = Resolve<InfoWFn>(module, "GetFileVersionInfoW");
    const auto systemInfoW = Resolve<InfoWFn>(system, "GetFileVersionInfoW");
    const auto proxyInfoA = Resolve<InfoAFn>(module, "GetFileVersionInfoA");
    const auto systemInfoA = Resolve<InfoAFn>(system, "GetFileVersionInfoA");
    const auto proxyQueryW = Resolve<QueryWFn>(module, "VerQueryValueW");
    const auto systemQueryW = Resolve<QueryWFn>(system, "VerQueryValueW");
    const auto proxyQueryA = Resolve<QueryAFn>(module, "VerQueryValueA");
    const auto systemQueryA = Resolve<QueryAFn>(system, "VerQueryValueA");
    const auto proxyLangW = Resolve<LangWFn>(module, "VerLanguageNameW");
    const auto systemLangW = Resolve<LangWFn>(system, "VerLanguageNameW");
    const auto proxyLangA = Resolve<LangAFn>(module, "VerLanguageNameA");
    const auto systemLangA = Resolve<LangAFn>(system, "VerLanguageNameA");

    DWORD proxyHandle = 0;
    DWORD systemHandle = 0;
    const DWORD proxySize = proxySizeW != nullptr ? proxySizeW(subject.c_str(), &proxyHandle) : 0;
    const DWORD systemSize = systemSizeW != nullptr ? systemSizeW(subject.c_str(), &systemHandle) : 0;
    report.Check(proxySize > 0 && proxySize == systemSize, "GetFileVersionInfoSizeW matches the system library");

    DWORD proxyHandleA = 0;
    DWORD systemHandleA = 0;
    const DWORD proxySizeAValue = proxySizeA != nullptr ? proxySizeA(subjectNarrow.c_str(), &proxyHandleA) : 0;
    const DWORD systemSizeAValue = systemSizeA != nullptr ? systemSizeA(subjectNarrow.c_str(), &systemHandleA) : 0;
    report.Check(proxySizeAValue > 0 && proxySizeAValue == systemSizeAValue, "GetFileVersionInfoSizeA matches the system library");

    DWORD proxyHandleEx = 0;
    DWORD systemHandleEx = 0;
    const DWORD proxySizeEx =
        proxySizeExW != nullptr ? proxySizeExW(0, subject.c_str(), &proxyHandleEx) : 0;
    const DWORD systemSizeEx =
        systemSizeExW != nullptr ? systemSizeExW(0, subject.c_str(), &systemHandleEx) : 0;
    report.Check(proxySizeEx > 0 && proxySizeEx == systemSizeEx, "GetFileVersionInfoSizeExW matches the system library");

    DWORD proxyHandleExA = 0;
    DWORD systemHandleExA = 0;
    const DWORD proxySizeExAValue =
        proxySizeExA != nullptr ? proxySizeExA(0, subjectNarrow.c_str(), &proxyHandleExA) : 0;
    const DWORD systemSizeExAValue =
        systemSizeExA != nullptr ? systemSizeExA(0, subjectNarrow.c_str(), &systemHandleExA) : 0;
    report.Check(proxySizeExAValue > 0 && proxySizeExAValue == systemSizeExAValue, "GetFileVersionInfoSizeExA matches the system library");

    std::vector<BYTE> proxyBlock(proxySize > 0 ? proxySize : 1, 0);
    std::vector<BYTE> systemBlock(systemSize > 0 ? systemSize : 1, 0);
    const BOOL proxyFilled = proxyInfoW != nullptr
        ? proxyInfoW(subject.c_str(), proxyHandle, proxySize, proxyBlock.data())
        : FALSE;
    const BOOL systemFilled = systemInfoW != nullptr
        ? systemInfoW(subject.c_str(), systemHandle, systemSize, systemBlock.data())
        : FALSE;
    report.Check(proxyFilled == systemFilled && proxyFilled != FALSE, "GetFileVersionInfoW reports the same result");
    report.Check(proxySize == systemSize && std::memcmp(proxyBlock.data(), systemBlock.data(), systemSize) == 0,
                 "GetFileVersionInfoW fills an identical version block");

    std::vector<BYTE> proxyBlockA(proxySizeAValue > 0 ? proxySizeAValue : 1, 0);
    std::vector<BYTE> systemBlockA(systemSizeAValue > 0 ? systemSizeAValue : 1, 0);
    const BOOL proxyFilledA = proxyInfoA != nullptr
        ? proxyInfoA(subjectNarrow.c_str(), proxyHandleA, proxySizeAValue, proxyBlockA.data())
        : FALSE;
    const BOOL systemFilledA = systemInfoA != nullptr
        ? systemInfoA(subjectNarrow.c_str(), systemHandleA, systemSizeAValue, systemBlockA.data())
        : FALSE;
    report.Check(proxyFilledA == systemFilledA && proxyFilledA != FALSE, "GetFileVersionInfoA reports the same result");
    report.Check(proxySizeAValue == systemSizeAValue &&
                     std::memcmp(proxyBlockA.data(), systemBlockA.data(), systemSizeAValue) == 0,
                 "GetFileVersionInfoA fills an identical version block");

    VS_FIXEDFILEINFO* proxyInfo = nullptr;
    VS_FIXEDFILEINFO* systemInfo = nullptr;
    UINT proxyInfoLength = 0;
    UINT systemInfoLength = 0;
    const BOOL proxyRoot = proxyQueryW != nullptr
        ? proxyQueryW(proxyBlock.data(), L"\\", reinterpret_cast<LPVOID*>(&proxyInfo), &proxyInfoLength)
        : FALSE;
    const BOOL systemRoot = systemQueryW != nullptr
        ? systemQueryW(systemBlock.data(), L"\\", reinterpret_cast<LPVOID*>(&systemInfo), &systemInfoLength)
        : FALSE;
    report.Check(proxyRoot == systemRoot && proxyRoot != FALSE && proxyInfo != nullptr && systemInfo != nullptr,
                 "VerQueryValueW answers the root block with the same shape");
    if (proxyInfo != nullptr && systemInfo != nullptr) {
        report.Check(proxyInfo->dwSignature == systemInfo->dwSignature &&
                         proxyInfo->dwFileVersionMS == systemInfo->dwFileVersionMS &&
                         proxyInfo->dwFileVersionLS == systemInfo->dwFileVersionLS,
                     "VerQueryValueW reports the same file version");
    } else {
        report.Check(false, "VerQueryValueW reports the same file version");
    }

    VS_FIXEDFILEINFO* proxyInfoA2 = nullptr;
    VS_FIXEDFILEINFO* systemInfoA2 = nullptr;
    UINT proxyInfoALength = 0;
    UINT systemInfoALength = 0;
    const BOOL proxyRootA = proxyQueryA != nullptr
        ? proxyQueryA(proxyBlockA.data(), "\\", reinterpret_cast<LPVOID*>(&proxyInfoA2), &proxyInfoALength)
        : FALSE;
    const BOOL systemRootA = systemQueryA != nullptr
        ? systemQueryA(systemBlockA.data(), "\\", reinterpret_cast<LPVOID*>(&systemInfoA2), &systemInfoALength)
        : FALSE;
    report.Check(proxyRootA == systemRootA && proxyRootA != FALSE, "VerQueryValueA answers the root block the same way");

    wchar_t proxyLanguage[64]{};
    wchar_t systemLanguage[64]{};
    const DWORD proxyLanguageLength = proxyLangW != nullptr
        ? proxyLangW(0x0409, proxyLanguage, 64)
        : 0;
    const DWORD systemLanguageLength = systemLangW != nullptr
        ? systemLangW(0x0409, systemLanguage, 64)
        : 0;
    report.Check(proxyLanguageLength == systemLanguageLength && proxyLanguageLength > 0 &&
                     std::wstring{proxyLanguage} == std::wstring{systemLanguage},
                 "VerLanguageNameW names the language the same way");

    char proxyLanguageA[64]{};
    char systemLanguageA[64]{};
    const DWORD proxyLanguageALength = proxyLangA != nullptr
        ? proxyLangA(0x0409, proxyLanguageA, 64)
        : 0;
    const DWORD systemLanguageALength = systemLangA != nullptr
        ? systemLangA(0x0409, systemLanguageA, 64)
        : 0;
    report.Check(proxyLanguageALength == systemLanguageALength && proxyLanguageALength > 0 &&
                     std::string{proxyLanguageA} == std::string{systemLanguageA},
                 "VerLanguageNameA names the language the same way");
}

}

int main(int argc, char** argv) {
    using radio76::testing::CheckReport;

    CheckReport report;
    if (argc < 2) {
        std::cout << "FAIL module path argument missing\n";
        return 1;
    }
    const std::filesystem::path modulePath{argv[1]};
    report.Check(std::filesystem::exists(modulePath), "surface module exists at the given path");

    HMODULE module = LoadLibraryW(modulePath.wstring().c_str());
    report.Check(module != nullptr, "surface module loads");

    if (module != nullptr) {
        using SizeFn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
        const auto sizeFn = reinterpret_cast<SizeFn>(reinterpret_cast<void*>(GetProcAddress(module, "GetFileVersionInfoSizeW")));
        report.Check(sizeFn != nullptr, "proxy exports the version query entry point");

        wchar_t systemPath[MAX_PATH]{};
        GetSystemDirectoryW(systemPath, MAX_PATH);
        std::wstring systemDll{systemPath};
        systemDll += L"\\kernel32.dll";
        DWORD handle = 0;
        const DWORD size = sizeFn != nullptr ? sizeFn(systemDll.c_str(), &handle) : 0;
        report.Check(size > 0, "forwarded version query returns a real size");

        CheckProxyParity(module, report);

        using QueryFn = BOOL(WINAPI*)(const LPVOID, LPCWSTR, LPVOID*, PUINT);
        const auto queryFn = reinterpret_cast<QueryFn>(reinterpret_cast<void*>(GetProcAddress(module, "VerQueryValueW")));
        report.Check(queryFn != nullptr, "proxy exports the version block query entry point");

        const auto directory = ExecutableDirectory();
        const auto modsData = directory / "Data" / "modsdata";

        const auto stateDocument = modsData / "76radio" / "r76radio.state.json";
        const auto commandDocument = modsData / "r76radio.command.json";
        const auto logFile = directory / "Data" / "XSCAL" / "76Radio" / "76Radio.log";
        std::filesystem::remove(stateDocument);

        report.Check(WaitForFile(stateDocument, std::chrono::seconds(10)), "module published a state document");
        if (std::filesystem::exists(stateDocument)) {
            const std::string state = ReadDocument(stateDocument);
            report.Check(Contains(state, "\"host\":\"76radio\""), "state document carries the host marker");
            report.Check(Contains(state, "\"ready\":1"), "state document reports ready");
            report.Check(Contains(state, "\"state\":\""), "state document carries a playback state");
        }

        if (std::filesystem::exists(logFile)) {
            const std::string log = ReadDocument(logFile);
            report.Check(Contains(log, "channel running"), "module log reports the channel running");
        } else {
            report.Check(false, "module log exists");
        }

        WriteDocument(commandDocument, "{\"stamp\":1,\"command\":\"not-a-verb\"}");
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        report.Check(!Contains(ReadDocument(logFile), "command executed: not-a-verb"),
                     "an unknown verb is never executed");
        report.Check(Contains(ReadDocument(stateDocument), "\"state\":\""),
                     "the state document stays well formed after an unknown verb");

        std::filesystem::remove(commandDocument);
        const auto unloadStart = std::chrono::steady_clock::now();
        const bool freed = FreeLibrary(module) != FALSE;
        const auto unloadMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - unloadStart);
        report.Check(freed, "surface module unloads cleanly");
        report.Check(unloadMs < std::chrono::seconds(10), "surface module unload returns within its bounded wait");
        report.Info("unload took " + std::to_string(unloadMs.count()) + " ms");
    }

    std::cout << report.Checks() << " checks, " << report.Failures() << " failures\n";
    return report.ExitCode();
}
