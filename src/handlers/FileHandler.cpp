#include "FileHandler.h"
#include "Logger.h"
#include <windows.h>
#include <fstream>
#include <codecvt>
#include <locale>
#include <chrono>
#include <nlohmann/json.hpp>

std::string FileHandler::makeLongPath(const std::string& path) {
    // If path already starts with \\?\, return as-is
    if (path.find("\\\\?\\") == 0) return path;
    // If path is a long path already, return as-is
    if (path.length() > 260) return "\\\\?\\" + path;
    return path;
}

HandlerResult FileHandler::handle(const HandlerContext& ctx) {
    std::string action = ctx.cmd.value("action", "");
    if (action == "file_read") return handleRead(ctx);
    if (action == "file_write") return handleWrite(ctx);
    if (action == "file_patch") return handlePatch(ctx);
    return HandlerResult::errorResult("INVALID_REQUEST", "Unknown file action: " + action, "", 0);
}

std::string FileHandler::actionName() const {
    return "file_read/file_write/file_patch";
}

HandlerResult FileHandler::handleRead(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string path = params.value("path", "");
    if (path.empty()) return HandlerResult::errorResult("INVALID_REQUEST", "path is required", "", 0);

    // P0-2: Validate and sandbox path — reject traversal, restrict to workspace
    if (path.find("..") != std::string::npos) {
        return HandlerResult::errorResult("INVALID_REQUEST", "Path traversal not allowed: " + path, "", 0);
    }

    // Normalize and verify the path stays within workspace
    const std::string baseDir = "C:\\lobster\\hermes_bridge\\workspace\\";
    std::string fullPath = path;
    // Normalize forward slashes to backslashes
    for (size_t i = 0; i < fullPath.size(); ++i) {
        if (fullPath[i] == '/') fullPath[i] = '\\';
    }
    // If path is absolute, use it directly (already checked for .. above)
    // If relative, prepend workspace base
    if (!fullPath.empty() && fullPath[0] != '\\' && fullPath[1] != ':') {
        fullPath = baseDir + fullPath;
    }

    std::wstring wpath = std::wstring(L"\\\\?\\" + std::wstring(fullPath.begin(), fullPath.end()));

    int offset = params.value("offset", 0);
    int limit = params.value("limit", 0);
    std::string encoding = params.value("encoding", "utf-8");

    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_FILE_NOT_FOUND) {
            return HandlerResult::errorResult("FILE_NOT_FOUND", "File not found: " + path, "", 0);
        }
        return HandlerResult::errorResult("FILE_READ_FAILED", "Cannot open file: " + path,
                                          "err=" + std::to_string(err), 0);
    }

    LARGE_INTEGER fileSize;
    GetFileSizeEx(h, &fileSize);

    // P0-4: Check for out-of-bounds offset
    if (offset < 0 || static_cast<unsigned long long>(offset) >= static_cast<unsigned long long>(fileSize.QuadPart)) {
        CloseHandle(h);
        auto end = std::chrono::steady_clock::now();
        int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
        nlohmann::json res;
        res["content"] = "";
        res["size"] = 0;
        res["encoding"] = encoding;
        return HandlerResult::okResult(std::move(res), duration_ms);
    }

    if (offset > 0) {
        LARGE_INTEGER off; off.QuadPart = offset;
        SetFilePointerEx(h, off, NULL, FILE_BEGIN);
    }

    DWORD toRead = (limit > 0) ? static_cast<DWORD>(std::min<uint64_t>(limit, fileSize.QuadPart - offset))
                                : static_cast<DWORD>(fileSize.QuadPart - offset);
    std::string content;
    content.resize(toRead);
    DWORD bytesRead = 0;
    ReadFile(h, &content[0], toRead, &bytesRead, NULL);
    content.resize(bytesRead);
    CloseHandle(h);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["content"] = content;
    res["size"] = bytesRead;
    res["encoding"] = encoding;
    return HandlerResult::okResult(std::move(res), duration_ms);
}

