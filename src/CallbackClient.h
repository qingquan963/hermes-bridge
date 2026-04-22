#pragma once
#include <string>

void asyncCallback(const std::string& url, const std::string& json_body,
                   const std::string& client_id, const std::string& cmd_id);
