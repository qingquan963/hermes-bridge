# Hermes Bridge v2.1 �?双向实时通道架构设计

**版本�?* v2.1
**日期�?* 2026-04-23
**状态：** Phase 1 + Phase 2 已实现

---

## 1. 概述

�?Hermes Bridge v2.0 基础上，新增 **HTTP Server 反向通道模块**，实�?Bridge �?Hermes 的毫秒级实时通知，绕�?5 秒轮询瓶颈�?
### 整体架构

```
龙虾小兵（Windows�?    �?POST http://127.0.0.1:18900/callback
    �?Bridge HTTP Server（端�?18900，hermes_bridge.exe 内）
    �?写入原子操作
    �?callbacks/（ImDisk 内存盘，R:\callbacks\�?    �?inotify 事件（WSL2�? 10ms�?    �?WSL2 inotify 监听器（Python�?    �?Unix socket �?cmd_hermes.txt fallback
    �?Hermes 主程�?```

**端到端目标延迟：** < 50ms

---

## 2. Phase 1：Bridge HTTP Server

### 2.1 实现选型

**决策：选项A �?手写轻量 HTTP Server**

理由�?- 避免引入 cpp-httplib 等第三方依赖
- 保持�?exe（不增加运行时依赖）
- 所需功能极简（仅 POST /callback），手写代码�?< 300 �?- 现有代码库已是纯 C++ Win32，手写风格完全一�?
### 2.2 端口绑定

```
监听地址�?27.0.0.1:18900
不对外暴露，不监�?0.0.0.0
```

### 2.3 接口定义

#### POST /callback

**请求�?*
```
POST /callback HTTP/1.1
Host: 127.0.0.1:18900
Content-Type: application/json
Content-Length: <n>

{
  "from": "lobster",
  "type": "task_update | error | knowledge | info",
  "content": "具体内容",
  "timestamp": 1745396400000,
  "client": "lobster"
}
```

**响应（成功）�?*
```
HTTP/1.1 200 OK
Content-Type: application/json

{"status": "ok"}
```

**响应（失败）�?*
```
HTTP/1.1 400 Bad Request          �?Content-Type 错误或非 POST
HTTP/1.1 413 Payload Too Large    �?body > 64KB
HTTP/1.1 500 Internal Server Error �?写入失败
Content-Type: application/json
{"status": "error", "message": "..."}
```

### 2.4 校验规则

| 校验�?| 失败时返�?|
|--------|-----------|
| HTTP 方法必须�?POST | 400 |
| Content-Type 必须�?application/json | 400 |
| Content-Length �?65536 字节 | 413 |
| body 可解析为 JSON | 400 |

### 2.5 文件写入流程

```
1. 验证请求（方�?Content-Type/大小�?2. 生成文件名：{timestamp}_{client}.json
   - timestamp：GetSystemTimeAsFileTime() / 10000（毫秒级�?   - client：从 JSON 字段读取，非法则�?"unknown"
3. 写入 R:\callbacks\{filename}.tmp（ImDisk 内存盘）
4. FlushFileBuffers + CloseHandle
5. MoveFileExA(.tmp, .json, MOVEFILE_REPLACE_EXISTING)  �?原子 rename
6. 返回 HTTP 200
```

**关键点：** 写完即返�?200�?*不等�?Hermes 确认**（完全异步）�?
### 2.6 新增源文�?
```
src/
  ├── HttpServer.h       # HTTP Server 类声�?  ├── HttpServer.cpp     # HTTP Server 实现�? 300 行）
  ├── CallbackWriter.h   # 回调文件写入�?  └── CallbackWriter.cpp # 原子写入逻辑
```

### 2.7 main.cpp 改�?
```cpp
// 新增：启�?HTTP Server
#include "HttpServer.h"

static std::unique_ptr<HttpServer> g_httpServer;

int main(int argc, char* argv[]) {
    // ... 现有初始�?...

    // 启动 HTTP Server（独立线程，不阻塞主循环�?    g_httpServer = std::make_unique<HttpServer>(
        "127.0.0.1", 18900,
        g_config.work_dir + "\\callbacks"
    );
    g_httpServer->start();
    LOG_INFO("HTTP Server started on 127.0.0.1:18900");

    // ... 现有主循�?...
}
```

