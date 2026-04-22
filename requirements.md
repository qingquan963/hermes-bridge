# Hermes Bridge 桥接服务需求文档

---

## 一、背景

### 1.1 角色定位
- **名称**：Hermes Agent（中文名：百事通）
- **角色**：唯一主编排，独立运行，和其他 agent（龙虾小兵、大白）平等
- **目标**：能够独立调度 Windows 本地资源，成为真正的主编排层

### 1.2 运行环境
| 项目 | 说明 |
|------|------|
| 运行环境 | WSL2 Ubuntu（Linux） |
| 启动方式 | systemd user service，daemon 化运行 |
| 微信接入 | ✅ 已接入（平台 adapter） |
| 核心数据 | 存储在 `C:\Users\Administrator\.hermes\`（Windows 文件系统） |
| 技能/记忆 | 33 个技能，SQLite state.db |

### 1.3 当前能力
- 记忆持久化 ✅
- 技能固化 ✅
- delegate_task（派生子 agent）✅
- cronjob（定时任务）✅
- terminal / file / search ✅
- web / browser ✅
- 微信接入 ✅

### 1.4 核心瓶颈
```
Hermes (WSL) ←———?———→ Windows 资源
     ↓微信                      ↑50+ 进程/Ollama/文件
  只能被动等消息                没有渠道触达 Hermes
```

**根本问题**：WSL 和 Windows 网络隔离，Hermes 无法直接 TCP 调用 Windows 上的资源。

**Hermes 能主动发指令的唯一通道**：写文件到 Windows（通过 WSL 挂载的 /mnt/c）

---

## 二、问题定义

Hermes 需要调用 Windows 本地资源，但缺乏持久、双向、实时的通信通道。

### 2.1 现有通道（不满足需求）
- **PowerShell subprocess**：临时执行命令，退出即销毁，不持久
- **文件写入**：单向，无返回通知机制，轮询效率低
- **微信消息**：被动触发，无法主动查询状态

### 2.2 理想通道应具备
1. **持久性**：服务常驻内存，不是临时进程
2. **实时性**：指令下发后能及时得到响应
3. **并发支持**：支持多条指令并行处理
4. **多客户端**：支持 Hermes 主agent 及多个子 agent 同时独立通信
5. **可观测**：运行时日志、状态可查

---

## 三、需求规格

### 3.1 桥接服务定位
- **执行环境**：Windows 原生（C++）
- **启动方式**：开机自启（Windows Task Scheduler 或 Startup LNK）
- **进程形态**：daemon 化，常驻内存，支持崩溃自拉起
- **与 Hermes 的关系**：Hermes 是主编排，桥接是 Hermes 的 Windows 资源触手

### 3.2 资源调用清单

| 资源类型 | 具体需求 | 优先级 |
|---------|---------|--------|
| PowerShell 命令执行 | 执行任意 PS 命令，返回 stdout/stderr | P0 |
| Python 进程管理 | 启动/停止/查询 Windows Python 进程 | P0 |
| 文件系统操作 | 读/写/修改 Windows 文件 | P0 |
| HTTP REST API | GET/POST 访问 Windows 上的 REST 接口 | P1 |
| Ollama LLM 调用 | 调用本地 Ollama（Windows 版）| P1 |
| 网络请求 | curl/wget 类功能，下载/上传 | P2 |
| Windows 服务查询 | 查询 Windows Service 状态 | P2 |

### 3.3 并发要求

**场景1：串行指令流**
```
Hermes 主agent → 指令A → 等待结果 → 指令B → 等待结果 → ...
```
单线程轮询处理即可。

**场景2：并行指令流**
```
子agent_1 → 指令A ─┐
子agent_2 → 指令B ─┼→ Bridge 并行处理
子agent_3 → 指令C ─┘
         ↓
