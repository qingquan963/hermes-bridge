# Hermes Bridge — 详细规格文档

**文档版本**：v3
**更新日期**：2026-04-22
**架构师**：Architect（DeepSeek）
**状态**：✅ 审核通过，可进入实现阶段

---

## 一、项目概述

### 1.1 项目信息

| 项目 | 内容 |
|------|------|
| 项目名称 | Hermes Bridge |
| 项目类型 | C++ Windows 常驻服务（Daemon）|
| 核心功能 | Hermes（WSL）与 Windows 资源的桥接，通过文件轮询实现双向通信 |
| 编译目标 | `hermes_bridge.exe`（单个可执行文件）|
| 部署路径 | `C:\lobster\hermes_bridge\` |

### 1.2 技术栈

| 组件 | 选型 | 版本要求 |
|------|------|---------|
| 编程语言 | C++ | C++17 |
| 编译器 | MSVC | VS Build Tools 2022 x64 |
| CRT 链接 | 静态 | `/MT` |
| HTTP 客户端 | libcurl | 8.x，静态链接 |
| JSON 解析 | nlohmann/json | v3.10+ |
| 日志 | spdlog | 异步 + 轮转 |
| 线程池 | 自研 | lightweight，<100行 |
| 包管理 | vcpkg | latest |

---

## 二、目录结构

```
C:\lobster\hermes_bridge\
├── hermes_bridge.exe       # 编译产物（单一可执行文件）
├── hermes_bridge.json      # 配置文件
├── logs\                   # 日志目录（spdlog 轮转）
│   └── events.txt         # 操作日志（自动轮转）
├── cmd_*.txt               # 指令文件（Hermes 写，Bridge 读）
├── out_*.txt               # 结果文件（Bridge 写，Hermes 读）
├── state.json             # Bridge 自身状态
├── README.md               # 部署说明
└── CMakeLists.txt         # 构建配置
```

**文件命名规范**：
- cmd 文件：`cmd_<client_id>.txt`（小写，client_id 由字母数字下划线组成）
- out 文件：`out_<client_id>.txt`（与 cmd 一一对应）
- client_id 示例：`main`、`agent1`、`agent2`、`sub_abc123`

---

## 三、配置文件（hermes_bridge.json）

```json
{
  "version": "1.0.0",
  "poll_interval_ms": 5000,
  "worker_count": 5,
  "default_timeout_sec": 30,
  "log_dir": "logs",
  "log_file": "events.txt",
  "log_level": "info",
  "log_max_size_mb": 10,
  "log_backup_count": 5,
  "work_dir": "C:\\lobster\\hermes_bridge",
  "state_file": "state.json",
  "max_request_size_kb": 10240,
  "ollama_url": "http://127.0.0.1:11434/api/generate"
}
```

**字段说明**：

| 字段 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| poll_interval_ms | int | 5000 | FileMonitor 轮询间隔（毫秒）|
| worker_count | int | 5 | 线程池 worker 数量 |
| default_timeout_sec | int | 30 | 指令默认超时（秒）|
| log_dir | string | logs | 日志目录（相对路径）|
| log_file | string | events.txt | 日志文件名 |
| log_level | string | info | 日志级别：debug/info/warn/error |
| log_max_size_mb | int | 10 | 单个日志文件最大大小（MB）|
| log_backup_count | int | 5 | 保留备份数量 |
| work_dir | string | — | Bridge 工作目录 |
| state_file | string | state.json | 状态文件路径 |
| max_request_size_kb | int | 10240 | cmd 文件最大大小（KB）|
| ollama_url | string | — | Ollama API 地址 |

---

## 四、指令协议

### 4.1 cmd 文件格式

Bridge 读取 `cmd_<client_id>.txt`，内容为 JSON（数组或对象）：

**单条指令**：
```json
{
  "cmd_id": "550e8400-e29b-41d4-a716-446655440000",
  "action": "exec",
  "params": {
    "command": "powershell.exe -Command \"Get-Process | Select-Object -First 5\"",
    "shell": "powershell",
    "cwd": "C:\\Users\\Administrator"
  },
  "timeout": 30,
  "timestamp": "2026-04-22T08:00:00Z"
}
```

**多条指令（数组）**：
```json
[
  {
    "cmd_id": "uuid-1",
    "action": "file_read",
    "params": {"path": "C:\\test\\a.txt"},
    "timeout": 10
  },
  {
    "cmd_id": "uuid-2",
    "action": "file_read",
    "params": {"path": "C:\\test\\b.txt"},
    "timeout": 10
  }
]
```

### 4.2 out 文件格式

Bridge 写入 `out_<client_id>.txt`，内容为 JSON（数组或对象，顺序与 cmd 一一对应）：

**单条结果**：
```json
{
  "cmd_id": "550e8400-e29b-41d4-a716-446655440000",
  "status": "ok",
  "result": {
    "stdout": "..." ,
    "stderr": "",
    "exit_code": 0
  },
  "duration_ms": 1234
}
```

**多条结果（数组）**：
```json
[
  {
    "cmd_id": "uuid-1",
    "status": "ok",
    "result": {...},
    "duration_ms": 100
  },
  {
    "cmd_id": "uuid-2",
    "status": "ok",
    "result": {...},
    "duration_ms": 200
  }
]
```

**错误响应**：
```json
{
  "cmd_id": "uuid-xxx",
  "status": "error",
  "error": {
    "code": "EXEC_FAILED",
    "message": "command timed out",
    "details": "Timeout after 30 seconds"
  },
  "duration_ms": 30001
}
```

### 4.3 错误码定义

| 错误码 | 说明 | 触发场景 |
|--------|------|---------|
| INVALID_REQUEST | 请求格式错误 | JSON 解析失败、缺少必填字段 |
| FILE_NOT_FOUND | 文件不存在 | file_read 目标不存在 |
| FILE_WRITE_FAILED | 文件写入失败 | 磁盘满、权限问题 |
| EXEC_FAILED | 命令执行失败 | 进程启动失败、非0退出码 |
| EXEC_TIMEOUT | 命令执行超时 | 超过 timeout |
| HTTP_ERROR | HTTP 请求失败 | curl_easy_perform 返回非 0 |
| OLLAMA_ERROR | Ollama 调用失败 | API 返回非 200 |
| PROCESS_NOT_FOUND | 进程未找到 | process_stop 目标不存在 |
| SERVICE_NOT_FOUND | 服务未找到 | ps_service_query 目标不存在 |
| INTERNAL_ERROR | Bridge 内部错误 | 未预期的异常 |

---

## 五、Action 详细规格

### 5.1 exec — 命令执行

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| command | string | ✅ | 要执行的命令（含完整参数）|
| shell | string | ❌ | 执行器：`powershell`（默认）、`cmd`、`python` |
| cwd | string | ❌ | 工作目录（默认继承 Bridge 进程目录）|
| timeout | int | ❌ | 超时秒数（默认 30）|

**结果**：

```json
{
  "stdout": "string",
  "stderr": "string",
  "exit_code": 0,
  "killed": false
}
```

**实现要求**：
- 使用 Windows API `CreateProcessW`
- `bInheritHandles = FALSE`，安全
- 超时使用 `TerminateProcess` 杀进程
- stdout/stderr 分离捕获（分别 pipe）

### 5.2 file_read — 读取文件

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | ✅ | 文件绝对路径 |
| offset | int | ❌ | 读取起始位置（字节），默认 0 |
| limit | int | ❌ | 最大读取字节数，默认 0（全部）|
| encoding | string | ❌ | 编码：`utf-8`（默认）、`gbk` |

**结果**：

```json
{
  "content": "string",
  "size": 1234,
  "encoding": "utf-8"
}
```

**实现要求**：
- 使用 `CreateFileW` + `ReadFile`（wide char API）
- 路径使用 `\\?\` 前缀支持超长路径
- encoding 影响 content 字段的编码转换

### 5.3 file_write — 写入文件（★★★ 原子写入 ★★★）

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | ✅ | 文件绝对路径 |
| content | string | ✅ | 写入内容 |
| encoding | string | ❌ | 编码：`utf-8`（默认）、`gbk` |
| append | bool | ❌ | 是否追加模式，默认 false（覆盖）|

**结果**：

```json
{
  "bytes_written": 1234,
  "path": "C:\\..."
}
```

**实现要求（★★★ 原子写入 ★★★）**：

```
1. 打开 path.tmp（CREATE_ALWAYS，TRUNCATE_EXISTING）
2. WriteFile(content, encoding)
3. FlushFileBuffers
4. rename path.tmp → path（原子替换）
5. if rename 失败: DeleteFile(path.tmp), return error
```

### 5.4 file_patch — 替换文件内容

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| path | string | ✅ | 文件绝对路径 |
| old_string | string | ✅ | 待替换的字符串 |
| new_string | string | ✅ | 替换后的字符串 |
| replace_all | bool | ❌ | 是否全部替换，默认 false |

**结果**：

```json
{
  "replacements": 1,
  "path": "C:\\..."
}
```

**实现要求**：
- 先 file_read 读取内容
- 内存中替换字符串
- 再 file_write 写回（使用原子写入）

### 5.5 process_start — 启动进程

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| command | string | ✅ | 命令（含参数）|
| detached | bool | ❌ | 是否脱离 Bridge 进程（守护模式），默认 true |
| cwd | string | ❌ | 工作目录 |

**结果**：

```json
{
  "pid": 12345,
  "command": "python C:\\lobster\\start.py"
}
```

**实现要求**：
- `detached=true` 使用 `CREATE_NO_WINDOW` + 继承句柄关闭
- `detached=false` 等待进程结束，返回 exit_code

### 5.6 process_stop — 停止进程

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| name | string | ❌ | 进程名称（不完全匹配，如 "python"）|
| pid | int | ❌ | 进程 ID（与 name 二选一）|
| port | int | ❌ | 通过端口号查找进程（netstat）|

**结果**：

```json
{
  "stopped": true,
  "killed_pids": [12345, 12346]
}
```

### 5.7 http_get — GET 请求

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| url | string | ✅ | 目标 URL |
| timeout | int | ❌ | 超时秒数，默认 10 |

**结果**：

```json
{
  "status_code": 200,
  "body": "string",
  "headers": {"Content-Type": "application/json"},
  "time_ms": 234
}
```

### 5.8 http_post — POST 请求

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| url | string | ✅ | 目标 URL |
| json | object | ❌ | JSON 请求体（与 body 二选一）|
| body | string | ❌ | 原始请求体（与 json 二选一）|
| headers | object | ❌ | 额外请求头 |
| timeout | int | ❌ | 超时秒数，默认 10 |

**结果**：

```json
{
  "status_code": 200,
  "body": "string",
  "headers": {...},
  "time_ms": 234
}
```

### 5.9 ollama — 调用本地 Ollama

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| model | string | ✅ | 模型名称，如 "qwen2.5" |
| prompt | string | ✅ | 输入提示词 |
| stream | bool | ❌ | 是否流式返回，默认 false |
| options | object | ❌ | 额外参数（temperature、top_p 等）|

**结果**：

```json
{
  "model": "qwen2.5",
  "response": "string",
  "done": true,
  "total_duration_ms": 1234
}
```

### 5.10 ps_service_query — 查询 Windows 服务

**参数**：

| 字段 | 类型 | 必填 | 说明 |
|------|------|------|------|
| service_name | string | ✅ | 服务名称（如 "WinRM"）|

**结果**：

```json
{
  "name": "WinRM",
  "display_name": "Windows Remote Management",
  "status": "Running",
  "start_type": "Automatic",
  "can_stop": true
}
```

---

## 六、JSON 完整性检查规格

### 6.1 检查流程

```
readFile(path):
    1. LockFileEx(shared) — 加共享读锁
    2. ReadFile → raw_content
    3. UnlockFile — 解锁
    4. return raw_content