### 2.8 与现�?FileMonitor 的共存关�?
| 模块 | 职责 | 状�?|
|------|------|------|
| FileMonitor | 轮询 cmd_*.txt �?Bridge �?Hermes | 不变，继续运�?|
| HttpServer | 接收 POST �?�?callbacks/ �?Hermes | 新增，独立运�?|

两�?*完全独立**，无共享状态，无互相依赖�?
---

## 3. Phase 2：ImDisk 内存�?
### 3.1 工具选型

**工具：ImDisk**（开源，https://sourceforge.net/projects/imdisk-toolkit/�?
> ⚠️ 注意：AROM 是常见笔误，正确工具名为 **ImDisk**

### 3.2 挂载方案

**ImDisk 挂载参数�?*
```
imdisk.exe -a -t vm -m R: -s 64M -p "/FS:NTFS /Q /V:callbacks" -o rem
```

- 挂载点：`R:\callbacks\`�?4MB RAM disk�?- `-o rem`：系统重启后自动卸载（防�?stale mount�?- 文件系统：NTFS（兼�?Windows 文件锁）

**目录结构�?*
```
R:\callbacks\
  └── *.json   # 每次回调一个文件，inotify 处理后即�?```

### 3.3 NSSM 启动脚本

现有 NSSM service 启动的是 `hermes_bridge.exe`。在 Bridge 启动前，需先挂�?ImDisk�?
**方案：创建一�?wrapper batch 脚本**

```batch
@echo off
REM ========================================
REM mount_callbacks.bat
REM NSSM 启动前先执行此脚本挂�?ImDisk
REM ========================================

REM 检�?ImDisk 是否已挂�?if exist R:\callbacks\ goto :bridge_start

REM 挂载 ImDisk 内存盘（64MB�?imdisk.exe -a -t vm -m R: -s 64M -p "/FS:NTFS /Q /V:callbacks" -o rem
if errorlevel 1 (
    echo [%date% %time%] ImDisk mount failed >> "C:\lobster\hermes_bridge\logs\imdisk_error.log"
    goto :bridge_start
)

REM 创建 callbacks 目录（ImDisk 格式化为空）
if not exist R:\callbacks mkdir R:\callbacks

:bridge_start
REM 启动 Bridge（相对路径，NSSM workdir 设为 C:\lobster\hermes_bridge�?hermes_bridge.exe
```

**NSSM 重新配置�?*
```powershell
# 替换 NSSM 启动命令
nssm set hermes_bridge Application "C:\lobster\hermes_bridge\mount_callbacks.bat"
nssm set hermes_bridge AppDirectory "C:\lobster\hermes_bridge"
```