各自返回结果到独立文件
```
多线程 worker pool 处理，指令间互不阻塞。

**并发数**：可配置，默认 5 个 worker

### 3.4 多客户端支持

每个 client（Hermes 主agent 或子 agent）使用独立通信文件：

```
C:\lobster\hermes_bridge\
├── cmd_<client_id>.txt    # 指令队列（客户端写，Bridge读）
├── out_<client_id>.txt    # 结果返回（Bridge写，客户端读）
├── events.txt            # 统一日志
└── state.json            # Bridge 自身状态
```

- client_id：Hermes 主agent 用 `main`，子 agent 用分配的独立 ID
- 文件锁机制：避免同时读写同一文件冲突
- 原子写入：写入前先写临时文件，再 rename，避免半截写入

### 3.5 心跳机制

- Bridge 每 5 秒检测 cmd_<client_id>.txt 有无新指令
- 新指令立即入队处理，worker 取走执行
- 结果写入 out_<client_id>.txt
- Hermes 轮询 out_<client_id>.txt（间隔可配置，默认 1 秒）

---

## 四、指令协议

### 4.1 指令格式（cmd.txt）

```json
{
  "cmd_id": "uuid-v4",
  "action": "exec|file_read|file_write|file_patch|process_start|process_stop|http_get|http_post|ollama|ps_service_query",
  "params": {
    // action-specific parameters
  },
  "timeout": 30,
  "timestamp": "2026-04-22T08:00:00Z"
}
```

### 4.2 支持的 action 详细说明

**exec** — PowerShell/Python 命令执行
```json
{
  "action": "exec",
  "params": {
    "command": "powershell.exe -Command \"Get-Process | Select-Object -First 5\"",
    "shell": "powershell",
    "cwd": "C:\\Users\\Administrator"
  }
}
```

**file_read** — 读取文件
```json
{
  "action": "file_read",
  "params": {
    "path": "C:\\Users\\Administrator\\test.txt",
    "offset": 0,
    "limit": 1000,
    "encoding": "utf-8"
  }
}
```

**file_write** — 写入文件（覆盖）
```json
{
  "action": "file_write",
  "params": {
    "path": "C:\\Users\\Administrator\\test.txt",
    "content": "hello world",
    "encoding": "utf-8"
  }
}
```

**file_patch** — 替换文件内容
```json
{
  "action": "file_patch",
  "params": {
    "path": "C:\\Users\\Administrator\\test.txt",
    "old_string": "hello",
    "new_string": "hi",
    "replace_all": false
  }
}
```

**process_start** — 启动进程
```json
{
  "action": "process_start",
  "params": {
    "command": "python C:\\lobster\\scripts\\start_lobster.bat",
    "detached": true
  }
}
```

**process_stop** — 停止进程
```json
{
  "action": "process_stop",
  "params": {
    "name": "python",
    "port": 8007
  }
}
```

**http_get** — GET 请求
```json
{
  "action": "http_get",
  "params": {
    "url": "http://127.0.0.1:8007/health",
    "timeout": 5
  }
}
```

**http_post** — POST 请求
```json
{
  "action": "http_post",
  "params": {
    "url": "http://127.0.0.1:8007/memories/add",
    "json": {"text": "hello", "metadata": {}},
    "timeout": 5
  }
}
```

**ollama** — 调用本地 Ollama
```json
{
  "action": "ollama",
  "params": {
    "model": "qwen2.5",
    "prompt": "你好",
    "stream": false
  }
}
```

**ps_service_query** — 查询 Windows 服务
```json
{
  "action": "ps_service_query",
  "params": {
    "service_name": "WinRM"
  }
}
```

### 4.3 响应格式（out.txt）

**成功：**
```json
{
  "cmd_id": "uuid-v4",
  "status": "ok",
  "result": {
    // action-specific result
  },
  "duration_ms": 123
}
```

**失败：**
```json
{
  "cmd_id": "uuid-v4",
  "status": "error",
  "error": {
    "code": "EXEC_FAILED",
    "message": "command timed out",
    "details": "..."
  },
  "duration_ms": 30001
}
```

---

## 五、错误处理

| 错误类型 | 处理策略 |
|---------|---------|
| 指令执行超时 | 杀子进程，返回超时错误 |
| 文件不存在 | 返回错误码 FILE_NOT_FOUND |
| 文件写入失败 | 原子写入回退，重试1次 |
| Bridge 崩溃 | Task Scheduler 自动重启（建议配置） |
| 指令格式错误 | 返回 ERROR_INVALID_REQUEST |
| 并发写入冲突 | 文件锁保护，丢弃旧指令（只处理最新） |

---

## 六、非功能需求

### 6.1 性能
- 指令延迟：从 cmd.txt 写入到结果返回 < 2 秒（不含指令本身执行时间）
- 并发：支持至少 5 条指令并行处理
- 内存占用：< 50MB
- CPU 空闲：< 1%（无指令时）

### 6.2 可观测性
- events.txt 记录所有操作（INFO/ERROR/WARN）
- state.json 暴露当前状态（worker 数量、队列长度、运行时长）
- 日志轮转：单文件最大 10MB，保留 5 个备份

### 6.3 部署
- 编译为单个可执行文件 `hermes_bridge.exe`
- 配置文件 `hermes_bridge.json`（worker 数量、超时默认值、日志路径）
- 部署目录：`C:\lobster\hermes_bridge\`
- 开机自启：Windows Task Scheduler（推荐）或 Startup LNK

---

## 七、架构图

```
┌─────────────────────────────────────────────────────────┐
│  Hermes (WSL Ubuntu)                                    │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐    │
│  │ 主 Agent    │  │ 子 Agent 1  │  │ 子 Agent 2  │    │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘    │
│         │                 │                 │            │
│         ↓                 ↓                 ↓            │
│    cmd_main.txt      cmd_agent1.txt    cmd_agent2.txt  │
└─────────┼───────────────┼───────────────┼──────────────┘
          ↓               ↓               ↓
