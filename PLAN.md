# Hermes Bridge — 执行计划

**版本**：v1.0
**日期**：2026-04-22
**制定人**：Planner
**审核人**：Architect（龙虾小兵）
**状态**：待审核

---

## 一、接单条件确认（PM Review 纳管）

| # | 接单条件 | 落地位置 |
|---|---------|---------|
| C1 | JSON 完整性检查必须实现 | P0 / Section 4.1 |
| C2 | 原子写入必须落地 | P0 / Section 4.2 |
| C3 | P0 阶段必须包含多 client 并发框架 | P0 / Section 3 |

---

## 二、技术决策决议

### 2.1 崩溃自拉起方案：✅ NSSM（否决 Task Scheduler）

| 方案 | 决策 | 理由 |
|------|------|------|
| NSSM | ✅ 采用 | 崩溃后 5s 自动重启，可靠性高，注册为真实 Windows Service，支持 `sc query` 查看状态 |
| Task Scheduler | ❌ 不采用 | 最小粒度 1 分钟，中断窗口过长 |

**NSSM 注册命令**（部署时执行）：
```powershell
nssm install HermesBridge C:\lobster\hermes_bridge\hermes_bridge.exe ""
nssm set HermesBridge AppDirectory C:\lobster\hermes_bridge
nssm set HermesBridge Description "Hermes Bridge - Hermes Agent Windows Resource Bridge"
nssm set HermesBridge Start SERVICE_AUTO_START
nssm set HermesBridge AppRestartDelay 5000
net start HermesBridge
```

### 2.2 libcurl 静态链接方案：✅ vcpkg（否决手动）

| 方案 | 决策 | 理由 |
|------|------|------|
| vcpkg | ✅ 采用 | 一站式管理 libcurl + SSL(zlib) + 依赖，无需手动下载预编译包，SPEC.md 已有明确命令 |
| 手动下载 | ❌ 不采用 | SSL 版本不一致风险，维护成本高 |

**vcpkg 安装命令**：
```powershell
vcpkg install curl:x64-windows-static spdlog:x64-windows nlohmann-json:x64-windows
vcpkg integrate install
```

**编译选项**：`/MT`（静态 CRT）+ `/DYNAMICBASE:NO`（libcurl 要求）

---

## 三、任务拆解

### 阶段 0：环境准备（0.5d，不计入主要工时）

```
[0.1] 安装 VS Build Tools 2022 x64（含 CMake）
[0.2] 安装 vcpkg，vcpkg install curl:x64-windows-static spdlog:x64-windows nlohmann-json:x64-windows
[0.3] vcpkg integrate install
[0.4] 创建 C:\lobster\hermes_bridge\ 目录结构
[0.5] 验证 vcpkg libcurl 静态链接可编译（写一个 hello curl 程序验证）
```

**验收**：成功编译出 hello_curl.exe，运行时无 DLL 依赖报错。

---

### P0：基础能力（3–4 人天）

#### P0.1 项目骨架

```
[P0.1.1] 编写 CMakeLists.txt（MSVC x64 /MT，vcpkg 集成）
[P0.1.2] 编写 hermes_bridge.json 配置文件（含全部字段）
[P0.1.3] 编写 src/main.cpp（入口：解析配置、初始化日志、启动 FileMonitor + ThreadPool）
[P0.1.4] 编写 src/Config.cpp/h（配置加载和字段验证）
```

**验收**：编译通过，`hermes_bridge.exe -h` 或直接运行不报错，state.json 显示 running。

---

#### P0.2 线程池 + 命令队列

```
[P0.2.1] 编写 src/CommandQueue.cpp/h（线程安全队列，push/pop + mutex + condition_variable）
[P0.2.2] 编写 src/ThreadPool.cpp/h（5 workers，从队列取任务执行）
[P0.2.3] 验证：10 个并发任务全部完成，无丢失，worker 数量正确
```

**验收**：
- [ ] `total_requests` 从 0 开始
- [ ] 10 个并发任务执行后，`ok_requests` = 10
- [ ] workers 全部归还（`busy` → `idle`）

---

#### P0.3 FileMonitor（5s 轮询）

