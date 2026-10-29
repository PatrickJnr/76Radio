#include <windows.h>

#include <string>

namespace {

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
T Resolve(const char* name) {
    const HMODULE module = SystemVersionModule();
    if (module == nullptr) {
        return nullptr;
    }
    return reinterpret_cast<T>(reinterpret_cast<void*>(GetProcAddress(module, name)));
}

template <typename T>
T Target(const char* name) {
    static T target = Resolve<T>(name);
    return target;
}

}

extern "C" BOOL R76GetFileVersionInfoByHandle(HANDLE file, DWORD flags, LPVOID data) {
    using Fn = BOOL(WINAPI*)(HANDLE, DWORD, LPVOID);
    const Fn target = Target<Fn>("GetFileVersionInfoByHandle");
    return target == nullptr ? FALSE : target(file, flags, data);
}

extern "C" BOOL R76GetFileVersionInfoA(LPCSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(LPCSTR, DWORD, DWORD, LPVOID);
    const Fn target = Target<Fn>("GetFileVersionInfoA");
    return target == nullptr ? FALSE : target(filename, handle, length, data);
}

extern "C" BOOL R76GetFileVersionInfoW(LPCWSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(LPCWSTR, DWORD, DWORD, LPVOID);
    const Fn target = Target<Fn>("GetFileVersionInfoW");
    return target == nullptr ? FALSE : target(filename, handle, length, data);
}

extern "C" BOOL R76GetFileVersionInfoExA(DWORD flags, LPCSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCSTR, DWORD, DWORD, LPVOID);
    const Fn target = Target<Fn>("GetFileVersionInfoExA");
    return target == nullptr ? FALSE : target(flags, filename, handle, length, data);
}

extern "C" BOOL R76GetFileVersionInfoExW(DWORD flags, LPCWSTR filename, DWORD handle, DWORD length, LPVOID data) {
    using Fn = BOOL(WINAPI*)(DWORD, LPCWSTR, DWORD, DWORD, LPVOID);
    const Fn target = Target<Fn>("GetFileVersionInfoExW");
    return target == nullptr ? FALSE : target(flags, filename, handle, length, data);
}

extern "C" DWORD R76GetFileVersionInfoSizeA(LPCSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(LPCSTR, LPDWORD);
    const Fn target = Target<Fn>("GetFileVersionInfoSizeA");
    return target == nullptr ? 0 : target(filename, handle);
}

extern "C" DWORD R76GetFileVersionInfoSizeW(LPCWSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(LPCWSTR, LPDWORD);
    const Fn target = Target<Fn>("GetFileVersionInfoSizeW");
    return target == nullptr ? 0 : target(filename, handle);
}

extern "C" DWORD R76GetFileVersionInfoSizeExA(DWORD flags, LPCSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPDWORD);
    const Fn target = Target<Fn>("GetFileVersionInfoSizeExA");
    return target == nullptr ? 0 : target(flags, filename, handle);
}

extern "C" DWORD R76GetFileVersionInfoSizeExW(DWORD flags, LPCWSTR filename, LPDWORD handle) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPDWORD);
    const Fn target = Target<Fn>("GetFileVersionInfoSizeExW");
    return target == nullptr ? 0 : target(flags, filename, handle);
}

extern "C" DWORD R76VerFindFileA(
    DWORD flags, LPCSTR fileName, LPCSTR windowsDirectory, LPCSTR appDirectory,
    LPSTR currentDirectory, PUINT currentDirectoryLength, LPSTR destinationDirectory, PUINT destinationDirectoryLength) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT, LPSTR, PUINT);
    const Fn target = Target<Fn>("VerFindFileA");
    if (target == nullptr) {
        return 0;
    }
    return target(flags, fileName, windowsDirectory, appDirectory, currentDirectory, currentDirectoryLength, destinationDirectory, destinationDirectoryLength);
}

extern "C" DWORD R76VerFindFileW(
    DWORD flags, LPCWSTR fileName, LPCWSTR windowsDirectory, LPCWSTR appDirectory,
    LPWSTR currentDirectory, PUINT currentDirectoryLength, LPWSTR destinationDirectory, PUINT destinationDirectoryLength) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT, LPWSTR, PUINT);
    const Fn target = Target<Fn>("VerFindFileW");
    if (target == nullptr) {
        return 0;
    }
    return target(flags, fileName, windowsDirectory, appDirectory, currentDirectory, currentDirectoryLength, destinationDirectory, destinationDirectoryLength);
}

extern "C" DWORD R76VerInstallFileA(
    DWORD flags, LPCSTR sourceFileName, LPCSTR destinationFileName, LPCSTR sourceDirectory, LPCSTR destinationDirectory,
    LPSTR temporaryFile, PUINT temporaryFileLength) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCSTR, LPCSTR, LPCSTR, LPCSTR, LPSTR, PUINT);
    const Fn target = Target<Fn>("VerInstallFileA");
    if (target == nullptr) {
        return 0;
    }
    return target(flags, sourceFileName, destinationFileName, sourceDirectory, destinationDirectory, temporaryFile, temporaryFileLength);
}

extern "C" DWORD R76VerInstallFileW(
    DWORD flags, LPCWSTR sourceFileName, LPCWSTR destinationFileName, LPCWSTR sourceDirectory, LPCWSTR destinationDirectory,
    LPWSTR temporaryFile, PUINT temporaryFileLength) {
    using Fn = DWORD(WINAPI*)(DWORD, LPCWSTR, LPCWSTR, LPCWSTR, LPCWSTR, LPWSTR, PUINT);
    const Fn target = Target<Fn>("VerInstallFileW");
    if (target == nullptr) {
        return 0;
    }
    return target(flags, sourceFileName, destinationFileName, sourceDirectory, destinationDirectory, temporaryFile, temporaryFileLength);
}

extern "C" DWORD R76VerLanguageNameA(DWORD language, LPSTR buffer, DWORD length) {
    using Fn = DWORD(WINAPI*)(DWORD, LPSTR, DWORD);
    const Fn target = Target<Fn>("VerLanguageNameA");
    return target == nullptr ? 0 : target(language, buffer, length);
}

extern "C" DWORD R76VerLanguageNameW(DWORD language, LPWSTR buffer, DWORD length) {
    using Fn = DWORD(WINAPI*)(DWORD, LPWSTR, DWORD);
    const Fn target = Target<Fn>("VerLanguageNameW");
    return target == nullptr ? 0 : target(language, buffer, length);
}

extern "C" BOOL R76VerQueryValueA(const LPVOID block, LPCSTR subBlock, LPVOID* buffer, PUINT length) {
    using Fn = BOOL(WINAPI*)(const LPVOID, LPCSTR, LPVOID*, PUINT);
    const Fn target = Target<Fn>("VerQueryValueA");
    return target == nullptr ? FALSE : target(block, subBlock, buffer, length);
}

extern "C" BOOL R76VerQueryValueW(const LPVOID block, LPCWSTR subBlock, LPVOID* buffer, PUINT length) {
    using Fn = BOOL(WINAPI*)(const LPVOID, LPCWSTR, LPVOID*, PUINT);
    const Fn target = Target<Fn>("VerQueryValueW");
    return target == nullptr ? FALSE : target(block, subBlock, buffer, length);
}
