#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "wldap32.lib")

#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")

#include <iostream>
#include <string>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <windows.h>

#include "Config.h"
#include "Logger.h"
#include "ThreadPool.h"
#include "CommandQueue.h"
#include "FileMonitor.h"
#include "ResultWriter.h"
#include "StateFile.h"
#include "HttpServer.h"
#include "CallbackClient.h"
#include "handlers/IHandler.h"
#include "handlers/ExecHandler.h"
#include "handlers/FileHandler.h"
#include "handlers/HttpHandler.h"
#include "handlers/OllamaHandler.h"
#include "handlers/ProcessHandler.h"
#include "handlers/ServiceHandler.h"
#include "handlers/TcpProxyHandler.h"

#include <nlohmann/json.hpp>
using json = nlohmann::json;

// Global state
static std::atomic<bool> g_running{true};
static CommandQueue g_queue;
static std::unique_ptr<FileMonitor> g_monitor;
static std::unique_ptr<ThreadPool> g_pool;
static std::unique_ptr<ResultWriter> g_writer;
static std::unique_ptr<StateFile> g_state;
static std::unordered_map<std::string, std::unique_ptr<IHandler>> g_handlers;
static std::atomic<uint64_t> g_total_requests{0};
static std::atomic<uint64_t> g_errors{0};
static Config g_config;
static std::vector<std::thread> g_workers;
static std::unique_ptr<HttpServer> g_httpServer;
static TcpProxyHandler* g_tcpProxyHandler = nullptr;

// Signal handler
static BOOL WINAPI consoleCtrlHandler(DWORD dwCtrlType) {
    if (dwCtrlType == CTRL_C_EVENT || dwCtrlType == CTRL_BREAK_EVENT ||
        dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_SHUTDOWN_EVENT) {
        g_running = false;
        g_queue.wakeAll();
        return TRUE;
    }
    return FALSE;
}

static void runCommandHandler(const Command& cmd) {
    auto start = std::chrono::steady_clock::now();

    HandlerResult result;
    auto it = g_handlers.find(cmd.action);
    if (it != g_handlers.end()) {
        json cmd_json;
        cmd_json["action"] = cmd.action;
        cmd_json["params"] = cmd.params;
        cmd_json["timeout"] = cmd.timeout;
        HandlerContext ctx{cmd_json, cmd.client_id, g_config.default_timeout_sec};
        result = it->second->handle(ctx);
    } else {
        result = HandlerResult::errorResult("INVALID_REQUEST",
            "Unknown action: " + cmd.action, "", 0);
    }

    auto end = std::chrono::steady_clock::now();
    int dur = static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());

    json resp;
    resp["cmd_id"] = cmd.cmd_id;
    if (result.ok) {
        resp["status"] = "ok";
        resp["result"] = result.result;
        g_total_requests++;
    } else {
        resp["status"] = "error";
        resp["error"] = {
            {"code", result.error_code},
            {"message", result.error_message},
            {"details", result.error_details}
        };
        g_errors++;
    }
    resp["duration_ms"] = dur;

    g_writer->writeResult(cmd.client_id, resp);

    // Async callback if callback_url is set
    if (!cmd.cmd_callback_url.empty()) {
        asyncCallback(cmd.cmd_callback_url, resp.dump(), cmd.client_id, cmd.cmd_id);
    }

    LOG_INFO("[{}] Completed cmd {} (status={}, duration_ms={})",
        cmd.client_id, cmd.cmd_id, resp["status"].get<std::string>(), dur);
}

static void workerLoop(int /*worker_id*/) {
    while (g_running.load()) {
        Command cmd = g_queue.dequeue();
        if (!g_running.load() || cmd.cmd_id.empty()) break;
        runCommandHandler(cmd);
    }
}