```
[P0.3.1] 编写 src/FileMonitor.cpp/h
        - 扫描 C:\lobster\hermes_bridge\cmd_*.txt
        - 使用 Windows LockFileEx 加锁读取
        - 动态发现新 client_id
[P0.3.2] 验证：创建 cmd_main.txt，5s 内检测到文件
[P0.3.3] 验证：3 个 client_id（main/agent1/agent2）同时存在时正确轮询
```

**验收**：
- [ ] FileMonitor 每 5s poll 一次，events.txt 有对应 log
- [ ] state.json 的 `clients` 列表包含所有发现的 client_id

---

#### P0.4 JSON 完整性检查（★★★ 接单条件 C1 ★★★）

```
[P0.4.1] 编写 src/Json完整性.cpp/h（或内联到 FileMonitor）
        - isJsonComplete()：空文件→跳过，纯空白→跳过，截断→跳过
[P0.4.2] 编写单元测试，验证以下 6 种情况全部正确判断：
          1. ""           → 不完整（空文件）
          2. "   "        → 不完整（纯空白）
          3. "{}"         → 完整
          4. "[{}]"       → 完整
          5. "{\"cmd_id\" → 不完整（截断）
          6. "not json"   → 不完整（非 JSON）
```

**验收**：
- [ ] 6 个边界用例全部通过（可目测 spdlog 输出确认 warn/ok 行为）
- [ ] events.txt 中截断 JSON 对应行记录 `[warn] Incomplete JSON, skipping`
- [ ] cmd 文件读完即 truncate 清空（不是 rename），Hermes 下一条指令不受影响

---

#### P0.5 exec Handler（PowerShell 执行）

```
[P0.5.1] 编写 src/handlers/ExecHandler.cpp
        - CreateProcessW 创建子进程
        - 捕获 stdout/stderr（分离 pipe）
        - 支持 timeout（默认 30s），超时 TerminateProcess
[P0.5.2] 验证：
          a. powershell.exe -Command "echo hello" → stdout="hello"
          b. 不存在的命令 → error_code=EXEC_FAILED
          c. sleep 60 + timeout=2 → error_code=EXEC_TIMEOUT，进程被 kill
```

**验收**：
- [ ] stdout/stderr 正确分离，exit_code 准确
- [ ] 超时进程被 TerminateProcess 杀死（Windows 任务管理器验证）
- [ ] events.txt 记录每次 exec 的 cmd_id 和 duration_ms

---

#### P0.6 file_read / file_write Handler（★★★ 接单条件 C2 ★★★）

```
[P0.6.1] 编写 src/handlers/FileHandler.cpp
        - FileReadHandler：CreateFileW + ReadFile，\\?\ 前缀长路径，offset/limit，encoding 参数
        - FileWriteHandler：先写 .tmp → rename 原子写入
[P0.6.2] 原子写入验证：
          a. file_write 写入内容，out_<client_id>.txt 存在且内容完整
          b. 写入过程中 Kill Bridge，磁盘上无残留 .tmp 文件
          c. Hermes 读 out 文件，要么读到旧结果，要么读到新结果，绝无半截
[P0.6.3] 中文内容验证：
          file_write {content: "中文测试ABC123"} → file_read 内容一致
```

**验收**：
- [ ] 20 次快速连续写入 + rename，零残留 .tmp 文件
- [ ] 中文 UTF-8 内容读写一致，无乱码
- [ ] 写入失败（只读路径）→ error_code=FILE_WRITE_FAILED

---

#### P0.7 多 client 并发框架（★★★ 接单条件 C3 ★★★）

```
[P0.7.1] FileMonitor 同时扫描所有 cmd_*.txt，入队到统一 CommandQueue
[P0.7.2] ThreadPool worker 取任务执行，不区分 client_id（不同 client 完全并行）
[P0.7.3] ResultWriter 按 client_id 分别写 out 文件（原子 rename）
[P0.7.4] 验证：
          a. cmd_main.txt 发 exec_a，cmd_agent1.txt 发 exec_b，同时执行
          b. out_main.txt 有 exec_a 结果，out_agent1.txt 有 exec_b 结果
          c. 结果互不覆盖，不串扰
```

**验收**：
- [ ] 3 个 client 同时发指令，out 文件各自独立，无串扰
- [ ] state.json 的 `clients` 包含 ["main", "agent1", "agent2"]

---

#### P0.8 日志系统（spdlog 异步 + 轮转）

