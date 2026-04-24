#include "CallbackWriter.h"
#include "Logger.h"
#include <windows.h>
#include <string>

CallbackWriter::CallbackWriter(const std::string& work_dir)
    : work_dir_(work_dir), using_imdisk_(false) {}

CallbackWriter::~CallbackWriter() {}

bool CallbackWriter::init() {
    // Try ImDisk path first
    if (GetFileAttributesA("R:\\callbacks") != INVALID_FILE_ATTRIBUTES) {
        base_path_ = "R:\\callbacks\\";
        using_imdisk_ = true;
        LOG_INFO("[httpserver] Using ImDisk: R:\\callbacks");
    } else {
        base_path_ = work_dir_ + "\\callbacks\\";
        using_imdisk_ = false;
        LOG_WARN("[httpserver] ImDisk not mounted, falling back to local callbacks/");
    }
    return ensureDirectory();
}

bool CallbackWriter::ensureDirectory() {
    if (!CreateDirectoryA(base_path_.c_str(), NULL)) {
        DWORD err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) {
            LOG_ERROR("[httpserver] Failed to create callbacks directory: " + base_path_ + ", error=" + std::to_string(err));
            return false;
        }
    }
    return true;
}

std::string CallbackWriter::getCallbacksDir() const {
    return base_path_;
}

std::string CallbackWriter::generateFilename(const std::string& client) const {
    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);
    unsigned __int64 ts = (static_cast<unsigned __int64>(ft.dwHighDateTime) << 32) | ft.dwLowDateTime;
    ts /= 10000; // milliseconds

    std::string safe_client = client;
    for (char& c : safe_client) {
        if (c == '\\' || c == '/' || c == ':' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|') {
            c = '_';
        }
    }
    if (safe_client.empty()) safe_client = "unknown";

    return std::to_string(ts) + "_" + safe_client + ".json";
}

bool CallbackWriter::atomicWrite(const std::string& path, const std::string& content) {
    HANDLE h = CreateFileA(path.c_str(), GENERIC_WRITE, 0, NULL,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) {
        LOG_ERROR("[httpserver] CreateFile failed: " + path + ", error=" + std::to_string(GetLastError()));
        return false;
    }

    DWORD written = 0;
    if (!WriteFile(h, content.data(), static_cast<DWORD>(content.size()), &written, NULL)) {
        CloseHandle(h);
        LOG_ERROR("[httpserver] WriteFile failed: " + path + ", error=" + std::to_string(GetLastError()));
        return false;
    }

    if (!FlushFileBuffers(h)) {
        CloseHandle(h);
        LOG_ERROR("[httpserver] FlushFileBuffers failed: " + path + ", error=" + std::to_string(GetLastError()));
        return false;
    }
    CloseHandle(h);
    return true;
}

bool CallbackWriter::writeCallback(const std::string& json, const std::string& client) {
    std::string filename = generateFilename(client);
    std::string tmp_path = base_path_ + filename + ".tmp";
    std::string final_path = base_path_ + filename;

    // Write atomically to .tmp
    if (!atomicWrite(tmp_path, json)) {
        // Cleanup partial tmp if exists
        DeleteFileA(tmp_path.c_str());
        return false;
    }

    // Atomic rename: MoveFileEx with MOVEFILE_REPLACE_EXISTING
    if (!MoveFileExA(tmp_path.c_str(), final_path.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        LOG_ERROR("[httpserver] MoveFileEx failed: " + tmp_path + " -> " + final_path + ", error=" + std::to_string(GetLastError()));
        DeleteFileA(tmp_path.c_str());
        return false;
    }

    LOG_INFO("[httpserver] Callback written: " + filename);
    return true;
}