### 3.4 Fallback 策略（ImDisk 挂载失败�?
**目标�?* Bridge HTTP Server **不崩�?*，降级到普通磁盘�?
| 场景 | HTTP Server 行为 | 日志 |
|------|-----------------|------|
| ImDisk 未挂�?| 检�?`R:\callbacks\` 不存�?�?回退�?`C:\lobster\hermes_bridge\callbacks\` | WARN: ImDisk not available, falling back to disk |
| ImDisk 挂载失败 | 继续启动 HTTP Server，写�?fallback 目录 | ERROR: ImDisk mount failed: <reason> |
| 磁盘空间不足 | 写入失败 �?HTTP 500 | ERROR: Failed to write callback file: <reason> |

**检测逻辑（CallbackWriter 初始化时）：**
```cpp
bool CallbackWriter::init() {
    // 优先使用 ImDisk
    if (PathFileExistsA("R:\\callbacks")) {
        base_path_ = "R:\\callbacks\\";
        LOG_INFO("Using ImDisk: R:\\callbacks");
    } else {
        base_path_ = work_dir_ + "\\callbacks\\";
        if (!PathFileExistsA(base_path_.c_str())) {
            CreateDirectoryA(base_path_.c_str(), NULL);
        }
        LOG_WARN("ImDisk not available, falling back to: " + base_path_);
    }
    return true;
}
```

### 3.5 内存�?vs 物理盘对�?
| 指标 | ImDisk 内存�?| 物理硬盘 |
|------|-------------|---------|
| 读写延迟 | < 1ms | ~10ms |
| 写入速度 | ~5 GB/s | ~100 MB/s |
| 数据持久�?| 断电丢失 | 持久 |
| CPU 占用 | 极低 | �?|
| 适合场景 | 临时文件，中�?| 长期存储 |

callbacks/ 文件生命周期：写�?�?inotify 捕获 �?Hermes 读取 �?**删除**，全�?< 1 秒，内存盘完全满足需求�?
---

## 4. 错误处理策略

### 4.1 HTTP Server 错误处理

| 错误类型 | 处理方式 | 返回�?|
|---------|---------|--------|
| �?POST 请求 | 记录 client IP（均�?127.0.0.1），返回错误 | 400 |
| Content-Type 错误 | 同上 | 400 |
| body �?64KB | 记录 Content-Length | 413 |
| JSON 解析失败 | 记录原始 body（前 200 字节�?| 400 |
| 写入失败 | 记录 Win32 GetLastError() | 500 |
| Accept() 失败 | 记录错误，重�?1 秒后继续 | N/A |

### 4.2 文件写入错误处理

| 错误类型 | 处理方式 |
|---------|---------|
| 父目录不存在 | 尝试 CreateDirectory，失败则记录并返�?500 |
| .tmp 写入失败 | 记录错误，返�?500 |
| rename 失败 | 尝试删除目标文件�?rename，仍失败则返�?500 |
| 磁盘空间不足 | GetLastError() == ERROR_DISK_FULL，记录并返回 500 |

### 4.3 ImDisk Fallback 错误处理

```
启动阶段�?  ImDisk 挂载失败 �?LOG_ERROR �?继续启动（降级到磁盘�?
运行阶段�?  R:\callbacks\ 消失 �?LOG_ERROR �?HTTP Server 暂停接收（返�?503�?  等待管理员手动恢复或重启服务
```

### 4.4 日志分类

| 日志级别 | 触发条件 |
|---------|---------|
| INFO | HTTP Server 启动/停止、ImDisk 挂载成功、文件写入成�?|
| WARN | ImDisk 不可用降级到磁盘、disk fallback active |
| ERROR | 挂载失败、写入失败、HTTP 处理异常 |

---

## 5. 文件结构

```
C:\lobster\hermes_bridge\
├── hermes_bridge.exe        # Bridge 主程序（v2.1，含 HTTP Server�?�?├── DESIGN_BIDIRECTIONAL_v2.1.md
�?├── mount_callbacks.bat      # 【新增】NSSM wrapper：挂�?ImDisk 后启�?Bridge
�?├── callbacks/                 # 【新增，fallback 目录�?�?  └── *.json                # 降级时使用（ImDisk 挂载失败时）
�?├── src/
�?  ├── HttpServer.h          # 【新增】HTTP Server 头文�?�?  ├── HttpServer.cpp        # 【新增】HTTP Server 实现
�?  ├── CallbackWriter.h     # 【新增】回调写入器头文�?�?  ├── CallbackWriter.cpp   # 【新增】原子写入实�?�?  ├── main.cpp              # 【改造】新�?HttpServer 启动/停止
�?  └── ...
�?├── include/                   # （现有头文件�?�?  └── ...
�?├── logs/
�?  └── events.txt            # 现有日志
�?├── cmd_*.txt                 # 现有：Hermes �?Bridge 命令通道
├── out_*.txt                 # 现有：Bridge �?Hermes 结果通道
�?└── NSSM 安装配置/
    ├── install_service.bat   # 现有
    └── install_service.ps1   # 现有（需更新启动命令�?
WSL2 侧（不在本设计范围内，Phase 3）：
  ~/.hermes/
    ├── callback_queue/       # Hermes 未就绪时消息暂存
    └── inotify_listener.py   # inotify 监听�?```

---

## 6. 向后兼容�?
| 组件 | 兼容性保�?|
|------|-----------|
| cmd_*.txt 轮询 | **完全不变**，FileMonitor 继续运行 |
| out_*.txt 写入 | **完全不变**，ResultWriter 继续运行 |
| NSSM service | 启动命令改为 mount_callbacks.bat，参数不�?|
| 配置文件 | 无新增配置项（全部硬编码或编译期常量�?|
| API 接口 | 无变化（现有 action handler 不变�?|

---

## 7. HTTP Server 实现要点（手写）

### 7.1 核心组件

