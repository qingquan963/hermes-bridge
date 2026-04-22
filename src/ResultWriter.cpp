#include "ResultWriter.h"
#include "Logger.h"
#include <fstream>
#include <windows.h>
#include <nlohmann/json.hpp>

ResultWriter::ResultWriter(const std::string& work_dir) : work_dir_(work_dir) {}

void ResultWriter::writeResult(const std::string& client_id, const nlohmann::json& result) {
    std::string out_path = work_dir_ + "\\out_" + client_id + ".txt";
    std::string tmp_path = out_path + ".tmp";

    // Write to tmp file (CREATE_ALWAYS + TRUNCATE_EXISTING)
    HANDLE hTmp = CreateFileW(
        (L"\\\\?\\" + std::wstring(tmp_path.begin(), tmp_path.end())).c_str(),
        GENERIC_WRITE,
        0, // exclusive access
        NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);

    if (hTmp == INVALID_HANDLE_VALUE) {
        LOG_ERROR("Failed to create tmp file: " + tmp_path);
        return;
    }

    std::string content = result.dump(-1, ' ', false);
    DWORD written = 0;
    WriteFile(hTmp, content.c_str(), static_cast<DWORD>(content.size()), &written, NULL);
    FlushFileBuffers(hTmp);
    CloseHandle(hTmp);

    // Atomic rename
    std::wstring wout(out_path.begin(), out_path.end());
    std::wstring wtmp(tmp_path.begin(), tmp_path.end());

    // Use MoveFileEx with MOVEFILE_REPLACE_EXISTING for atomic behavior
    BOOL ok = MoveFileExW(wtmp.c_str(), wout.c_str(), MOVEFILE_REPLACE_EXISTING);
    if (!ok) {
        LOG_ERROR("Atomic rename failed for: " + client_id + " err=" + std::to_string(GetLastError()));
        // Clean up tmp file
        DeleteFileW(wtmp.c_str());
    }
}

void ResultWriter::cleanupTmpFiles() {
    WIN32_FIND_DATAW ffd;
    std::wstring searchPath = L"\\\\?\\" + std::wstring(work_dir_.begin(), work_dir_.end()) + L"\\*.tmp";
    HANDLE hFind = FindFirstFileW(searchPath.c_str(), &ffd);
    if (hFind == INVALID_HANDLE_VALUE) return;
    do {
        std::wstring tmpFile = std::wstring(work_dir_.begin(), work_dir_.end()) + L"\\" + std::wstring(ffd.cFileName);
        DeleteFileW(tmpFile.c_str());
    } while (FindNextFileW(hFind, &ffd));
    FindClose(hFind);
}
