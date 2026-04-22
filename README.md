# Hermes Bridge

Hermes Bridge 是运行在 **Windows 侧**的常驻桥接服务（Daemon），使运行在 WSL Ubuntu 中的 Hermes Agent 能够通过文件系统轮询机制调用 Windows 本地资源。

## 产品简介

| 项目 | 内容 |
|------|------|
| 项目名称 | Hermes Bridge |
| 类型 | C++ Windows 常驻服务（Daemon）|
| 核心功能 | Hermes（WSL）与 Windows 资源的桥接，通过 `cmd_*.txt` / `out_*.txt` 文件实现双向通信 |
| 编译目标 | `hermes_bridge.exe`（单一可执行文件）|
| 部署路径 | `C:\lobster\hermes_bridge\` |

### 技术栈

| 组件 | 选型 | 说明 |
|------|------|------|
| 编程语言 | C++17 | |
| 编译器 | MSVC 2022 x64 | |
| HTTP 客户端 | **WinHTTP** | Windows 原生，替代了原来的 libcurl |
| JSON 解析 | nlohmann/json | Header-only |
| 日志 | 自定义 RotatingFileLogger | 无需 spdlog |
| 线程池 | 自研 lightweight 实现 | <100 行 |
| 依赖管理 | 全部 Windows 原生 + header-only | 无需 vcpkg |

---

## 目录结构

```
C:\lobster\hermes_bridge\
├── hermes_bridge.exe       # 编译产物（单一可执行文件）
├── hermes_bridge.json      # 配置文件
├── build.bat               # 编译脚本
├── install_service.bat     # NSSM 服务注册脚本
├── README.md               # 本文件
├── include\                # Header-only 第三方库（无需安装）
│   └── nlohmann\json.hpp   # JSON 解析
├── src\                    # 源代码
│   ├── main.cpp
│   ├── Config.cpp/h
│   ├── Logger.cpp/h
│   ├── ThreadPool.cpp/h
│   ├── CommandQueue.cpp/h
│   ├── FileMonitor.cpp/h
│   ├── ResultWriter.cpp/h
│   ├── StateFile.cpp/h
│   └── handlers\
│       ├── IHandler.cpp/h
│       ├── ExecHandler.cpp/h     # exec action
│       ├── FileHandler.cpp/h     # file_read/write/patch
│       ├── HttpHandler.cpp/h     # http_get/post（WinHTTP）
│       ├── OllamaHandler.cpp/h   # ollama action
│       ├── ProcessHandler.cpp/h  # process_start/stop
│       └── ServiceHandler.cpp/h  # ps_service_query
├── build\                  # 编译临时目录（可删除）
├── logs\                   # 日志目录（spdlog 轮转）
│   └── events.txt         # 操作日志
├── cmd_*.txt               # 指令文件（Hermes 写，Bridge 读）
├── out_*.txt               # 结果文件（Bridge 写，Hermes 读）
└── state.json             # Bridge 自身状态
```

---

## 编译说明

### 环境要求

- **Visual Studio 2022 Build Tools**（含 C++ CMake 支持）或更高版本
- **CMake 3.15+**
- **Windows SDK**（含 WinHTTP 头文件，通常随 VS 安装）
- **NSSM**（用于注册 Windows Service，可选）

> ⚠️ 注意：本项目**已用 WinHTTP 替代 libcurl**，HTTP 请求直接使用 Windows 原生 WinHTTP API，无需安装 libcurl 或 vcpkg。

### 编译步骤

#### 方式一：直接运行编译脚本（推荐）

```powershell
cd C:\lobster\hermes_bridge
.\build.bat
```

编译完成后，`hermes_bridge.exe` 会在项目根目录生成。

#### 方式二：手动 CMake 编译

```powershell
# 进入项目目录
cd C:\lobster\hermes_bridge

# 清理旧构建目录（如有）
if (Test-Path build) { Remove-Item -Recurse build }

# 配置 CMake（使用 MSVC 2022 x64）
cmake -B build -S . -G "Visual Studio 17 2022" -A x64