```
[P0.8.1] 编写 src/Logger.cpp（spdlog 异步，10MB × 5 备份，events.txt）
[P0.8.2] 验证：运行 1 小时后，logs/ 目录有 events.txt.1 ~ events.txt.5 备份
```

**验收**：
- [ ] events.txt 单文件达到 10MB 后自动创建 .1 备份，保留 5 个
- [ ] 日志格式：`[2026-04-22 10:00:05.123] [info] [pool-1] [main] Enqueued cmd ...`

---

#### P0.9 崩溃自拉起（NSSM）

```
[P0.9.1] NSSM 注册 HermesBridge 服务
[P0.9.2] 验证：Kill Bridge 进程，5s 内服务自动重启，state.json 重新显示 running
```

**验收**：
- [ ] `net start HermesBridge` 成功
- [ ] `sc query HermesBridge` 显示 RUNNING
- [ ] Kill 进程后，5s 内 Bridge 重新运行（state.json status=running）

---

### P1：扩展能力（2 人天）

#### P1.1 HTTP Client（libcurl）

```
[P1.1.1] 验证 vcpkg libcurl 静态链接可正常使用
[P1.1.2] 编写 src/handlers/HttpHandler.cpp
        - HttpGetHandler：GET 请求，timeout，状态码，响应体
        - HttpPostHandler：POST JSON，Content-Type 自动设置
[P1.1.3] 验证：
          a. http_get http://127.0.0.1:8007/health → status_code=200
          b. http_post http://127.0.0.1:8007/... → 实际业务响应
```

**验收**：
- [ ] libcurl 静态链接（无 DLL 依赖），exe 可独立分发
- [ ] HTTP GET/POST 本地 REST API 正确返回 JSON 响应
- [ ] 响应时间 < 2s（1MB 以内 payload）

---

#### P1.2 Ollama Handler

```
[P1.2.1] 编写 src/handlers/OllamaHandler.cpp（封装 HttpPostHandler）
[P1.2.2] 验证：action=ollama {model:"qwen2.5", prompt:"hello"} → response 非空
```

**验收**：
- [ ] 连续 10 次 ollama 调用，response 字段正确解析，无 crash
- [ ] ollama 服务不可用时 → error_code=OLLAMA_ERROR，有可用错误信息

---

### P2：高级能力（1–2 人天）

```
[P2.1] 编写 src/handlers/ProcessHandler.cpp（process_start + process_stop）
       - process_start：detached=true（CREATE_NO_WINDOW），返回 pid
       - process_stop：按 name/pid/port 查找并 TerminateProcess
[P2.2] 编写 src/handlers/ServiceHandler.cpp（ps_service_query）
       - 调用 powershell Get-Service，返回 status/start_type
[P2.3] 验证：
         a. process_start 后，Windows 任务管理器可见对应进程
         b. process_stop 后，进程消失
         c. ps_service_query WinRM → status 字段正确
```

**验收**：
- [ ] process_start 启动独立进程，Bridge 不随之退出
- [ ] process_stop 正确杀进程，killed_pids 列表准确
- [ ] ps_service_query 返回的 status/start_type 与 `Get-Service` 一致

---

### 集成测试（2–3 人天）

```
[T1] Hermes → Bridge → PowerShell 端到端
     - Hermes 写 cmd_main.txt，Bridge 执行，Hermes 读到 out_main.txt 结果
     - 完整往返时延 < 6s（5s poll + 1s Hermes 轮询，不含指令执行时间）

[T2] 双 client 并发压力测试
     - main + agent1 + agent2 同时各发 5 条指令
     - 全部正确返回，无丢失，无串扰

[T3] 边界测试
     - 空 cmd 文件（跳过不报错）
     - 截断 JSON（跳过不报错，events.txt 有 warn）
     - 超大文件写入（>1MB，验证内存不爆）

[T4] 崩溃恢复测试
     - Kill Bridge，验证 NSSM 5s 内自动重启
     - 重启后 state.json 正确（status=running，uptime_seconds 重置）

[T5] 长时间运行测试
     - 连续运行 4h，内存占用稳定（< 50MB）
     - 日志文件正确轮转

[T6] NSSM 开机自启测试
     - 重启宿主机，Bridge 自动启动（无需人工介入）
```

