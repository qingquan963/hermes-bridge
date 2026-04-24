# Hermes Bridge 双向实时通道
**版本：** v2.1
**日期：** 2026-04-23
**状态：** 需求确认，待开发

---

## 背景与目标

Hermes Bridge（桥服务）现已支持基础双向能力，但**反向通道（Bridge → Hermes）依赖 5 秒轮询文件**，龙虾小兵团队内部已实现毫秒级反馈，瓶颈卡在 Hermes 收不到这最后一截。

本方案目标：**打通 Bridge 到 Hermes 的实时反向通道，实现毫秒级端到端延迟。**

### 网络约束说明

| 路径 | 状态 |
|------|------|
| WSL → Windows | ✅ 0.166ms，localhost 直接访问 |
| Windows → WSL | ❌ 不通，网络隔离 |

因此，反向通道不走直接 HTTP，必须绕道文件系统 + inotify 事件通知。

---

## 整体架构

```
龙虾小兵（Windows）
        ↓ POST localhost:18900
Bridge HTTP Server（Windows C++，端口 18900）
        ↓ 写入内存盘（tmpfs）
callbacks/（tmpfs，内存文件系统）
        ↓ inotify 事件（< 10ms）
WSL inotify 守护进程
        ↓ Unix socket 或文件注入
Hermes 主程序
```

**关键点：** callbacks/ 目录全程在内存中读写，不碰物理硬盘，速度极快。

---

## 两部分组件

| 组件 | 位置 | 职责 |
|------|------|------|
| Bridge HTTP Server | Windows C++ | 接收 POST 请求，写入 tmpfs callbacks/ |
| Hermes inotify 监听器 | WSL Python | 监测 tmpfs callbacks/，事件驱动通知 Hermes |

---

## 功能需求

### 一、Bridge HTTP Server（新增模块）

**端口：** 18900（仅监听 127.0.0.1，不对外暴露）

**接口：**
```
POST /callback
Content-Type: application/json

{
  "from": "lobster",
  "type": "task_update | error | knowledge | info",
  "content": "具体内容",
  "timestamp": 1745396400000,
  "client": "lobster"
}
```

**响应：**
- 成功：`HTTP 200` → `{"status": "ok"}`
- 失败：`HTTP 500` → `{"status": "error", "message": "..."}`

**行为要求：**
1. 接收请求后，立即将内容写入 `C:\lobster\hermes_bridge\callbacks\{timestamp}_{client}.json`
2. **callbacks/ 必须使用 tmpfs 内存盘**（不对接物理硬盘）
3. 写文件使用原子操作（先写 `.tmp` 再 rename），避免读到不完整内容
4. 写完即返回 200，**不等 Hermes 确认**（异步处理）
5. 如果 callbacks/ 目录不存在自动创建

**安全限制：**
- 仅监听 `127.0.0.1:18900`
- 请求体大小限制 64KB
- 拒绝非 POST 请求

---

### 二、callbacks/ 目录（tmpfs 内存盘）

**挂载方式：** 由 Bridge 启动时在 Windows 上创建 RAM disk，或由 NSSM 启动脚本在启动阶段执行内存盘挂载。

**读写速度：** 内存级读写，延迟 < 1ms

**目录内容：**
- 仅存放临时 JSON 文件（每次回调生成一个）
- Hermes inotify 监听器处理后**立即删除**
- 不落物理硬盘，不持久化

**注意：** Windows 内存盘工具可用 `ImDisk` 或 `AROM` 等开源工具，NSSM 启动时自动挂载。

---

### 三、WSL inotify 监听器（新增 Python 组件）

**实现语言：** Python 3

**监测目标：** `/mnt/c/lobster/hermes_bridge/callbacks/`

**监测事件：** `IN_CREATE | IN_MOVED_TO`

**处理流程：**
```
1. 启动 inotify 监听 callbacks/ 目录
2. 捕获到新 .json 文件事件
3. 读取文件内容（加文件锁，防止读到正在写入的内容）
4. 解析 JSON，提取 type 和 content
5. 将消息通过 Unix socket 发给 Hermes 主进程
   （或写入 Hermes 的 cmd_{hermes}.txt 作为 fallback）
6. 删除已处理的文件
7. 返回步骤 1 继续监听
```