┌─────────────────────────────────────────────────────────┐
│  Hermes Bridge (C++ Windows Daemon)                      │
│  ┌─────────────────────────────────────────────────┐   │
│  │ File Poller (5s interval) → Command Queue       │   │
│  │ Thread Pool (N workers) → Execute Concurrently  │   │
│  └─────────────────────────────────────────────────┘   │
│         ↓               ↓               ↓              │
│    PowerShell     Windows API   HTTP Client   Ollama  │
│         ↓               ↓               ↓              │
│    File System    Process Mgmt   REST APIs    LLM     │
└─────────────────────────────────────────────────────────┘
          ↓               ↓               ↓
    out_main.txt    out_agent1.txt    out_agent2.txt
```

---

## 八、验收标准

1. ✅ Bridge 服务开机自启，常驻内存
2. ✅ Hermes 能通过 cmd.txt → out.txt 调用 PowerShell 执行任意命令
3. ✅ Hermes 能读写 Windows 文件（UTF-8 中文正常）
4. ✅ 支持至少 3 个 client_id 并行通信，结果互不干扰
5. ✅ 线程池并行处理多条指令，无阻塞
6. ✅ HTTP GET/POST 访问本地 REST API 正常返回
7. ✅ Ollama 调用返回正确结果
8. ✅ 崩溃后 Task Scheduler 自动拉起
9. ✅ events.txt 记录完整操作日志
10. ✅ 指令超时正确中断，不卡死 Bridge

---

## 九、架构方案审核意见（2026-04-22）

### 9.1 审核结论

Architect 提交的架构方案（DESIGN.md + SPEC.md + PROJECT_STATUS.md）整体**通过**，技术选型合理，架构清晰，可以进入实现阶段。

### 9.2 技术选型确认

| 组件 | 选型 | 审核意见 |
|------|------|---------|
| HTTP 客户端 | libcurl 8.x 静态链接 | ✅ 通过 |
| JSON 解析 | nlohmann/json v3.10+ | ✅ 通过 |
| 线程池 | 自研 lightweight pool | ✅ 通过（无外部依赖，<100行）|
| 日志 | spdlog 异步+轮转 | ✅ 通过 |
| 编译器 | MSVC (VS Build Tools 2022) x64 /MT | ✅ 通过 |

### 9.3 架构流程确认

```
File Poller (5s) → Concurrent Queue → Thread Pool (5 workers)
                                          ↓
                      ┌──────────────────────────────────────┐
                      │ ExecHandler / FileHandler /          │
                      │ HttpHandler / OllamaHandler /         │
                      │ ServiceQueryHandler                  │
                      └──────────────────────────────────────┘
                                          ↓
                              Result Writer (atomic rename)
                                          ↓
                              out_<client_id>.txt
```

流程正确，支持多 client 并行，各自独立文件隔离。

### 9.4 关键风险及建议

#### 风险：WSL 文件锁与 Windows 文件锁兼容性

**前因后果**：

Bridge 运行在 Windows 侧，读写 `C:\lobster\hermes_bridge\cmd_main.txt` 等文件时使用 Windows 原生 API（`LockFileEx`）。

Hermes 运行在 WSL Ubuntu 侧，通过 `/mnt/c/...` 路径写同一个文件。WSL 的文件 I/O 走的是 WSL 文件系统层，**不经过 Windows 原生文件锁 API**。

这意味着：Bridge 的 `LockFileEx` 无法阻止 Hermes 的写入，理论上存在同时读写冲突的可能。

**实际情况分析**：

这个风险在 Hermes 实际使用场景下**影响有限**，原因：

1. Hermes 发指令是串行的：主 agent 自己发指令，收到结果再发下一条，不会并发写同一个 cmd 文件
2. 子 agent 各自有独立文件：`cmd_main.txt`、`cmd_agent1.txt`、`cmd_agent2.txt`，互不干扰
3. Bridge 读完 cmd 文件后清空，Hermes 收到结果才发下一条，顺序保证

因此 Hermes 写 cmd → Bridge 读 cmd → Bridge 写 out → Hermes 读 out 这个循环天然是串行的，不存在真正的并发写同一文件。

**Architect 的缓解措施**：

使用 Windows 原生 `LockFileEx` API，`LOCKFILE_EXCLUSIVE_LOCK` 进行独占写锁。这是正确的方向。

**建议补充的保护**：

在 ExecHandler / FileHandler 读取 cmd 文件时，增加 JSON 完整性检查：

- 读到空文件 → 跳过本次处理
- 读到内容被截断（不完整的 JSON）→ 跳过本次处理，等待下次轮询
- 写入 out 文件时使用原子 rename（先写 `.tmp` 再 rename），确保 Hermes 不会读到半截结果

这些保护在 SPEC.md 中已有部分体现，建议在实现阶段确保全部落地。

### 9.5 建议

方案可以开始写代码。实现顺序建议：

1. 先实现 exec + file_read + file_write 这三个 P0 基础 action，验证核心流程
2. 再实现 http_get/http_post + ollama
3. 最后补充 process_start/stop + ps_service_query

并发和多 client 支持在第一阶段就应纳入，避免后期重构。
