# BUILD_STATUS.md

## 清理结果
- WinSW 服务 `Hermes Bridge (hermes-bridge)` 已停止
- 所有 hermes_bridge.exe 进程已终止
- 确认无残留进程

## V4 Bug 修复状态
- **文件**: `src/HttpServer.cpp`
- **修复**: 移除了调试用的 `LOG_INFO("[httpserver] content_length={}, MAX={}", ...)` 语句
- **说明**: content_length > 64KB 检查原本就在 JSON 验证之前（位于 Content-Type 检查之后、body 读取之前），此次仅清理了调试日志。70KB body 的处理流程为：
  1. Content-Type 检查
  2. content_length > 64KB 检查 → 413（正确位置）
  3. 读取完整 body
  4. validateJson → 400 Invalid JSON

## 编译状态
- **状态**: 编译成功
- **输出**: `C:\lobster\hermes_bridge\Release\hermes_bridge.exe`
- **大小**: 418,304 bytes
- **时间戳**: 2026/4/23 12:46:59

## 下一步说明
1. 启动服务进行验收测试：
   ```powershell
   C:\lobster\hermes_bridge\Release\winsw.exe start
   ```
2. 验收测试项：
   - 发送 70KB body → 预期返回 413 Payload Too Large（而非 400 Invalid JSON）
   - 发送 50KB 正常 JSON body → 预期返回 200 OK
   - 发送无效 JSON（< 64KB）→ 预期返回 400 Invalid JSON
