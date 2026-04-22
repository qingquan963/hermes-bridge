# Hermes Bridge — 架构设计文档

**文档版本**：v3
**更新日期**：2026-04-22
**架构师**：Architect（DeepSeek）
**状态**：✅ 审核通过，可进入实现阶段

---

## 一、设计目标

Hermes Bridge 是 Hermes Agent（WSL Ubuntu）在 Windows侧的桥接服务，使 Hermes 能够调用 Windows 本地资源（PowerShell、文件、HTTP、Ollama 等）。

**核心设计原则**：
1. **简单可靠**：Hermes 发指令天然串行，架构不做过度设计
2. **文件为协议**：Hermes ↔ Bridge 通过文件系统通信（cmd_*.txt / out_*.txt）
3. **多 client 并发**：支持 Hermes 主agent 及多个子 agent 同时独立通信
4. **原子安全**：写入永远先写 .tmp 再 rename，不提供半截数据

---

## 二、系统架构

```
┌─────────────────────────────────────────────────────────────────────┐
│  Hermes (WSL Ubuntu)                                                │
│  ┌──────────┐  ┌──────────┐  ┌──────────┐                           │
│  │ 主 Agent │  │ 子Agent1 │  │ 子Agent2 │  ...                     │
│  └────┬─────┘  └────┬─────┘  └────┬─────┘                           │
│       │              │              │                                │
│       ↓              ↓              ↓                                │
│  cmd_main.txt  cmd_agent1.txt  cmd_agent2.txt                       │
└─────────────────────────────────────────────────────────────────┼───┘
                                                                    ↓
┌─────────────────────────────────────────────────────────────────────┐
│  Hermes Bridge (C++ Windows Daemon, hermes_bridge.exe)              │
│                                                                      │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ FileMonitor — 5s 轮询所有 cmd_<client_id>.txt                  │   │
│  │  · 检测文件存在                                                 │   │
│  │  · Windows LockFileEx 尝试加锁（读）                            │   │
│  │  · JSON 完整性检查 → 不完整则跳过                               │   │
│  └─────────────────────────┬────────────────────────────────────┘   │
│                            ↓                                         │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ CommandQueue — 无界并发安全队列（std::queue + mutex）          │   │
│  └─────────────────────────┬────────────────────────────────────┘   │
│                            ↓                                         │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ ThreadPool — 5 workers（自研，无外部依赖）                     │   │
│  │  · 每个 worker 从队列取指令，独立执行                           │   │
│  │  · 不同 client_id 的指令互不阻塞                               │   │
│  └─────────────────────────┬────────────────────────────────────┘   │
│                            ↓                                         │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ Action Handlers（策略模式，每个 action 一个 handler）          │   │
│  │  ┌──────────────┐ ┌──────────────┐ ┌──────────────┐         │   │
│  │  │ExecHandler   │ │FileHandler   │ │HttpHandler    │ ...    │   │
│  │  └──────────────┘ └──────────────┘ └──────────────┘         │   │
│  └─────────────────────────┬────────────────────────────────────┘   │
│                            ↓                                         │
│  ┌──────────────────────────────────────────────────────────────┐   │
│  │ ResultWriter — 原子写入                                        │   │
│  │  · 先写 out_<client_id>.tmp                                    │   │
│  │  · rename 为 out_<client_id>.txt（原子操作）                    │   │
│  │  · Hermes 只能读到完整结果                                      │   │
│  └──────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────┘
                              ↓
                    out_main.txt / out_agent1.txt / out_agent2.txt
                              ↓
                    Hermes 轮询读取（间隔 1s，可配置）
```

---

## 三、核心技术决策

### 3.1 文件轮询 + 文件锁

**轮询策略**：
- 每 5 秒扫描 `C:\lobster\hermes_bridge\cmd_*.txt`
- 支持动态发现新 client_id（第一次发现某 client_id 时自动注册监听）
- 文件不存在则跳过，不报错

**文件锁**：
- 使用 Windows API `LockFileEx` 加锁
- `LOCKFILE_EXCLUSIVE_LOCK` 独占读锁
- 加锁失败（另一进程持有）则本次跳过，等待下次轮询

**WSL 文件锁风险缓解**（Section 9.4 确认）：
- Hermes 侧写 cmd 文件走 WSL 文件系统层，不经过 Windows 文件锁 API
- Bridge 侧 `LockFileEx` 无法阻止 Hermes 写入
- **缓解措施**：JSON 完整性检查 + 原子写入，Hermes 发指令天然串行
- **结论**：实际影响有限，架构认可此风险