parseCmdFile(path, client_id):
    1. content = readFile(path)
    2. if not isJsonComplete(content):
           logger.warn("Incomplete JSON from {}", client_id)
           return []  // 空数组，跳过
    3. json = json::parse(content)
    4. if json.is_array():
           return json
    else:
           return [json]  // 单对象包装为数组
```

### 6.2 isJsonComplete 实现

```cpp
bool isJsonComplete(const std::string& content) {
    // 规则1：非空（允许纯空白 → 不通过）
    auto pos = content.find_first_not_of(" \t\r\n");
    if (pos == std::string::npos) return false;
    
    // 规则2：以 { 或 [ 开头
    char first_char = content[pos];
    if (first_char != '{' && first_char != '[') return false;
    
    // 规则3：能成功解析
    try {
        json::parse(content);
        return true;
    } catch (const json::parse_error&) {
        return false;
    }
}
```

### 6.3 边界测试用例

| 输入 | 期望结果 | 原因 |
|------|---------|------|
| `""` | ❌ 不完整 | 空文件 |
| `"   "` | ❌ 不完整 | 纯空白 |
| `"{}"` | ✅ 完整 | 有效空 JSON 对象 |
| `"[{}]"` | ✅ 完整 | 有效空 JSON 数组 |
| `"{\"cmd_id\": \"uuid"` | ❌ 不完整 | 截断 JSON |
| `"not json"` | ❌ 不完整 | 不是 JSON |
| `"{}extra"` | ❌ 不完整 | JSON 后有多余内容（不严格，但保守处理）|

---

## 七、原子写入规格

### 7.1 写入流程

```cpp
void atomicWrite(const std::string& client_id, const json& result) {
    std::string out_path = "out_" + client_id + ".txt";
    std::string tmp_path = out_path + ".tmp";
    
    // Step 1: 写入 tmp 文件
    {
        auto tmp_file = open(tmp_path, "wb");  // CREATE_ALWAYS, TRUNCATE
        std::string content = result.dump(-1, ' ', false);  // 紧凑格式
        // content 使用 utf-8 编码
        write(tmp_file, content);
        flush(tmp_file);
        // tmp_file 析构时自动关闭
    }
    
    // Step 2: 原子 rename
    if (!rename(tmp_path.c_str(), out_path.c_str())) {
        // 成功，out_<client_id>.txt 现在包含完整结果
    } else {
        // rename 失败，清理 tmp 文件
        DeleteFile(tmp_path.c_str());
        throw std::runtime_error("atomic write failed");
    }
}
```

### 7.2 验证方法

Hermes 读取 out 文件时：
- 要么读到旧结果（完整但过时）
- 要么读到新结果（完整且最新）
- **绝不可能**读到半截结果（因为 rename 是原子的）

---

## 八、状态文件规格（state.json）

```json
{
  "version": "1.0.0",
  "status": "running",
  "pid": 12345,
  "start_time": "2026-04-22T08:00:00Z",
  "uptime_seconds": 3600,
  "workers": {
    "total": 5,
    "busy": 2,
    "idle": 3
  },
  "queue_length": 0,
  "clients": ["main", "agent1", "agent2"],
  "stats": {
    "total_requests": 1500,
    "ok_requests": 1497,
    "error_requests": 3,
    "avg_duration_ms": 250
  },
  "config": {
    "poll_interval_ms": 5000,
    "worker_count": 5,
    "default_timeout_sec": 30
  }
}
```

**更新频率**：每次 poll 结束时更新（5s）

---

## 九、日志规格

### 9.1 日志格式

```
[%Y-%m-%d %H:%M:%S.%e] [%l] [%t] [client_id] message
```

**示例**：
```
[2026-04-22 08:00:05.123] [info] [pool-1] [main] Enqueued cmd 550e8400-e29b-41d4-a716-446655440000 (action=exec)
[2026-04-22 08:00:05.234] [info] [pool-2] [agent1] Enqueued cmd 550e8401-e29b-41d4-a716-446655440001 (action=file_read)
[2026-04-22 08:00:05.456] [info] [pool-1] [main] Completed cmd 550e8400-e29b-41d4-a716-446655440000 (status=ok, duration_ms=230)
[2026-04-22 08:00:05.789] [warn] [monitor] [main] Incomplete JSON, skipping (file=cmd_main.txt)
```

### 9.2 日志级别说明

| 级别 | 触发条件 | 示例 |
|------|---------|------|
| debug | 详细调试信息（可选开启）| 队列长度变化、lock 获取结果 |
| info | 正常流程 | 指令入队/完成、启动/停止 |
| warn | 异常但可恢复 | JSON 不完整、lock 失败、超时 |
| error | 异常不可恢复 | handler crash、文件写入失败 |

---

## 十、构建规格

### 10.1 依赖安装（vcpkg）

```powershell
# 安装依赖
vcpkg install curl:x64-windows-static spdlog:x64-windows nlohmann-json:x64-windows