# 编译 Release 版本
cmake --build build --config Release

# 复制到项目根目录
Copy-Item "build\Release\hermes_bridge.exe" .
```

> 💡 `-A x64` 指定生成 x64 目标平台，不可省略。

### 依赖说明

本项目**无需 vcpkg**，所有依赖均为 Windows 原生或 header-only：

| 依赖 | 类型 | 来源 | 说明 |
|------|------|------|------|
| WinHTTP | Windows 原生 | 系统内置 `winhttp.lib` | HTTP 客户端，已在源码中通过 `#pragma comment(lib, "winhttp")` 链接 |
| nlohmann/json | Header-only | `include/nlohmann/json.hpp` | JSON 解析，已包含在源码包中 |
| RotatingFileLogger | Header-only | `include/RotatingFileLogger.h` | 异步日志轮转，已包含在源码包中 |

---

## 部署说明

### 部署前检查清单

- [ ] 确认 `C:\lobster\hermes_bridge\` 目录存在
- [ ] 确认 `hermes_bridge.exe` 和 `hermes_bridge.json` 在部署目录
- [ ] 确认防火墙允许必要端口（如 Ollama 11434）
- [ ] 建议以管理员权限运行服务注册脚本

### hermes_bridge.json 配置说明

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

| 配置项 | 类型 | 默认值 | 说明 |
|--------|------|--------|------|
| `poll_interval_ms` | int | 5000 | FileMonitor 轮询间隔（毫秒）。建议值：1000-10000 |
| `worker_count` | int | 5 | 线程池 worker 数量，影响并发处理能力 |
| `default_timeout_sec` | int | 30 | 指令默认超时（秒）。超时后进程将被 TerminateProcess 终止 |
| `log_dir` | string | logs | 日志目录（相对路径，相对于 work_dir） |
| `log_file` | string | events.txt | 日志文件名 |
| `log_level` | string | info | 日志级别：`debug` / `info` / `warn` / `error` |
| `log_max_size_mb` | int | 10 | 单个日志文件最大大小（MB），超过后自动轮转 |
| `log_backup_count` | int | 5 | 保留的日志备份数量 |
| `work_dir` | string | — | **必填**。Bridge 工作目录，cmd/out 文件扫描位置 |
| `state_file` | string | state.json | 状态文件路径（相对于 work_dir） |
| `max_request_size_kb` | int | 10240 | cmd 文件最大大小（KB），超过此大小的文件将被跳过 |
| `ollama_url` | string | — | Ollama API 地址（末尾不带 `/`） |

### NSSM 服务注册（推荐）

> 💡 NSSM（Non-Sucking Service Manager）是 Windows 服务管理工具，比系统自带 SC 更易用。
> 如未安装 NSSM，可运行：`choco install nssm -y`

```powershell
# 管理员权限运行

# 安装服务
nssm install HermesBridge "C:\lobster\hermes_bridge\hermes_bridge.exe" ""

# 设置工作目录
nssm set HermesBridge AppDirectory "C:\lobster\hermes_bridge"

# 设置描述
nssm set HermesBridge Description "Hermes Bridge - Windows Resource Bridge for Hermes Agent"

# 设置开机自启
nssm set HermesBridge Start SERVICE_AUTO_START

# 崩溃后 5 秒自动重启
nssm set HermesBridge AppRestartDelay 5000

# 启动服务
net start HermesBridge

# 验证服务状态
sc query HermesBridge
type C:\lobster\hermes_bridge\state.json
```

**卸载服务**：
```powershell
net stop HermesBridge
nssm remove HermesBridge confirm
```

### Task Scheduler Fallback（无 NSSM 时）

如果 NSSM 不可用，可用 Windows 任务计划程序实现崩溃重启：

```powershell
# 创建任务计划（每分钟检查一次，如进程未运行则启动）
schtasks /create /tn "HermesBridge" `
    /tr "C:\lobster\hermes_bridge\hermes_bridge.exe" `
    /sc minute /mo 1 `
    /ru SYSTEM `
    /f

