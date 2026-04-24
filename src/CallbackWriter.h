#pragma once

#include <string>

class CallbackWriter {
public:
    explicit CallbackWriter(const std::string& work_dir);
    ~CallbackWriter();

    // Initialize: detect ImDisk mount, choose path, create directory if needed
    bool init();

    // Write callback JSON atomically: write .tmp + MoveFileEx rename
    // Returns true on success, false on failure
    bool writeCallback(const std::string& json, const std::string& client);

    // Get the currently active callbacks directory (for logging)
    std::string getCallbacksDir() const;

    bool usingImDisk() const { return using_imdisk_; }

private:
    std::string work_dir_;
    std::string base_path_;
    bool using_imdisk_ = false;

    std::string generateFilename(const std::string& client) const;
    bool ensureDirectory();
    bool atomicWrite(const std::string& path, const std::string& content);
};