**验收**：
- [ ] T1–T6 全部通过
- [ ] Hermes 能完整控制 Windows 资源（百事通第八节 10 条验收标准全部达成）

---

## 四、我们团队的验收标准（可测量）

> **说明**：以下不是百事通的验收标准，是**我们自己的**。每条标准均可通过脚本或人工验证。

### 4.1 P0 必须通过的验收标准（8 条）

| # | 验收标准 | 验证方法 |
|---|---------|---------|
| **V1** | Bridge 启动后 3s 内，state.json 存在且 `status=running` | `timeout 3 & type state.json`，验证 contains `"running"` |
| **V2** | exec action 执行 `echo test`，out 文件 2s 内返回 `stdout="test"` | 写 cmd_main.txt，计时，grep stdout |
| **V3** | 2 个 client（main + agent1）同时发指令，out 文件各自独立，内容互不污染 | 并发写 2 个 cmd 文件，验证各自的 out 文件内容对应正确 |
| **V4** | 6 种 JSON 边界用例（空/空白/截断/有效 JSON）处理符合预期（跳过/解析） | 查看 events.txt 中对应 warn/info 日志，验证逻辑正确 |
| **V5** | 原子写入：20 次快速写入后，磁盘无残留 `.tmp` 文件 | `Get-ChildItem *.tmp \| Measure-Object` == 0 |
| **V6** | 中文内容写入 UTF-8 文件，读出内容完全一致（字节级相等） | `file_write {content:"中文"}` → `file_read`，对比 content |
| **V7** | 进程超时 2s，Bridge 不卡死，正确返回 `error_code=EXEC_TIMEOUT` | `exec {command:"sleep 60", timeout:2}`，Bridge 保持响应 |
| **V8** | NSSM 崩溃后 5s 内 Bridge 重启，state.json 重新显示 `running` | Kill 进程，计时，轮询 state.json 直至恢复 |

### 4.2 P1 必须通过的验收标准（3 条）

| # | 验收标准 | 验证方法 |
|---|---------|---------|
| **V9** | http_get 访问本地 HTTP 接口，返回正确 status_code 和 body | `http_get {url:"http://127.0.0.1:8007/health"}`，验证 status_code=200 |
| **V10** | http_post 发送 JSON body，服务端正确接收并响应 | `http_post {url:"http://127.0.0.1:8007/echo", json:{"test":1}}`，验证回显正确 |
| **V11** | ollama action 连续 5 次调用，response 字段正确解析，无 crash | 循环 5 次，统计 error_count == 0 |

### 4.3 P2 必须通过的验收标准（3 条）

| # | 验收标准 | 验证方法 |
|---|---------|---------|
| **V12** | process_start 启动独立 python 进程，任务管理器可见 | `process_start {command:"python -c \"import time; time.sleep(30)\""}`，查 pid |
| **V13** | process_stop 按 name 杀进程，目标进程消失 | start python → stop by name，验证进程消失 |
| **V14** | ps_service_query WinRM，返回 status/start_type 与 PowerShell Get-Service 一致 | 对比 Bridge 返回值与 `Get-Service WinRM \| Select Status,StartType` |

### 4.4 集成测试验收标准（4 条）

| # | 验收标准 | 验证方法 |
|---|---------|---------|
| **V15** | Hermes 端到端：写 cmd → 等待 → 读 out，全流程 6s 内完成 | Hermes 脚本自动化，从写文件到读到结果计时 |
| **V16** | 3 client 并发 5 条指令（共 15 条），全部正确返回，零丢失 | 脚本并发注入，统计返回数量 |
| **V17** | 日志轮转：events.txt 达到 10MB 后，保留 5 个备份 | 填充日志至 10MB+，检查 .1~.5 文件存在 |
| **V18** | 内存占用：Bridge 空闲时 < 50MB（无指令积压时） | `Get-Process hermes_bridge \| Select-Object WorkingSet` |

---

## 五、技术难点识别

