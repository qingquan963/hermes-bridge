# PM_REVIEW.md — Hermes Bridge 项目评审

**评审日期**：2026-04-22
**评审人**：项目经理（PM）
**版本**：v2（基于第九节新增内容重新评审）

---

## 一、需求确认结论

**结论：有条件接**

Architect 方案整体质量高，技术选型经过充分验证，可以进入实现阶段。

**接单条件（必须在实现前/中解决）**：
1. **JSON 完整性检查必须实现**：Bridge 读 cmd 文件时必须检测空文件、截断 JSON，发现不完整时跳过本次处理，不能 crash 或返回错误结果。
2. **原子写入必须落地**：out 文件写入必须先写 `.tmp` 再 rename，确保 Hermes 不会读到半截结果。
3. **P0 阶段必须包含多 client 并发框架**：避免后期重构成本。

---

## 二、项目范围定义

### 2.1 核心范围（必须交付）

| 模块 | 功能 | 优先级 |
|------|------|--------|
| Bridge Daemon | C++ Windows 常驻进程，崩溃自拉起 | P0 |
| File Poller | 5s 轮询 cmd_<client_id>.txt | P0 |
| Thread Pool | 5 workers，支持指令并行 | P0 |
| exec action | PowerShell 命令执行 | P0 |
| file_read action | 文件读取（UTF-8 中文正常）| P0 |
| file_write action | 文件写入（原子 rename）| P0 |
| HTTP client | libcurl，支持 GET/POST | P1 |
| ollama action | 调用本地 Ollama | P1 |
| process management | 启动/停止 Windows 进程 | P2 |
| ps_service_query | 查询 Windows 服务状态 | P2 |

### 2.2 交付物清单

```
C:\lobster\hermes_bridge\
├── hermes_bridge.exe          # 编译产物
├── hermes_bridge.json         # 配置文件
├── cmd_<client_id>.txt         # 指令队列文件（Hermes 写，Bridge 读）
├── out_<client_id>.txt         # 结果返回文件（Bridge 写，Hermes 读）
├── events.txt                  # 操作日志
├── state.json                  # Bridge 自身状态
└── README.md                   # 部署说明
```

### 2.3 明确排除范围

- 微信 adapter（已有，不在本次交付）
- WSL 侧 Hermes 代码修改（Hermes 现有轮询逻辑不变）
- 移动端 / 非 Windows 平台支持
- 指令重试机制（本次不做，失败直接返回错误）

---

## 三、初步工作量评估

### 3.1 分阶段评估

| 阶段 | 内容 | 预估人天 |
|------|------|---------|
| **P0** | Bridge 骨架 + 线程池 + exec/file_read/file_write | 3–4 人天 |
| **P1** | HTTP client + ollama handler | 2 人天 |
| **P2** | process_start/stop + ps_service_query | 1–2 人天 |
| **测试 + 集成** | 端到端联调，Hermes 联调，边界测试 | 2–3 人天 |
| **文档 + 部署** | README、配置文件、Task Scheduler 配置 | 0.5 人天 |
| **合计** | | **8–12 人天** |

### 3.2 关键技术点工时预估

| 技术点 | 风险等级 | 预估 |
|--------|---------|------|
| libcurl 静态链接 + Windows 编译 | 中 | 1 人天（含踩坑）|
| spdlog 异步日志 + 轮转 | 低 | 0.5 人天 |
| nlohmann/json 高性能解析 | 低 | 0.5 人天 |
| JSON 完整性检查 | 中 | 0.5 人天（需仔细设计边界）|
| 原子 rename 写入 | 低 | 0.5 人天 |
| 多 client 文件隔离 | 低 | 已明确在架构中 |

---

## 四、风险项

### 4.1 🔴 高风险

**R1：WSL 文件锁与 Windows 文件锁兼容性**

- **描述**：Bridge 用 Windows `LockFileEx` 加锁，Hermes 通过 `/mnt/c/` 写文件走 WSL 文件系统层，不经过 Windows 文件锁 API。理论上存在同时读写冲突的可能。
- **缓解**：Section 9 分析指出实际场景影响有限（串行写、文件隔离），但 JSON 完整性检查必须实现兜底。
- **必须落地**：
  - [ ] 读 cmd 文件前检查文件非空
  - [ ] 读入内容必须是可以解析的完整 JSON（截断则跳过）
  - [ ] 写入 out 文件先写 `.tmp` 再 rename
- **责任人**：Architect + 实现者

### 4.2 🟡 中风险

**R2：libcurl 静态链接 Windows 依赖**

- **描述**：libcurl 8.x 静态链接需要处理 SSL（OpenSSL/WinSSL）、zlib 等依赖，在 Windows 环境编译复杂。
- **建议**：使用 vcpkg 管理依赖，或在 DESIGN.md 中明确静态链接方案（含依赖列表）。
- **责任人**：Architect

**R3：崩溃自拉起的可靠性**

- **描述**：Section 9 建议用 Task Scheduler 自动重启，但 Windows Task Scheduler 最短粒度是 1 分钟，可能导致 1 分钟服务中断。
- **建议**：增加 Watchdog 进程双保险，或使用 NSSM（Non-Sucking Service Manager）将 Bridge 注册为 Windows Service。
- **责任人**：Architect

### 4.3 🟢 低风险

**R4：Hermes 轮询间隔与 Bridge 处理速度的配合**

- **描述**：Bridge 5s 轮询，Hermes 1s 轮询 out 文件，整体响应时间最多约 6s（非指令执行时间）。
- **评估**：满足 Section 6.1 性能要求（< 2s 延迟不含指令执行），低风险。

