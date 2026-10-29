#include "radio76/log_file.h"

#include <windows.h>

#include <string>
#include <system_error>

namespace radio76 {
namespace {

unsigned long long FileSize(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA information{};
    if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &information) == FALSE) {
        return 0;
    }
    return (static_cast<unsigned long long>(information.nFileSizeHigh) << 32) |
        static_cast<unsigned long long>(information.nFileSizeLow);
}

}

std::filesystem::path LogArchivePath(const std::filesystem::path& logFile) {
    std::filesystem::path archive = logFile;
    archive += L".1";
    return archive;
}

bool ResetLogFile(const std::filesystem::path& logFile) {
    if (logFile.empty()) {
        return false;
    }
    std::error_code error;
    const auto parent = logFile.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        std::filesystem::create_directories(parent, error);
    }

    const HANDLE file = CreateFileW(
        logFile.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    CloseHandle(file);
    return true;
}

bool AppendLogLine(
    const std::filesystem::path& logFile,
    std::string_view line,
    std::size_t maxBytes) {
    if (logFile.empty()) {
        return false;
    }
    std::error_code error;
    const auto parent = logFile.parent_path();
    if (!parent.empty() && !std::filesystem::exists(parent, error)) {
        std::filesystem::create_directories(parent, error);
    }
    if (maxBytes != 0 && FileSize(logFile) + line.size() + 2 > maxBytes) {
        MoveFileExW(
            logFile.c_str(),
            LogArchivePath(logFile).c_str(),
            MOVEFILE_REPLACE_EXISTING);
    }
    const HANDLE file = CreateFileW(
        logFile.c_str(),
        FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL lineOk = WriteFile(
        file, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
    DWORD newlineWritten = 0;
    const BOOL newlineOk = WriteFile(file, "\r\n", 2, &newlineWritten, nullptr);
    CloseHandle(file);
    return lineOk != FALSE && newlineOk != FALSE && written == line.size() && newlineWritten == 2;
}

}