### 3.2 JSON 完整性检查（★★★ P0 必须实现 ★★★）

读取 cmd 文件后、入队前，必须进行 JSON 完整性检查：

```cpp
bool isJsonComplete(const std::string& content) {
    // 1. 空文件或纯空白 → 不完整
    if (content.find_first_not_of(" \t\r\n") == std::string::npos) return false;
    
    // 2. 不是以 { 或 [ 开头 → 不完整
    if (content[0] != '{' && content[0] != '[') return false;
    
    // 3. 尝试解析，失败 → 不完整
    try {
        json::parse(content);
        return true;
    } catch (const json::parse_error&) {
        return false;
    }
}
```

**边界情况处理**：
| 情况 | 处理 |
|------|------|
| 空文件 | 跳过本次处理 |
| 纯空白 | 跳过本次处理 |
| 截断 JSON（如 `{"cmd_id": "uuid`）| 跳过本次处理，等待下次轮询 |
| 完整 JSON | 入队处理 |
| JSON 数组（含多条指令）| 逐条解析入队 |

### 3.3 原子写入（★★★ P0 必须实现 ★★★）

写入 out 文件必须使用两步原子操作：

```
Step 1: 写入 out_<client_id>.tmp（临时文件，与 out_<client_id>.txt 不同名）
Step 2: rename("out_<client_id>.tmp", "out_<client_id>.txt")（原子替换）
```

**为什么 rename 是原子的**：
- Windows 上对同一目录的 rename 操作是原子的
- 如果 rename 成功，Hermes 读到的一定是完整文件
- 如果 Bridge 在 Step 1 和 Step 2 之间崩溃，Hermes 读到的仍是旧 out 文件（完整但过时）

**实现要点**：
- 临时文件和目标文件必须在**同一目录**（rename 跨目录不原子）
- 写入前清空 tmp 文件内容（避免残留旧数据）
- rename 失败时删除 tmp 文件，不留垃圾

### 3.4 多 client 并发框架

**文件隔离**：
- 每个 client_id 独立 cmd 文件和 out 文件
- `cmd_main.txt` / `out_main.txt` — Hermes 主agent
- `cmd_<agent_id>.txt` / `out_<agent_id>.txt` — 子agent（动态分配）

**并发处理**：
- FileMonitor 负责扫描所有 cmd 文件，发现就入队
- ThreadPool 的 N 个 worker 从队列取指令执行
- 同一 client_id 的指令按入队顺序串行执行（队列保序）
- 不同 client_id 的指令完全并行（互不阻塞）

**client_id 发现机制**：
- Bridge 启动时扫描 `C:\lobster\hermes_bridge\cmd_*.txt`
- 运行中轮询时发现新的 cmd 文件，自动注册该 client_id
- 不需要预先配置 client_id 列表

---

## 四、组件设计

### 4.1 FileMonitor

**职责**：检测 cmd 文件变化，读取并验证后入队

**流程**：
```
for each cmd_<client_id>.txt in C:\lobster\hermes_bridge\:
    1. LockFileEx(shared) — 加共享读锁
    2. ReadFile → content
    3. UnlockFile
    4. if isJsonComplete(content):
           parse JSON
           for each cmd in JSON:
               enqueue(Command{cmd_id, action, params, client_id})
    5. 清空 cmd 文件（或 rename 为 .processed 备份）
```

**注意**：清空 cmd 文件使用 truncate，避免 Hermes 下一条指令被覆盖

### 4.2 ThreadPool（自研，<100行）

**职责**：管理固定数量 worker，从队列取指令执行

**设计**（无外部依赖）：
```cpp
class ThreadPool {
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;
    std::mutex mtx;
    std::condition_variable cv;
    bool stop;
public:
    // N 个 worker，每个执行: while(!stop) { wait → 取任务 → 执行 }
};
```

**默认配置**：5 workers（可配置）

### 4.3 Action Handlers（策略模式）

每个 action 对应一个 Handler，统一接口：

```cpp
struct HandlerContext {
    const json& cmd;        // 原始指令
    const std::string& client_id;
    spdlog::logger& logger;
};

struct HandlerResult {
    json result;             // 成功时填
    std::string error_code;  // 失败时填
    std::string error_msg;
    bool ok;
};

class IHandler {
public:
    virtual ~IHandler() = default;
    virtual HandlerResult handle(const HandlerContext&) = 0;
    virtual std::string actionName() const = 0;
};
```