**R5：中文文件名的文件操作**

- **描述**：Windows 中文路径需要 UTF-8 支持，C++ 侧注意使用 wide char API。
- **评估**：风险可控，实现 file_read/file_write 时注意 encoding 参数处理即可。

**R6：多 client 并发写同一文件**

- **描述**：每个 client 独立文件（cmd_main.txt / cmd_agent1.txt 等），天然隔离，无竞争。
- **评估**：低风险。

---

## 五、实现顺序建议

### 5.1 总体顺序（参考 Section 9.5）

```
P0 基础能力 ─────────────────────────────────────────────────
│
├─ 1.1 项目骨架（CMake + 编译配置，MSVC x64 /MT）
├─ 1.2 hermes_bridge.json 配置文件解析
├─ 1.3 线程池（5 workers，无外部依赖）
├─ 1.4 File Poller（5s 轮询 cmd_<client_id>.txt）
├─ 1.5 exec handler（PowerShell 执行）
├─ 1.6 file_read handler（UTF-8 读取）
├─ 1.7 file_write handler（原子 rename）
├─ 1.8 JSON 完整性检查（★★★ 必须在 P0 落地 ★★★）
├─ 1.9 out 文件原子写入（.tmp → rename）
└─ 1.10 events.txt 日志（spdlog，轮转 10MB×5）
      └─ 多 client 并发框架（必须随 P0 一起做，不是后面补充）
│
P1 扩展能力 ─────────────────────────────────────────────────
│
├─ 2.1 HTTP client（libcurl，GET + POST）
├─ 2.2 http_get handler
├─ 2.3 http_post handler
└─ 2.4 ollama handler
│
P2 高级能力 ─────────────────────────────────────────────────
│
├─ 3.1 process_start handler
├─ 3.2 process_stop handler
└─ 3.3 ps_service_query handler
│
集成 + 部署 ─────────────────────────────────────────────────
│
└─ 4.1 端到端联调（Hermes → Bridge → Windows 资源）
     4.2 Task Scheduler / NSSM 配置开机自启
     4.3 README.md + 部署文档
```

### 5.2 P0 阶段验收检查点

进入 P1 前必须确认：
- [ ] Bridge 启动成功，state.json 显示 running
- [ ] exec action 能执行 PowerShell 命令并返回结果
- [ ] file_read / file_write 中文内容正常
- [ ] 2 个 client_id 同时发指令，结果互不干扰
- [ ] 空 cmd 文件、截断 JSON 能被正确跳过（不报错）
- [ ] 崩溃后 Task Scheduler / Watchdog 能自动重启

---

## 六、其他建议

1. **先做 P0 的 MVP 验证**：用最简单的方式验证 Hermes → Bridge → PowerShell 命令的执行链路，再逐步扩展。
2. **测试用例覆盖 JSON 完整性检查**：设计边界测试用例（空文件、纯空格、截断 JSON、有效 JSON），确保跳过逻辑正确。
3. **NSSM vs Task Scheduler**：推荐 NSSM 将 Bridge 注册为真实 Windows Service，可靠性高于 Task Scheduler，且支持立即重启。
4. **libcurl 依赖**：建议在 DESIGN.md 中明确是 vcpkg 安装还是手动下载预编译包。

---

## 七、总结

| 项目 | 结论 |
|------|------|
| 需求确认 | **有条件接** |
| 范围 | 明确，无重大遗漏 |
| 工作量 | 8–12 人天 |
| 最大风险 | WSL/Windows 文件锁 + JSON 完整性 |
| 实现顺序 | P0 → P1 → P2 |
| 下一步 | Architect 确认实现方案，启动 P0 开发 |

---

## 八、P1 问题修复记录（2026-04-23）

| 问题 | 描述 | 修复方案 | 状态 |
|------|------|---------|------|
| **P1-1** | `|` 和 `;` 误杀正常 PowerShell 命令 | `ExecHandler.cpp` 中 shell 模式下移除 `|` 字符（管道在 PowerShell 中合法）；`;` 在 shell 模式下本来就不拦截 | ✅ 已修复 |
| **P1-2** | state.json 统计数据不准（显示 2，实际 6）| 经代码审查，`runCommandHandler` 中 `g_total_requests`/`g_errors` 计数器逻辑正确。差异原因：Hermes 发送 6 个文件时含重复 cmd_id，被 FileMonitor 去重（dedup）机制过滤，实际只处理 2 个唯一命令。计数器按设计工作，无代码 bug | ✅ 确认无 bug |

**P1-1 修复详情**：
- 修改文件：`src/handlers/ExecHandler.cpp` 第 56 行
- 修改前（shell 模式）：`dangerous = "&|`()<>\\"`（含 `|`）
- 修改后（shell 模式）：`dangerous = "&`\\()<>"`（移除 `|`）
- 直接模式（`shell="none"`）保持不变：`";&|`()<>\\"`
- 注释已更新，明确说明 `|` 和 `;` 在 shell 模式下允许

**P1-2 分析结论**：
- `g_total_requests` / `g_errors` 在 `runCommandHandler` 中每处理一个命令递增
- 主循环每 5 秒调用 `g_state->update()` 将计数器值写入 state.json
- 无代码路径会在不调用 `runCommandHandler` 的情况下消耗命令
- 6 vs 2 的差异由 FileMonitor 去重逻辑导致：Hermes 发送的 6 个 cmd 文件中含 4 个重复 cmd_id（相同 cmd_id + `force=false`），被跳过
- 如需追踪被跳过的重复命令数，可新增 `g_skipped_dedup` 计数器