int main(int argc, char* argv[]) {
    // Determine exe directory
    std::string exe_dir;
    {
        char exe_path[MAX_PATH];
        GetModuleFileNameA(NULL, exe_path, MAX_PATH);
        std::string ep(exe_path);
        size_t pos = ep.find_last_of("\\/");
        exe_dir = (pos != std::string::npos) ? ep.substr(0, pos) : ".";
    }

    std::string config_path = exe_dir + "\\hermes_bridge.json";

    // Load config
    if (!g_config.load(config_path)) {
        std::cerr << "Failed to load config: " << config_path << std::endl;
        return 1;
    }
    if (!g_config.validate()) {
        std::cerr << "Invalid config" << std::endl;
        return 1;
    }

    // Change to work dir
    SetCurrentDirectoryA(g_config.work_dir.c_str());

    // Initialize logger
    Logger::instance().init(g_config.log_dir, g_config.log_file,
                            g_config.log_level, g_config.log_max_size_mb,
                            g_config.log_backup_count);
    LOG_INFO("Hermes Bridge starting...");
    LOG_INFO("Work dir: " + g_config.work_dir);

    // Cleanup old tmp files
    {
        ResultWriter cleanup_writer(g_config.work_dir);
        cleanup_writer.cleanupTmpFiles();
    }

    // Initialize components
    g_pool = std::make_unique<ThreadPool>(g_config.worker_count);
    g_writer = std::make_unique<ResultWriter>(g_config.work_dir);
    g_state = std::make_unique<StateFile>(g_config.work_dir, g_config.state_file);
    g_state->state.poll_interval_ms = g_config.poll_interval_ms;
    g_state->state.worker_count = g_config.worker_count;
    g_state->state.total_workers = g_config.worker_count;
    g_state->state.default_timeout_sec = g_config.default_timeout_sec;

    // Register handlers
    g_handlers["exec"] = std::make_unique<ExecHandler>();
    g_handlers["file_read"] = std::make_unique<FileHandler>();
    g_handlers["file_write"] = std::make_unique<FileHandler>();
    g_handlers["file_patch"] = std::make_unique<FileHandler>();
    g_handlers["http_get"] = std::make_unique<HttpHandler>();
    g_handlers["http_post"] = std::make_unique<HttpHandler>();
    g_handlers["ollama"] = std::make_unique<OllamaHandler>(g_config.ollama_url);
    g_handlers["process_start"] = std::make_unique<ProcessHandler>();
    g_handlers["process_stop"] = std::make_unique<ProcessHandler>();
    g_handlers["ps_service_query"] = std::make_unique<ServiceHandler>();
    g_handlers["tcp_send"] = std::make_unique<TcpProxyHandler>();
    g_handlers["tcp_connect"] = g_handlers["tcp_send"].get();  // alias
    g_tcpProxyHandler = static_cast<TcpProxyHandler*>(g_handlers["tcp_send"].get());
    g_tcpProxyHandler->setWorkDir(g_config.work_dir);
    LOG_INFO("Handlers registered: exec, file_read/write/patch, http_get/post, ollama, process_*, ps_service_query, tcp_send");

    // Start worker threads
    g_workers.reserve(static_cast<size_t>(g_config.worker_count));
    for (int i = 0; i < g_config.worker_count; ++i) {
        g_workers.emplace_back(workerLoop, i);
    }
    LOG_INFO("ThreadPool started with " + std::to_string(g_config.worker_count) + " workers");

    // Start file monitor
    g_monitor = std::make_unique<FileMonitor>(g_config, g_queue);
    g_monitor->start();

    // Start HTTP Server (callback endpoint on port 18900)
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup failed");
        return 1;
    }
    g_httpServer = std::make_unique<HttpServer>("127.0.0.1", 18900, g_config.work_dir);
    if (!g_httpServer->start()) {
        LOG_ERROR("Failed to start HTTP Server");
    } else {
        LOG_INFO("HTTP Server started on 127.0.0.1:18900");
    }

    // Set running state
    g_state->setRunning(true);

    // Register console ctrl handler
    SetConsoleCtrlHandler(consoleCtrlHandler, TRUE);

    LOG_INFO("Hermes Bridge running. PID=" + std::to_string(GetCurrentProcessId()));
    LOG_INFO("Poll interval: " + std::to_string(g_config.poll_interval_ms) + "ms");

    // Main loop: update state file periodically
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        std::vector<std::string> clients;
        if (g_monitor) clients = g_monitor->discoveredClients();
        if (clients.empty()) clients = {"main"};

        g_state->update(
            g_pool->busyCount(),
            static_cast<int>(g_queue.size()),
            clients,
            g_total_requests.load(),
            g_errors.load()
        );
    }

    // Shutdown
    LOG_INFO("Hermes Bridge shutting down...");
    if (g_httpServer) g_httpServer->stop();
    WSACleanup();
    g_queue.wakeAll();
    g_monitor->stop();
    for (auto& t : g_workers) {
        if (t.joinable()) t.join();
    }
    g_state->setRunning(false);
    g_pool->shutdown();
    LOG_INFO("Hermes Bridge stopped.");
    return 0;
}