**Handler 注册表**（编译时静态注册）：
```cpp
static std::unordered_map<std::string, std::unique_ptr<IHandler>> handlers = {
    {"exec",           std::make_unique<ExecHandler>()},
    {"file_read",     std::make_unique<FileReadHandler>()},
    {"file_write",    std::make_unique<FileWriteHandler>()},
    {"file_patch",    std::make_unique<FilePatchHandler>()},
    {"process_start", std::make_unique<ProcessStartHandler>()},
    {"process_stop",  std::make_unique<ProcessStopHandler>()},
    {"http_get",      std::make_unique<HttpGetHandler>()},
    {"http_post",     std::make_unique<HttpPostHandler>()},
    {"ollama",        std::make_unique<OllamaHandler>()},
    {"ps_service_query", std::make_unique<PsServiceQueryHandler>()},
};
```

### 4.4 ExecHandler

**职责**：执行 PowerShell/Python 命令

**实现要点**：
- 使用 `CreateProcessW`（Windows API）创建子进程
- `powershell.exe -Command "..."` 或 `python.exe ...`
- 捕获 stdout/stderr（pipe）
- 支持 timeout（默认 30s，可配置）
- 超时杀子进程（`TerminateProcess`）

### 4.5 FileReadHandler / FileWriteHandler

**FileReadHandler**：
- 使用 `CreateFileW` + `ReadFile`（wide char API）
- 支持 offset/limit（部分读取）
- encoding 参数：`utf-8`（默认）、`gbk`（兼容）
- 路径使用 `\\?\` 前缀支持长路径（>260 字符）

**FileWriteHandler**（★★★ 原子写入 ★★★）：
- 先打开/创建 `out_<client_id>.tmp`
- WriteFile 写入完整内容
- rename 为 `out_<client_id>.txt`
- 失败则删除 tmp 文件

### 4.6 HttpHandler（libcurl）

**依赖**：libcurl 8.x 静态链接

**静态链接方案**（Section 9 建议明确）：
- 使用 vcpkg 安装 libcurl[x64-windows-static]
- 编译选项：`/MT`（静态 CRT）+ `DYNAMICBASE=NO`（libcurl 要求）
- SSL 支持：WinSSL（Windows 原生，不需要额外 OpenSSL 依赖）

**HttpGetHandler**：
```cpp
CURL* curl = curl_easy_init();
curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
curl_easy_setopt(curl, CURLOPT_TIMEOUT, timeout);
curl_easy_setopt(curl, CURLOPT_SSL_OPTIONS, CURLSSLOPT_NO_REVOKE); // Windows 兼容
// response 写入 string buffer
CURLcode res = curl_easy_perform(curl);
curl_easy_cleanup(curl);
```

**HttpPostHandler**：
- `CURLOPT_POST` + `CURLOPT_POSTFIELDS`
- Content-Type 自动设置为 `application/json`

### 4.7 OllamaHandler

**职责**：调用本地 Ollama（Windows 版）

**实现**：
- 使用 HttpPostHandler 封装
- URL: `http://127.0.0.1:11434/api/generate`
- Request body: `{"model": "...", "prompt": "...", "stream": false}`
- Response 解析 `response` 字段

### 4.8 ResultWriter

**职责**：将执行结果写入 out 文件（原子操作）

**流程**：
```
writeResult(cmd_id, client_id, HandlerResult):
    1. 构建响应 JSON {cmd_id, status, result/error, duration_ms}
    2. 打开 C:\lobster\hermes_bridge\out_<client_id>.tmp（CREATE_ALWAYS）
    3. WriteFile(content) — utf-8 编码
    4. FlushFileBuffers — 确保刷到磁盘
    5. rename("out_<client_id>.tmp", "out_<client_id>.txt")
    6. if rename 失败: DeleteFile("out_<client_id>.tmp")
```

---

## 五、日志系统（spdlog）

**配置**：
- 异步日志（`spdlog::async_factory`）
- 轮转文件：最大 10MB，保留 5 个备份
- 路径：`C:\lobster\hermes_bridge\logs\events.txt`
- 格式：`[%Y-%m-%d %H:%M:%S.%e] [%l] [client_id] message`

**日志级别**：
| 级别 | 场景 |
|------|------|
| INFO | 指令入队、执行完成、结果写入 |
| WARN | JSON 解析失败跳过、超时、lock 失败 |
| ERROR | handler 执行异常、文件写入失败 |
| DEBUG | 详细调试（可选，配置开启）|