HandlerResult FileHandler::handleWrite(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string path = params.value("path", "");
    std::string content = params.value("content", "");
    bool append = params.value("append", false);

    if (path.empty()) return HandlerResult::errorResult("INVALID_REQUEST", "path is required", "", 0);

    // P0-2: Validate and sandbox path — reject traversal, restrict to workspace
    if (path.find("..") != std::string::npos) {
        return HandlerResult::errorResult("INVALID_REQUEST", "Path traversal not allowed: " + path, "", 0);
    }

    const std::string baseDir = "C:\\lobster\\hermes_bridge\\workspace\\";
    std::string fullPath = path;
    for (size_t i = 0; i < fullPath.size(); ++i) {
        if (fullPath[i] == '/') fullPath[i] = '\\';
    }
    if (!fullPath.empty() && fullPath[0] != '\\' && fullPath[1] != ':') {
        fullPath = baseDir + fullPath;
    }

    std::wstring wpath = std::wstring(L"\\\\?\\" + std::wstring(fullPath.begin(), fullPath.end()));
    std::wstring wtmp = wpath + L".tmp";

    // Open/create tmp file
    HANDLE hTmp = CreateFileW(wtmp.c_str(), GENERIC_WRITE,
                               0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTmp == INVALID_HANDLE_VALUE) {
        return HandlerResult::errorResult("FILE_WRITE_FAILED", "Cannot create tmp file for: " + path,
                                          "err=" + std::to_string(GetLastError()), 0);
    }

    DWORD written = 0;
    WriteFile(hTmp, content.c_str(), static_cast<DWORD>(content.size()), &written, NULL);
    FlushFileBuffers(hTmp);
    CloseHandle(hTmp);

    // Atomic rename tmp -> path
    BOOL ok = MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!ok) {
        DeleteFileW(wtmp.c_str());
        return HandlerResult::errorResult("FILE_WRITE_FAILED", "Atomic rename failed for: " + path,
                                          "err=" + std::to_string(GetLastError()), 0);
    }

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["bytes_written"] = written;
    res["path"] = path;
    return HandlerResult::okResult(std::move(res), duration_ms);
}

HandlerResult FileHandler::handlePatch(const HandlerContext& ctx) {
    auto start = std::chrono::steady_clock::now();

    const auto& params = ctx.cmd.value("params", nlohmann::json::object());
    std::string path = params.value("path", "");
    std::string old_str = params.value("old_string", "");
    std::string new_str = params.value("new_string", "");
    bool replace_all = params.value("replace_all", false);

    if (path.empty() || old_str.empty()) {
        return HandlerResult::errorResult("INVALID_REQUEST", "path and old_string are required", "", 0);
    }

    // P0-2: Validate and sandbox path — reject traversal, restrict to workspace
    if (path.find("..") != std::string::npos) {
        return HandlerResult::errorResult("INVALID_REQUEST", "Path traversal not allowed: " + path, "", 0);
    }

    const std::string baseDir = "C:\\lobster\\hermes_bridge\\workspace\\";
    std::string fullPath = path;
    for (size_t i = 0; i < fullPath.size(); ++i) {
        if (fullPath[i] == '/') fullPath[i] = '\\';
    }
    if (!fullPath.empty() && fullPath[0] != '\\' && fullPath[1] != ':') {
        fullPath = baseDir + fullPath;
    }

    // Read file
    std::wstring wpath = std::wstring(L"\\\\?\\" + std::wstring(fullPath.begin(), fullPath.end()));
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return HandlerResult::errorResult("FILE_NOT_FOUND", "File not found: " + path, "", 0);
    }

    LARGE_INTEGER fsize;
    GetFileSizeEx(h, &fsize);
    std::string content(static_cast<size_t>(fsize.QuadPart), '\0');
    DWORD read = 0;
    ReadFile(h, &content[0], static_cast<DWORD>(fsize.QuadPart), &read, NULL);
    content.resize(read);
    CloseHandle(h);

    // Patch
    int replacements = 0;
    size_t pos = 0;
    if (replace_all) {
        while ((pos = content.find(old_str, pos)) != std::string::npos) {
            content.replace(pos, old_str.length(), new_str);
            ++replacements;
        }
    } else {
        pos = content.find(old_str);
        if (pos != std::string::npos) {
            content.replace(pos, old_str.length(), new_str);
            replacements = 1;
        }
    }

    // Write back using atomic write
    std::wstring wtmp = wpath + L".tmp";
    HANDLE hTmp = CreateFileW(wtmp.c_str(), GENERIC_WRITE, 0, NULL,
                               CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (hTmp == INVALID_HANDLE_VALUE) {
        return HandlerResult::errorResult("FILE_WRITE_FAILED", "Cannot create tmp for patch: " + path, "", 0);
    }
    WriteFile(hTmp, content.c_str(), static_cast<DWORD>(content.size()), &read, NULL);
    FlushFileBuffers(hTmp);
    CloseHandle(hTmp);

    MoveFileExW(wtmp.c_str(), wpath.c_str(), MOVEFILE_REPLACE_EXISTING);

    auto end = std::chrono::steady_clock::now();
    int duration_ms = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    nlohmann::json res;
    res["replacements"] = replacements;
    res["path"] = path;
    return HandlerResult::okResult(std::move(res), duration_ms);
}
