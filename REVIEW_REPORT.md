# Hermes Bridge — 代码审查报告

**审查日期**：2026-04-22（第二轮复查）
**审查人**：Reviewer（审查师）
**版本**：基于 SPEC.md v3 + PLAN.md v1.0
**结论**：✅ **通过（第二轮复查）**

---

## 一、总体评估

代码整体架构合理，核心逻辑（JSON 完整性检查、原子写入、多 client 并发框架）基本到位，错误处理覆盖面较广。第一轮审查发现的 6 个安全问题均已修复并验证通过。`hermes_bridge.exe` 已在项目根目录成功编译（340480 字节，2026-04-22 12:04）。

---

## 二、编译通过性

### 2.1 CMakeLists.txt ✅ 已确认修复

**问题**：原缺少 vcpkg 集成导致无法编译。

**修复验证**：`CMakeLists.txt` 已无 vcpkg 残留，纯源码编译，`hermes_bridge.exe` 已生成（340480 字节，2026-04-22 12:04:18）。

---

## 三、安全问题修复验证

### P0-1：ExecHandler 命令注入 ✅ 已确认修复

- **验证**：危险字符黑名单 `;|&$`()<>\/` + 换行/NULL 字节检验存在
- **验证**：`CreateProcessW` 使用 `lpApplicationName`（程序名）+ `lpCommandLine`（参数）分离参数，不经 shell 解释
- **建议**：黑名单策略存在被 Unicode 绕过理论风险，后续建议改为白名单（允许列表）

### P0-2：FileHandler 路径遍历 ✅ 已确认修复

- **验证**：`handleRead/handleWrite/handlePatch` 三处均检查 `..` 禁止路径遍历
- **验证**：相对路径强制拼接 `C:\lobster\hermes_bridge\workspace\`；绝对路径需不含 `..` 且以 `\` 或 `X:` 开头
- **建议**：workspace 路径硬编码在代码中，建议提取到 Config

### P0-3：CMakeLists.txt ✅ 已确认修复

- **验证**：`CMakeLists.txt` 无 vcpkg 残留，纯源码编译，`hermes_bridge.exe` 已生成

### P0-4：FileHandler 越界读取 ✅ 已确认修复

- **验证**：`handleRead` 读取前检查 `offset >= fileSize.QuadPart`，超界返回空内容而非整数下溢
- **验证**：`\\?\` 长路径前缀规范化保留

### P1-1：OllamaHandler 超时硬编码 ✅ 已确认修复

- **验证**：`timeout_sec = params.value("timeout", ctx.default_timeout)` 从 params 读取，有 30s 兜底

### P1-2：HTTPS 证书校验禁用 ✅ 已确认修复

- **验证**：`WinHttpOpenRequest` 调用时无额外 `WINHTTP_FLAG_IGNORE_*` 标志，使用 WinHTTP 默认证书验证

---

## 四、待处理非安全功能问题

以下问题不影响本次安全复查结论，仍记录在案：

| 编号 | 描述 | 文件 |
|------|------|------|
| S-3 | HttpHandler parseUrl 错误信息字段放错 | HttpHandler.cpp |
| G-1 | HttpHandler 未发送 Content-Length | HttpHandler.cpp |
| G-2 | HttpHandler 未返回响应头 headers | HttpHandler.cpp |
| G-3 | ResultWriter 写入失败无感知 | ResultWriter.cpp |
| G-4 | ProcessHandler port 参数空实现 | ProcessHandler.cpp |
| G-5 | ProcessHandler 精确匹配而非前缀匹配 | ProcessHandler.cpp |
| G-6 | ThreadPool busyCount 不更新 | ThreadPool.h |
| G-7 | StateFile 原子写入失败残留 .tmp | StateFile.cpp |
| G-8 | FileMonitor truncateFile 失败无感知 | FileMonitor.cpp |
| G-9 | makeLongPath 相对路径 + \\?\ 前缀问题 | FileHandler.cpp |
| P-1 | Logger 缺少 [client_id] 标签 | Logger.cpp |
| P-2 | RotatingFileLogger 轮转后缀 .bak 不一致 | RotatingFileLogger.h |
| P-3 | OllamaHandler total_duration_ms 是服务端数据 | OllamaHandler.cpp |
| P-5 | HttpHandler handleGet 缓冲区未预分配 | HttpHandler.cpp |

---

## 五、SPEC.md 符合性检查

| 检查项 | 状态 | 说明 |
|--------|------|------|
| JSON 完整性检查（isJsonComplete） | ✅ 符合 | 3 条规则与 SPEC Section 6.2 一致 |
| 原子写入（ResultWriter） | ✅ 符合 | CREATE_ALWAYS → Flush → MoveFileEx，失败时 DeleteFile |
| 原子写入（FileHandler handleWrite） | ✅ 符合 | 同上 |
| 多 client 并发框架 | ✅ 符合 | FileMonitor 扫描所有 cmd_*.txt，ResultWriter 按 client_id 写 |
| exec stdout/stderr 分离 | ✅ 符合 | 两个独立 pipe |
| exec 超时 TerminateProcess | ✅ 符合 | WAIT_TIMEOUT 后调用 TerminateProcess |
| file_read \\?\ 长路径 | ✅ 符合 | handleRead 使用 `\\?\` 前缀 |
| http_get/http_post 结果格式 | ⚠️ 部分符合 | 缺 headers 字段 |
| Ollama 结果格式 | ⚠️ 部分符合 | total_duration_ms 是服务端数据 |
| ps_service_query 结果格式 | ✅ 符合 | name/display_name/status/start_type/can_stop |

---

## 六、结论

**结论：✅ 通过（第二轮复查）**

本次复查逐一验证了第一轮发现的 6 个安全问题，确认全部修复有效：

1. ✅ P0-1：ExecHandler 命令注入 → 危险字符黑名单 + CreateProcessW 分离参数
2. ✅ P0-2：FileHandler 路径遍历 → 禁止 `..` + workspace 路径限制
3. ✅ P0-3：CMakeLists.txt → 无 vcpkg 残留，编译通过
4. ✅ P0-4：FileHandler 越界读取 → 读取前边界检查
5. ✅ P1-1：OllamaHandler 超时硬编码 → 从 params 读取
6. ✅ P1-2：HTTPS 证书校验禁用 → 恢复默认证书校验

**核心亮点**（保持）：
- JSON 完整性检查逻辑正确，边界 6 种用例处理符合 SPEC
- 原子写入 .tmp → rename → 失败 DeleteFile 流程正确
- 多 client 并发架构设计合理，无串扰
- ExecHandler 超时处理正确（TerminateProcess）
- 大部分 WinHTTP 调用有完整的错误处理和资源释放（CloseHandle）

---

*审查师 | 2026-04-22 | 第二轮复查 — 安全修复验证通过*