---

## 六、状态暴露（state.json）

Bridge 暴露自身状态到 `C:\lobster\hermes_bridge\state.json`：

```json
{
  "version": "1.0.0",
  "status": "running",
  "uptime_seconds": 3600,
  "workers": {
    "total": 5,
    "busy": 2,
    "idle": 3
  },
  "queue_length": 0,
  "clients": ["main", "agent1", "agent2"],
  "last_poll_ms": 1234,
  "last_cmd_count": 15,
  "total_requests": 1500,
  "errors": 3
}
```

Hermes 可通过 `file_read` action 读取此文件，监控 Bridge 健康状态。

---

## 七、崩溃自拉起方案

**推荐方案：NSSM（Non-Sucking Service Manager）**

```
nssm install HermesBridge C:\lobster\hermes_bridge\hermes_bridge.exe
nssm set HermesBridge AppDirectory C:\lobster\hermes_bridge
nssm set HermesBridge Description "Hermes Bridge - Windows Resource Bridge for Hermes Agent"
nssm set HermesBridge Start SERVICE_AUTO_START
nssm set HermesBridge AppRestartDelay 5000  // 5秒后自动重启
```

**优势**：
- 注册为真实 Windows Service
- 崩溃后立即重启（5s 延迟）
- 支持 `sc query HermesBridge` 查看状态
- 比 Task Scheduler（最小 1 分钟）可靠得多

---

## 八、编译配置（MSVC x64）

**CMakeLists.txt 关键配置**：
```cmake
cmake_minimum_required(VERSION 3.15)
project(HermesBridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC: 静态链接 CRT，x64
set(CMAKE_C_FLAGS_RELEASE "/MT /O2")
set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2")
set(CMAKE_EXE_LINKER_FLAGS "/DYNAMICBASE:NO")  # libcurl 要求

# vcpkg 集成
find_package(curl CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

target_link_libraries(HermesBridge PRIVATE
    curl::curl
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)
```

**vcpkg 安装命令**：
```powershell
vcpkg install curl:x64-windows-static spdlog:x64-windows nlohmann-json:x64-windows
```

---

## 九、关键设计约束

| 约束 | 说明 |
|------|------|
| cmd 文件只读不清 | Bridge 读完 cmd 文件内容后 truncate 清空，不 rename |
| out 文件必须原子 | 先写 .tmp 再 rename，不提供半截数据 |
| JSON 完整性是 P0 必须 | 读到不完整 JSON 跳过，不 crash，不报错 |
| 同一 client 串行 | 同一 client_id 的指令按队列顺序执行 |
| 不同 client 并行 | 不同 client_id 的指令完全并行，worker 不等待 |
| 轮询 5s 不可调 | 5s 是架构约定，Hermes 侧轮询 1s，整体延迟可接受 |

---

## 十、架构决策记录（ADR）

### ADR-001：技术选型
**决策**：libcurl 8.x / nlohmann/json / 自研线程池 / spdlog / MSVC x64  
**理由**：Section 9.2 百事通第九节审核通过  
**状态**：✅ 冻结

### ADR-002：JSON 完整性检查
**决策**：读取 cmd 文件后必须 JSON 完整性检查，不完整则跳过  
**理由**：WSL 文件锁与 Windows 文件锁不兼容，需要应用层兜底  
**状态**：✅ P0 必须实现

### ADR-003：原子写入
**决策**：out 文件写入必须先写 .tmp 再 rename  
**理由**：Hermes 可能在 Bridge 写入过程中读取，必须保证原子性  
**状态**：✅ P0 必须实现

### ADR-004：崩溃自拉起
**决策**：使用 NSSM 注册为 Windows Service  
**理由**：Task Scheduler 最小粒度 1 分钟，NSSM 可 5 秒重启，可靠性更高  
**状态**：⏳ 待 Executor 实施

### ADR-005：libcurl 静态链接方案
**决策**：vcpkg 安装 curl:x64-windows-static，SSL 使用 WinSSL  
**理由**：避免手动管理 OpenSSL/zlib 依赖，vcpkg 一站式解决  
**状态**：⏳ 待 P0 阶段验证

---

*本文档由 Architect（DeepSeek）基于百事通需求文档第九节 + PM_REVIEW.md v2 生成*
*审核状态：✅ 通过，可进入实现阶段*
