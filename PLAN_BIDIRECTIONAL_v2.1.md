# Hermes Bridge v2.1 Phase 1+2 执行计划

**项目路径：** `C:\lobster\hermes_bridge\`
**参考设计：** `DESIGN_BIDIRECTIONAL_v2.1.md`
**日期：** 2026-04-23
**状态：** 待实施

---

## 一、技术选型

### Phase 1：HTTP Server

**选型：手写轻量 HTTP Server（选项 A）**

- 不引入 cpp-httplib 等第三方依赖，保持单 exe
- 功能极简（仅 `POST /callback`），代码量 < 300 行
- 使用 Win32 API：socket → bind → listen，WSARecv/WSASend
- 使用 IOCP（`CreateIoCompletionPort`）处理并发，避免每连接一线程的开销
- 监听 `127.0.0.1:18900`，不对外暴露

### Phase 2：ImDisk 内存盘

**工具：ImDisk**（开源，SourceForge）
**挂载点：`R:\callbacks\`**，64MB NTFS RAM disk

---

## 二、Phase 1 任务拆解

### 任务 1.1：新增 `src/CallbackWriter.{h,cpp}`

**职责：** 原子写入 callbacks/ JSON 文件

**子任务：**
1. `CallbackWriter::init()` — 检测 ImDisk 是否可用，优先用 `R:\callbacks\`，fallback 到 `.\callbacks\`
2. `writeCallback(const std::string& json, const std::string& client)` — 写 `.tmp` → `FlushFileBuffers` → `MoveFileExA` 原子 rename
3. 文件名格式：`{timestamp}_{client}.json`

**验证方法：**
```powershell
# 直接单元测试：构造 json，写入，检查文件名和内容
# 不依赖 HTTP Server
```

---

### 任务 1.2：新增 `src/HttpServer.{h,cpp}`

**职责：** 接收 HTTP POST 请求，调用 CallbackWriter

**子任务（按执行顺序）：**

1. **初始化** — `HttpServer::start()` 在独立 std::thread 中运行 `listen()` 循环
2. **listen 循环** — `bind()` → `listen()` → `AcceptEx()` 或阻塞 `accept()`
3. **请求解析** — 读取 request line，校验：
   - 方法必须是 `POST`（否则 400）
   - Content-Type 必须是 `application/json`（否则 400）
   - Content-Length ≤ 65536（否则 413）
4. **接收 body** — `WSARecv()` 读取完整 body
5. **JSON 校验** — 尝试解析，不合法返回 400
6. **调用 CallbackWriter** — 写入成功返回 200，失败返回 500
7. **发送响应** — `WSASend()` 返回 HTTP 200/4xx/5xx

**新增常量（HttpServerConfig namespace）：**
```cpp
BIND_HOST       = "127.0.0.1"
BIND_PORT       = 18900
MAX_BODY_SIZE   = 65536   // 64KB
RECV_BUF_SIZE   = 65536 + 4096
```

**验证矩阵：**

| 验证项 | 操作 | 预期结果 |
|--------|------|---------|
| 正常 POST | `curl -X POST /callback -d {...}` | HTTP 200，写入 `callbacks/*.json` |
| GET 请求 | `curl -X GET /callback` | HTTP 400 |
| 非 application/json | `curl -X POST -H "Content-Type: text/plain"` | HTTP 400 |
| body = 64KB | dd 生成 64KB payload POST | HTTP 200 |
| body = 64KB+1 | dd 生成 64KB+1 payload POST | HTTP 413 |
| 非法 JSON | `curl -d "not json"` | HTTP 400 |

---

### 任务 1.3：改造 `src/main.cpp`

**改造点：**
1. `#include "HttpServer.h"`
2. 新增全局 `std::unique_ptr<HttpServer> g_httpServer`
3. `main()` 中调用 `g_httpServer->start()`（在现有初始化之后）
4. `main()` 退出前调用 `g_httpServer->stop()`
5. 服务 stop 时优雅关闭 HTTP Server

---

## 三、Phase 2 任务拆解

### 任务 2.1：安装 ImDisk

**步骤：**
1. 下载 ImDisk Toolkit（`imdisk-x64.exe`）from SourceForge
2. 管理员权限安装（静默：`/S` 参数）
3. 验证：`imdisk.exe -l` 应显示已注册驱动

**验证：** `imdisk.exe -l` 无报错

---

### 任务 2.2：编写 `mount_callbacks.bat`

**内容（见 DESIGN §3.3）：**
- 检查 `R:\callbacks\` 是否已存在，存在则直接启动 Bridge
- 不存在则执行 `imdisk.exe -a -t vm -m R: -s 64M -p "/FS:NTFS /Q /V:callbacks" -o rem`
- 挂载失败记录到 `logs\imdisk_error.log`，继续启动 Bridge（fallback）
- 创建 `R:\callbacks\` 目录（如不存在）
- 启动 `hermes_bridge.exe`

**NSSM 配置更新：**
```powershell
nssm set hermes_bridge Application "C:\lobster\hermes_bridge\mount_callbacks.bat"
nssm set hermes_bridge AppDirectory "C:\lobster\hermes_bridge"
```

**验证：** `nssm restart hermes_bridge` 后 `imdisk.exe -l` 显示 R: 盘

---

### 任务 2.3：Fallback 降级机制

**在 `CallbackWriter::init()` 中实现：**
1. 检测 `R:\callbacks\` 是否存在 → 存在则使用 ImDisk
2. 不存在 → fallback 到 `C:\lobster\hermes_bridge\callbacks\`，创建目录，继续运行
3. Bridge HTTP Server **永不在 ImDisk 缺失时主动崩溃**

**运行时 ImDisk 消失处理：**
- 检测到写入失败且返回 `ERROR_PATH_NOT_FOUND` → 尝试切换 fallback 路径再试一次
- 仍失败 → HTTP 503 Service Unavailable

---

## 四、验收标准检查清单

| # | 标准 | 验证命令 |
|---|------|---------|
| 1 | POST /callback 返回 200，写入 `callbacks/{timestamp}_{client}.json` | `curl -X POST http://127.0.0.1:18900/callback -H "Content-Type: application/json" -d "{...}"` → `ls R:\callbacks\*.json` |
| 2 | 非 POST 请求返回 405 | `curl -X GET http://127.0.0.1:18900/callback -i` → 检查状态码 |
| 3 | 请求体 > 64KB 返回 413 | `curl -X POST --data-binary @64KB_file http://127.0.0.1:18900/callback` → 检查 413 |
| 4 | ImDisk 挂载后 callbacks/ 在内存盘上 | `imdisk.exe -l` 显示 R: 且类型 vm；`fsutil fsinfo volumeinfo R:` |
| 5 | ImDisk 挂载失败 Bridge 不崩溃 | 卸载 ImDisk（`imdisk.exe -d R:`），重启服务，检查服务状态 RUNNING，日志有 WARN fallback 记录 |

---

## 五、工时估算

| 任务 | 阶段 | 预估工时 | 说明 |
|------|------|---------|------|
| CallbackWriter.{h,cpp} | Phase 1 | 1h | 原子写入逻辑，含 fallback 路径检测 |
| HttpServer.{h,cpp} | Phase 1 | 4h | 核心，IOCP/Accept 循环 + 请求解析 |
| main.cpp 改造 | Phase 1 | 0.5h | 启动/停止集成 |
| Phase 1 单元测试 | Phase 1 | 1h | curl 验证矩阵（表四） |
| ImDisk 安装脚本 | Phase 2 | 0.5h | install_imdisk.bat（含 /S 静默） |
| mount_callbacks.bat | Phase 2 | 0.5h | NSSM wrapper |
| NSSM 配置更新 | Phase 2 | 0.25h | 替换启动命令 |
| Phase 2 集成验证 | Phase 2 | 1h | fallback 降级验证 |
| **合计** | | **8.75h ≈ 2天** | |

---

## 六、文件变更清单

### 新增文件
```
src/HttpServer.h        # HTTP Server 类声明
src/HttpServer.cpp      # HTTP Server 实现（~250行）
src/CallbackWriter.h    # 回调写入器声明
src/CallbackWriter.cpp  # 原子写入实现（~100行）
mount_callbacks.bat     # NSSM wrapper
install_imdisk.bat      # ImDisk 静默安装脚本（可选）
```

### 修改文件
```
src/main.cpp            # 新增 HttpServer 启动/停止
install_service.ps1     # NSSM 启动命令改为 mount_callbacks.bat
```

### 新增目录
```
callbacks/              # fallback 目录（ImDisk 不可用时使用）
```

---

## 七、依赖关系

```
Phase 1 依赖：无（纯新增模块，不破坏现有功能）
Phase 2 依赖：Phase 1 完成后才能验证 callbacks/ 写入

Phase 2 部署顺序：
  1. 安装 ImDisk（install_imdisk.bat）
  2. 更新 NSSM 配置（mount_callbacks.bat）
  3. 重启服务
  4. 验证 ImDisk 挂载
  5. 验证 HTTP Server + callbacks 写入
```

---

*计划版本：v1.0 | 制定：Planner Agent | 日期：2026-04-23*