# 集成到 MSVC
vcpkg integrate install
```

### 10.2 CMake 配置

**最小 CMakeLists.txt**：
```cmake
cmake_minimum_required(VERSION 3.15)
project(HermesBridge CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MSVC 静态链接
string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE}")
string(REPLACE "/MD" "/MT" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")

# vcpkg 集成
find_package(curl CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

add_executable(hermes_bridge
    src/main.cpp
    src/Config.cpp
    src/ThreadPool.cpp
    src/FileMonitor.cpp
    src/CommandQueue.cpp
    src/handlers/ExecHandler.cpp
    src/handlers/FileHandler.cpp
    src/handlers/HttpHandler.cpp
    src/handlers/OllamaHandler.cpp
    src/handlers/ServiceHandler.cpp
    src/ResultWriter.cpp
    src/Logger.cpp
    src/StateFile.cpp
)

target_link_libraries(hermes_bridge PRIVATE
    curl::curl
    spdlog::spdlog
    nlohmann_json::nlohmann_json
)

# 运行时目录
set_target_properties(hermes_bridge PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}"
)
```

### 10.3 编译命令

```powershell
cd C:\lobster\hermes_bridge
mkdir build && cd build
cmake .. -G "Visual Studio 17 2022" -A x64 -DCMAKE_TOOLCHAIN_FILE=<vcpkg_root>/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release
```

### 10.4 构建产物

```
C:\lobster\hermes_bridge\hermes_bridge.exe
```

---

## 十一、部署规格

### 11.1 部署前检查

- [ ] 确认 `C:\lobster\hermes_bridge\` 目录存在
- [ ] 确认 `hermes_bridge.exe` 和 `hermes_bridge.json` 在部署目录
- [ ] 确认防火墙允许必要端口（如 Ollama 11434）

### 11.2 NSSM 注册 Windows Service

```powershell
# 下载 nssm（如果未安装）
choco install nssm -y

# 注册服务
nssm install HermesBridge "C:\lobster\hermes_bridge\hermes_bridge.exe" ""
nssm set HermesBridge AppDirectory "C:\lobster\hermes_bridge"
nssm set HermesBridge Description "Hermes Bridge - Windows Resource Bridge for Hermes Agent"
nssm set HermesBridge Start SERVICE_AUTO_START
nssm set HermesBridge AppRestartDelay 5000

# 启动服务
net start HermesBridge
```

### 11.3 验证部署

```powershell
# 检查服务状态
sc query HermesBridge

# 检查运行状态
type C:\lobster\hermes_bridge\state.json

# 检查日志
type C:\lobster\hermes_bridge\logs\events.txt
```

### 11.4 卸载

```powershell
net stop HermesBridge
nssm remove HermesBridge confirm
```

---

## 十二、测试规格

### 12.1 单元测试（P0 阶段）

| 测试项 | 测试内容 |
|--------|---------|
| JSON 完整性检查 | 空文件、纯空白、截断 JSON、有效 JSON |
| 原子写入 | 写入过程中 Kill，验证无残留 .tmp 文件 |
| ThreadPool | 5 workers 并发执行 10 个任务，验证无丢失 |
| exec handler | 正常命令、超时命令、不存在的命令 |
| file_read handler | 正常读取、不存在文件、超长路径 |
| file_write handler | 正常写入、只读文件、中文内容 |

### 12.2 集成测试（P0 完成后）

| 测试项 | 测试内容 |
|--------|---------|
| Hermes → Bridge → PowerShell | 端到端验证 exec action |
| 双 client 并发 | main + agent1 同时发指令，互不干扰 |
| 崩溃恢复 | Kill Bridge，验证 NSSM 自动拉起 |
| 长时间运行 | 连续 24h 运行，无内存泄漏 |

---

## 十三、验收标准

### 13.1 P0 验收（进入 P1 前必须通过）

- [ ] Bridge 启动成功，state.json 显示 running
- [ ] exec action 能执行 PowerShell 命令并返回 stdout/stderr
- [ ] file_read / file_write 中文内容正常
- [ ] 2 个 client_id 同时发指令，结果互不干扰
- [ ] 空 cmd 文件、截断 JSON 能被正确跳过（不报错）
- [ ] out 文件写入使用 .tmp → rename 原子操作
- [ ] events.txt 日志正常写入（spdlog，轮转）
- [ ] 崩溃后 NSSM / Task Scheduler 能自动重启

### 13.2 全量验收（交付前必须通过）

1. ✅ Bridge 服务开机自启，常驻内存
2. ✅ Hermes 能通过 cmd.txt → out.txt 调用 PowerShell 执行任意命令
3. ✅ Hermes 能读写 Windows 文件（UTF-8 中文正常）
4. ✅ 支持至少 3 个 client_id 并行通信，结果互不干扰
5. ✅ 线程池并行处理多条指令，无阻塞
6. ✅ HTTP GET/POST 访问本地 REST API 正常返回
7. ✅ Ollama 调用返回正确结果
8. ✅ 崩溃后 Task Scheduler/NSSM 自动拉起
9. ✅ events.txt 记录完整操作日志
10. ✅ 指令超时正确中断，不卡死 Bridge

---

*本文档由 Architect（DeepSeek）基于百事通需求文档第九节 + PM_REVIEW.md v2 生成*
*审核状态：✅ 通过，可进入实现阶段*
