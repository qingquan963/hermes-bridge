#pragma once
#ifndef HERMES_BRIDGE_RESULTWRITER_H
#define HERMES_BRIDGE_RESULTWRITER_H

#include <string>
#include <nlohmann/json.hpp>

class ResultWriter {
public:
    explicit ResultWriter(const std::string& work_dir);
    void writeResult(const std::string& client_id, const nlohmann::json& result);
    void cleanupTmpFiles();

private:
    std::string work_dir_;
};

#endif // HERMES_BRIDGE_RESULTWRITER_H