**鲁棒性要求：**
- 如果 Hermes 未就绪，消息暂存 `~/.hermes/callback_queue/`（普通文件），下次 Hermes 就绪时补发
- 进程异常退出后由 systemd 自动重启（或由 Hermes 主程序拉起）

---

### 四、消息类型（type 字段）

| type | 含义 | Hermes 处理方式 |
|------|------|----------------|
| `task_update` | 任务状态更新 | 更新任务状态，告知用户 |
| `error` | 错误/异常 | 立即告警，优先处理 |
| `knowledge` | 知识传递 | 存入记忆，供后续使用 |
| `info` | 通用信息 | 存档，必要时告知用户 |

---

### 五、文件格式规范

**文件名：** `{timestamp}_{client}.json`

**示例：** `1745396400000_lobster.json`

**文件内容：**
```json
{
  "from": "lobster",
  "type": "task_update",
  "content": "任务已完成：代码审查通过，3个文件变更",
  "timestamp": 1745396400000,
  "client": "lobster"
}
```

---

## 目录结构

```
C:\lobster\hermes_bridge\
├── hermes_bridge.exe       # Bridge 主程序（新增 HTTP Server）
├── callbacks/               # 【新增】tmpfs 内存盘挂载点
│   └── *.json              # 临时回调文件，处理后即删
├── cmd_*.txt               # 现有：Hermes → Bridge 命令通道
├── out_*.txt               # 现有：Bridge → Hermes 结果通道
└── logs/
    └── events.txt          # 现有：事件日志

~/.hermes/                   # WSL 侧
├── callback_queue/          # 【新增】消息暂存队列（Hermes 未就绪时使用）
└── inotify_listener.py     # 【新增】inotify 监听器脚本
```

---

## 非功能需求

| 需求 | 说明 |
|------|------|
| **实时性** | 端到端延迟 < 50ms（inotify 事件 < 10ms + HTTP 处理 < 5ms + IPC < 5ms） |
| **可靠性** | 处理后删除文件避免积压；Hermes 未就绪时写入 fallback 队列 |
| **隔离性** | HTTP Server 仅监听 localhost；不暴露外网端口 |
| **兼容性** | 现有 cmd_*/out_* 通道保持不变，不影响 v2.0 功能 |
| **可维护性** | HTTP Server 和文件轮询逻辑模块解耦，独立测试 |

---

## 改造优先级

| 阶段 | 内容 | 交付物 |
|------|------|--------|
| **Phase 1** | Bridge 增加 HTTP Server 模块（端口 18900） | hermes_bridge_v2.1.exe |
| **Phase 2** | Windows tmpfs 内存盘挂载（callbacks/） | 启动脚本或 ImDisk 配置 |
| **Phase 3** | WSL inotify 监听器 Python 脚本 | inotify_listener.py |
| **Phase 4** | Hermes 主程序集成 inotify 消息处理 | Hermes 适配改造 |
| **Phase 5** | 端到端测试 + 性能验证 | 压测报告 |

---

## 已知约束

1. **WSL 版本要求：** inotify 跨系统监测需要 WSL2 环境（WSL1 的 DrvFs 不支持 inotify）。
2. **tmpfs 挂载：** Windows 侧需使用 ImDisk 或同类工具将 `callbacks/` 挂载为 RAM disk，NSSM 启动时自动挂载。
3. **临时方案说明：** 本次为"准实时"实现（Phase 1-4），未来可升级为完整的 Unix socket 事件驱动（Phase 5，架构重写）。

---

## 附录：参考技术栈

| 组件 | 技术选型 |
|------|---------|
| Windows HTTP Server | C++，手写 HTTP 或轻量库（参考现有代码风格） |
| Windows 内存盘 | ImDisk（开源，轻量，NSSM 友好） |
| WSL inotify | Python 3 + pyinotify 或 inotify 绑定 |
| IPC | Unix Domain Socket（`/tmp/hermes_lobster.sock`）或 cmd_*.txt 回退 |