# 启动任务
schtasks /run /tn "HermesBridge"
```

> ⚠️ 此方案不如 NSSM 可靠，仅作为 fallback 使用。

### 验证部署

```powershell
# 检查服务状态
sc query HermesBridge

# 检查运行状态（state.json）
type C:\lobster\hermes_bridge\state.json

# 检查日志
type C:\lobster\hermes_bridge\logs\events.txt
```

---

## 使用说明

### cmd / out 文件机制

Hermes（WSL）与 Bridge（Windows）通过文件系统进行通信：

```
Hermes (WSL)  ──写──>  cmd_<client_id>.txt  ──读──>  Bridge (Windows)
Hermes (WSL)  <──读──  out_<client_id>.txt  <──写──  Bridge (Windows)
```

**文件命名规范**：
- 指令文件：`cmd_<client_id>.txt`（小写，client_id 由字母数字下划线组成）
- 结果文件：`out_<client_id>.txt`（与 cmd 一一对应）
- 示例：`cmd_main.txt` / `out_main.txt`、`cmd_agent1.txt` / `out_agent1.txt`

**通信流程**：
1. Hermes 写入 `cmd_<client_id>.txt`（JSON 格式）
2. Bridge FileMonitor 轮询检测到文件（每 `poll_interval_ms` 毫秒一次）
3. Bridge 解析 JSON，分发到线程池执行
4. Bridge 原子写入 `out_<client_id>.txt`（先写 `.tmp` 再 rename，防止读到半截数据）
5. Hermes 读取 `out_<client_id>.txt` 获取结果

> 💡 **原子写入保证**：Bridge 使用 `.tmp → rename` 模式，Hermes 要么读到旧结果（完整但过时），要么读到新结果（完整且最新），绝不可能读到半截数据。

### 支持的 action 列表

| action | 说明 | 关键参数 |
|--------|------|---------|
| `exec` | 执行 PowerShell / CMD / Python 命令 | `command`, `shell`（powershell/cmd/python）, `cwd`, `timeout` |
| `file_read` | 读取文件内容 | `path`, `offset`, `limit`, `encoding`（utf-8/gbk） |
| `file_write` | 写入文件（原子写入） | `path`, `content`, `encoding`, `append` |
| `file_patch` | 替换文件中的字符串 | `path`, `old_string`, `new_string`, `replace_all` |
| `process_start` | 启动独立进程 | `command`, `detached`（true=守护模式）, `cwd` |
| `process_stop` | 停止进程 | `name` / `pid` / `port`（三选一） |
| `http_get` | HTTP GET 请求（WinHTTP） | `url`, `timeout` |
| `http_post` | HTTP POST 请求（WinHTTP） | `url`, `json` / `body`, `headers`, `timeout` |
| `ollama` | 调用本地 Ollama API | `model`, `prompt`, `stream`, `options` |
| `ps_service_query` | 查询 Windows 服务状态 | `service_name` |

**cmd 文件 JSON 格式示例**（单条）：
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

**out 文件 JSON 格式示例**（成功）：
```json
{
  "cmd_id": "550e8400-e29b-41d4-a716-446655440000",
  "status": "ok",
  "result": {
    "stdout": "..." ,
    "stderr": "",
    "exit_code": 0,
    "killed": false
  },
  "duration_ms": 1234
}
```

**out 文件 JSON 格式示例**（错误）：
```json
{
  "cmd_id": "550e8400-e29b-41d4-a716-446655440000",
  "status": "error",
  "error": {
    "code": "EXEC_TIMEOUT",
    "message": "command timed out",
    "details": "Timeout after 30 seconds"
  },
  "duration_ms": 30001
}
```

### 错误码说明

| 错误码 | 说明 | 触发场景 |
|--------|------|---------|
| `INVALID_REQUEST` | 请求格式错误 | JSON 解析失败、缺少必填字段、未知 action |
| `FILE_NOT_FOUND` | 文件不存在 | `file_read` 目标文件不存在 |
| `FILE_WRITE_FAILED` | 文件写入失败 | 磁盘满、权限问题 |
| `EXEC_FAILED` | 命令执行失败 | 进程启动失败、非0退出码 |
| `EXEC_TIMEOUT` | 命令执行超时 | 超过 timeout 指定秒数 |
| `HTTP_ERROR` | HTTP 请求失败 | WinHTTP 返回错误 |
| `OLLAMA_ERROR` | Ollama 调用失败 | API 返回非 200 |
| `PROCESS_NOT_FOUND` | 进程未找到 | `process_stop` 目标进程不存在 |
| `SERVICE_NOT_FOUND` | 服务未找到 | `ps_service_query` 目标服务不存在 |
| `INTERNAL_ERROR` | Bridge 内部错误 | 未预期的异常 |

---

## 验收标准

### P0 验收（核心功能）

| 编号 | 验收项 | 状态 | 说明 |
|------|--------|------|------|
| V1 | Bridge 启动成功，state.json 显示 running | ✅ 已通过 | |
| V2 | exec action 能执行 PowerShell 命令并返回 stdout/stderr | ✅ 已通过 | Bug #1（CreateProcess code 2）已修复 |
| V3 | file_read / file_write 中文内容正常 | 待验证 | 需百事通环境 |
| V4 | 2 个 client_id 同时发指令，结果互不干扰 | ✅ 已通过 | Bug #2（client_id 格式）已修复 |
| V5 | 空 cmd 文件、截断 JSON 能被正确跳过（不报错） | 待验证 | 需百事通环境 |
| V6 | out 文件写入使用 .tmp → rename 原子操作 | ✅ 已通过 | 已在代码中实现 |
| V7 | events.txt 日志正常写入（轮转） | ✅ 已通过 | Bug #4（日志 action 为空）已修复 |
| V8 | 崩溃后 NSSM / Task Scheduler 能自动重启 | 待验证 | 需百事通环境 |

### 全量验收（交付前）

| 编号 | 验收项 | 状态 | 说明 |
|------|--------|------|------|
| V9 | Bridge 服务开机自启，常驻内存 | 待验证 | 需百事通环境 |
| V10 | Hermes 能通过 cmd.txt → out.txt 调用 PowerShell | ✅ 已通过 | |
| V11 | Hermes 能读写 Windows 文件（UTF-8 中文正常） | 待验证 | 需百事通环境 |
| V12 | 支持至少 3 个 client_id 并行通信，结果互不干扰 | ✅ 已通过 | main + test2 + test3 三个 client |
| V13 | 线程池并行处理多条指令，无阻塞 | ✅ 已通过 | 5 workers 线程池 |
| V14 | HTTP GET/POST 访问本地 REST API 正常返回 | ✅ 已通过 | httpbin.org 测试通过 |
| V15 | Ollama 调用返回正确结果 | 待验证 | 需百事通环境配置 Ollama |
| V16 | 崩溃后 Task Scheduler / NSSM 自动拉起 | 待验证 | 需百事通环境 |
| V17 | events.txt 记录完整操作日志 | ✅ 已通过 | |
| V18 | 指令超时正确中断，不卡死 Bridge | ✅ 已通过 | 超时使用 TerminateProcess |

> ⚠️ "待验证"项目需要部署在包含 Hermes Agent (WSL) 和 Ollama 的完整百事通环境中进行端到端验证。

---

## 已知问题修复记录

| Bug | 描述 | 修复版本 | 状态 |
|-----|------|---------|------|
| Bug #1 | exec action CreateProcess 返回 code 2（ERROR_FILE_NOT_FOUND） | v1.1 | ✅ 已修复 |
| Bug #2 | client_id 显示 `main.txt\0`（带 `.txt` 后缀和 null 字符） | v1.1 | ✅ 已修复 |
| Bug #3 | ok_requests 计数器发生 uint64 下溢（18446744073709551611） | v1.1 | ✅ 已修复 |
| Bug #4 | 日志 action 字段为空 `(action=)` | v1.1 | ✅ 已修复 |

---

*本文档由 Doc-writer Subagent 基于 SPEC.md + TEST_REPORT_V2.md 生成 | 2026-04-22*
