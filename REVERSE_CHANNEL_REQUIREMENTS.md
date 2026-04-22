# Hermes Bridge — 反向推送通道需求

**日期**: 2026-04-23
**提出人**: 百事通 (Hermes Agent)
**优先级**: P0
**类型**: 新功能

---

## 背景与目标

### 当前架构（单向）

```
Hermes(W到了) → Bridge: 写cmd_*.txt → Bridge执行 → 写out_*.txt
                                     ↑
                              5秒轮询延迟，被动等
```

### 目标架构（双向）

```
Hermes → Bridge: 写cmd_*.txt → Bridge执行 → 写out_*.txt → Hermes主动拉取
                    ↑
Hermes ← Bridge:  Bridge通过反向通道主动推送事件/结果给Hermes
```

核心需求：**Hermes 能实时接收 Bridge 的回调通知，无需等待 5s 轮询**

---

## 方案一：Hermes HTTP 回调端点（推荐）

### 架构

```
Bridge                          Hermes (WSL)
  │                                  │
  │  POST /bridge/callback           │  监听 0.0.0.0:18899
  │  {cmd_id, status, result}       │
  ├─────────────────────────────────►
  │                                  │  即时处理
```

### Hermes 侧需求

Hermes Agent 需要一个 HTTP 服务器，监听来自 Bridge 的回调：

- 端点：`POST /bridge/callback`
- 认证：简单的 token 验证（防止其他人乱发）
- 处理：解析 callback，写入事件队列或直接处理
- 端口：**18899**（WSL 映射到 Windows 可访问）

### Bridge 侧需求

callback_url 支持内网地址：

- 当前 SSRF 保护拦截了 `127.0.0.1`
- 需要对特定地址（如 `http://host.docker.internal:18899`）放行
- 或：Bridge 启动时自动注册一个内部回调端点

### 验收标准

- [ ] Hermes 监听 18899 端口可接收 POST 请求
- [ ] Bridge 执行完命令后 POST 回调到 Hermes
- [ ] 回调延迟 < 1 秒（去掉 5s 轮询等待）
- [ ] 回调包含完整结果（cmd_id、status、stdout、stderr、exit_code）
- [ ] Hermes 端有认证保护

---

## 方案二：Windows 命名管道（备选）

### 架构

```
Bridge → 命名管道 \\.\pipe\hermes_bridge → Hermes 监听该管道
```

### 优点
- 不需要网络端口
- Windows 进程间通信，低延迟

### 缺点
- Hermes 在 WSL，WSL 访问 Windows 命名管道需要特殊处理
- 实现复杂度高

---

## 推荐方案

**方案一（HTTP 回调端点）**，理由：
- 技术栈简单，Bridge 已有 libcurl
- Hermes 已有 HTTP 服务器框架（FastAPI/Flask）
- 延迟从 5s 降到 <1s
- 易于扩展（以后可以加 WebSocket）

---

## 实现步骤（草案）

1. **Hermes 侧**：在 Hermes Agent 中新增 HTTP 回调端点，监听 18899
2. **Bridge 侧**：新增配置项 `hermes_callback_url`，执行完成后 POST 回调
3. **SSRF 调整**：对 `host.docker.internal` 或特定内网地址放行
4. **认证**：简单的 Bearer token

---

## 临时方案（快速验证）

如果完整实现太复杂，可以先做一个简化版：

- Bridge 写一个标记文件 `C:\lobster\hermes_bridge\callback_flag.txt`
- 内容是最近完成任务的 cmd_id
- Hermes 通过 5s 轮询这个文件感知有完成事件
- Hermes 再读对应的 out_*.txt

这样延迟还是 5s，但至少不用遍历所有 client_id 去猜哪个有更新。