| 难点 | 描述 | 应对措施 |
|------|------|---------|
| **M1：WSL 文件锁不兼容** | Bridge 用 LockFileEx，但 Hermes 走 WSL 文件系统不过 Windows API | JSON 完整性检查兜底；Hermes 写 cmd 天然串行；截断 JSON 跳过不报错 |
| **M2：libcurl 静态链接编译** | SSL/zlib 依赖复杂，MSVC /MT 模式容易冲突 | vcpkg 一站式安装，明确 `/DYNAMICBASE:NO` 链接选项；先用 hello_curl 验证编译链 |
| **M3：中文路径 + 长路径** | Windows 中文路径 + 超过 260 字符路径 | 使用 `\\?\` 前缀 + `CreateFileW` wide char API；file_read/write handler 统一处理 |
| **M4：NSSM 与 MSVC /MT 兼容性** | NSSM 启动的进程若崩溃会导致 CRT 静态链接的 exe 出现 manifest 问题 | 先用 Task Scheduler 方案做 fallback；NSSM 注册时 AppRestartDelay=5000 避免频繁重启 |
| **M5：.tmp 文件清理** | rename 失败时若不删 .tmp 会留下垃圾文件 | ResultWriter 在 rename 失败路径明确 DeleteFile(tmp)；每次 poll 开始前清理本目录所有 .tmp 文件 |

---

## 六、依赖关系

```
环境准备（Section 0）
    ↓
P0.1 项目骨架 ──────────────────────────────────┐
    ↓                                              │
P0.2 线程池 + 命令队列                             │
    ↓                                              │
P0.3 FileMonitor（依赖 P0.2 队列）                  │ ← P0 整体可并行开发
    ↓                                              │
P0.4 JSON 完整性检查（依赖 P0.3 读取逻辑）           │
    ↓                                              │
P0.5 exec Handler（依赖 P0.2 线程池）              │
    ↓                                              │
P0.6 file_read/write（依赖 P0.4 JSON + 原子写入）   │
    ↓                                              │
P0.7 多 client 并发框架（依赖 P0.3/6）              │
    ↓                                              │
P0.8 日志系统（独立，可提前做）                      │
    ↓                                              │
P0.9 NSSM（依赖 P0.1 exe 可运行）                   │
    ↓
P0 交付验收（V1–V8）
    ↓
P1.1 HTTP Client（依赖 P0.1 exe 编译通过）           │
    ↓                                              │ ← P1 依赖 P0 交付
P1.2 Ollama Handler（依赖 P1.1）                    │
    ↓
P1 交付验收（V9–V11）
    ↓
P2（独立，可与 P1 并行）
    ↓
P2 交付验收（V12–V14）
    ↓
集成测试（T1–T6）
    ↓
V15–V18 验收
```

---

## 七、工时估算汇总

| 阶段 | 任务 | 预估人天 | 累计 |
|------|------|---------|------|
| 环境准备 | Section 0 | 0.5 | 0.5 |
| P0 | P0.1–P0.9 | 3–4 | 3.5–4.5 |
| P1 | P1.1–P1.2 | 2 | 5.5–6.5 |
| P2 | P2.1–P2.3 | 1–2 | 6.5–8.5 |
| 集成测试 | T1–T6 + 验收 V15–V18 | 2–3 | 8.5–11.5 |
| **合计** | | | **8–12 人天** |

---

## 八、交付物清单

```
C:\lobster\hermes_bridge\
├── CMakeLists.txt              # 构建配置
├── hermes_bridge.json         # 配置文件
├── README.md                   # 部署说明（含 NSSM 注册命令）
├── src\
│   ├── main.cpp
│   ├── Config.cpp/h
│   ├── CommandQueue.cpp/h
│   ├── ThreadPool.cpp/h
│   ├── FileMonitor.cpp/h
│   ├── ResultWriter.cpp/h
│   ├── StateFile.cpp/h
│   ├── Logger.cpp/h
│   └── handlers\
│       ├── IHandler.h
│       ├── ExecHandler.cpp
│       ├── FileHandler.cpp
│       ├── HttpHandler.cpp
│       ├── OllamaHandler.cpp
│       ├── ProcessHandler.cpp
│       └── ServiceHandler.cpp
└── hermes_bridge.exe          # 编译产物（交付）
```

---

## 九、后续动作

1. **本计划提交 Architect（龙虾小兵）审核**
2. **审核通过后**：Executor 领取 P0 任务，开始 Section 0 环境准备
3. **P0 完成后**：通知测试负责人编写自动化验收脚本（V1–V18）
4. **全量交付**：8–12 人天后，Hermes Bridge 可正式上线

---

*Planner 制定 | 待龙虾小兵审核*