```
HttpServer
  ├── listenSocket (SOCKET, WSASocket with WSA_FLAG_OVERLAPPED)
  ├── acceptThread (std::thread)
  ├── callbacksDir (std::string)
  └── running (std::atomic<bool>)
```

### 7.2 请求解析（状态机�?
```
 recv() �?parseRequestLine() �?validateMethod() �? validateContentType() �?validateContentLength() �? recvBody() �?parseJson() �?writeCallbackFile() �?sendResponse()
```

### 7.3 Win32 API 用法

| 功能 | API |
|------|-----|
| TCP 监听 | socket() �?bind() �?listen() |
| 异步接受 | AcceptEx() + GetQueuedCompletionStatus() |
| 接收数据 | WSARecv() |
| 发送数�?| WSASend() |
| 文件写入 | CreateFile() + WriteFile() + FlushFileBuffers() |
| 原子 rename | MoveFileExA() |
| 线程同步 | CreateIoCompletionPort() + GetQueuedCompletionStatus() |

### 7.4 常量定义

```cpp
namespace HttpServerConfig {
    constexpr const char*  BIND_HOST      = "127.0.0.1";
    constexpr int          BIND_PORT      = 18900;
    constexpr int          MAX_BODY_SIZE = 65536;   // 64KB
    constexpr int          RECV_BUF_SIZE = 65536 + 4096;
    constexpr int          SEND_BUF_SIZE = 4096;
    constexpr const char*  CALLBACKS_DIR  = "R:\\callbacks\\";
    constexpr const char*  CALLBACKS_DIR_FALLBACK = ".\\callbacks\\";
}
```

---

## 8. 测试策略

### 8.1 单元测试

| 测试�?| 验证�?|
|--------|-------|
| HTTP 请求解析 | GET/POST/HEAD 方法识别、Content-Type 校验 |
| body 大小限制 | 64KB 边界�?4KB+1 溢出 |
| JSON 解析 | 合法 JSON、非�?JSON、畸�?UTF-8 |
| 原子写入 | rename 前文件不存在、rename 后文件完�?|
| fallback | ImDisk 不可用时降级到磁�?|

### 8.2 集成测试

| 测试�?| 方法 |
|--------|------|
| HTTP Server 启动 | curl -X POST http://127.0.0.1:18900/callback -d "{}" |
| 端到端文件写�?| POST 后检�?callbacks/ 目录 |
| 并发写入 | 10 个并�?POST，验证文件数量和完整�?|
| NSSM 重启 | nssm restart hermes_bridge，验�?HTTP Server 重启 |

### 8.3 验证命令

```powershell
# 测试 POST
curl -X POST http://127.0.0.1:18900/callback `
  -H "Content-Type: application/json" `
  -d '{"from":"test","type":"info","content":"hello","timestamp":1745396400000,"client":"test"}'

# 验证文件写入
Get-ChildItem R:\callbacks\ -Filter "*.json" | Select-Object Name, Length, LastWriteTime

# 验证 ImDisk 状�?imdisk.exe -l
```

---

## 9. Phase 1 + Phase 2 交付清单

| 交付�?| 文件 | 状�?|
|--------|------|------|
| HTTP Server 声明 | src/HttpServer.h | 待实�?|
| HTTP Server 实现 | src/HttpServer.cpp | 待实�?|
| 回调写入�?| src/CallbackWriter.{h,cpp} | 待实�?|
| main.cpp 改�?| src/main.cpp | 待实�?|
| mount_callbacks.bat | mount_callbacks.bat | 待实�?|
| 设计文档 | DESIGN_BIDIRECTIONAL_v2.1.md | 本文�?|
| NSSM 配置更新 | install_service.ps1 | 待更�?|

**预计代码增量�?* ~500 �?C++（不含注释）

---

## 10. 已知约束

| 约束 | 说明 |
|------|------|
| WSL2 要求 | inotify 跨系统监测需�?WSL2（WSL1 DrvFs 不支�?inotify�?|
| ImDisk 安装 | Phase 2 部署前需在目标机器安�?ImDisk |
| 管理员权�?| ImDisk 挂载需要管理员权限（通过 NSSM service 解决�?|
| 单机限制 | 内存盘仅本机可见，不支持跨机器访�?|

---

*文档版本：v2.1 | 架构师：Architect Agent | 日期�?026-04-23*

