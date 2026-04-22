# OllamaHandler 超时问题调试报告

## ✅ 修复状态：已应用

**修复时间**：2026-04-22 14:43 (GMT+8)
**执行者**：Executor Subagent

---

## 修复内容

### 1. 最小超时从 30 秒提高到 300 秒
```cpp
// 修改前：
if (timeout_sec <= 0) timeout_sec = 30; // minimum 30s

// 修改后：
if (timeout_sec <= 0) timeout_sec = 300; // minimum 300s (5 min) to support slow models like qwen3.5:4b
```

### 2. 接收超时加倍
```cpp
// 修改前：
WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000);

// 修改后：
WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 2000); // receive timeout doubled for slow responses
```

### 3. 重新编译
- 新 EXE：`C:\lobster\hermes_bridge\hermes_bridge.exe` (342016 bytes, 2026-04-22 14:43)
- 进程已重启

---

## 原始问题分析

（保留原始分析内容，参见下方）

在 `OllamaHandler.cpp` 中，超时设置代码如下：

```cpp
// P1-1: Read timeout from params, fall back to context default
int timeout_sec = params.value("timeout", ctx.default_timeout);
if (timeout_sec <= 0) timeout_sec = 30; // minimum 30s
...
WinHttpSetTimeouts(hSession, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000, timeout_sec * 1000);
```

**当前超时值分析：**

1. **默认超时来源**：`timeout_sec` 首先从请求参数 `params["timeout"]` 获取，若未提供则使用 `ctx.default_timeout`（上下文默认值）
2. **最小超时**：若 `timeout_sec <= 0`，则强制设置为 **30 秒**
3. **四个超时参数**：
   - `WinHttpSetTimeouts` 设置了四个相同的超时值（单位：毫秒）：
     - 解析超时（resolve timeout）
     - 连接超时（connect timeout）
     - 发送超时（send timeout）
     - 接收超时（receive timeout）
   - 目前全部设置为 `timeout_sec * 1000` 毫秒

**实际生效的超时值：**
- 若无特殊配置，默认 `ctx.default_timeout` 可能为 0 或较小值，导致最终采用 **30 秒** 最小超时
- 对于 qwen3.5:4b 等较大模型，生成响应可能超过 30 秒，触发 **WinHTTP 错误 12002 (ERROR_WINHTTP_TIMEOUT)**

## 根因分析

### 1. 超时错误机制
- WinHTTP 在以下任一阶段超时都会返回 12002 错误：
  - DNS 解析（通常不是问题）
  - 建立 TCP 连接
  - 发送请求数据
  - **接收响应数据** ← 最可能阶段
- 对于大型语言模型推理，响应生成时间可能长达数分钟，30 秒接收超时明显不足

### 2. 代码逻辑缺陷
- **最小超时硬编码为 30 秒**：`if (timeout_sec <= 0) timeout_sec = 30;`
- **四个超时使用相同值**：接收阶段应允许更长时间
- **缺乏针对模型特性的超时自适应**：不同模型响应时间差异巨大

### 3. 实际影响
- qwen3.5:4b 在中等长度 prompt 下可能超过 30 秒响应时间
- 导致用户请求失败，体验下降
- 错误信息：`"OLLAMA_ERROR", "WinHttpSendRequest failed, error=12002"` 或类似

## 修复建议

### 方案一：增加默认超时值（推荐）

修改最小超时值，并区分不同阶段的超时：

```cpp
// 在 OllamaHandler.cpp 第 ~60 行附近修改：

// P1-1: Read timeout from params, fall back to context default
int timeout_sec = params.value("timeout", ctx.default_timeout);
if (timeout_sec <= 0) timeout_sec = 300; // 增加最小超时为 300 秒（5分钟）

// 或者更灵活：接收超时单独设置更长
int resolve_timeout = timeout_sec * 1000;
int connect_timeout = timeout_sec * 1000;
int send_timeout = timeout_sec * 1000;
int receive_timeout = timeout_sec * 2000; // 接收超时加倍

WinHttpSetTimeouts(hSession, 
    resolve_timeout, 
    connect_timeout, 
    send_timeout, 
    receive_timeout);
```

### 方案二：完全移除最小超时限制

允许超时值从上下文或参数直接传递，不强制设置最小值：

```cpp
// 修改为：
int timeout_sec = params.value("timeout", ctx.default_timeout);
if (timeout_sec <= 0) {
    // 保持原值或设置更合理的默认值
    timeout_sec = ctx.default_timeout > 0 ? ctx.default_timeout : 120; // 2分钟默认
}
```

### 方案三：动态超时基于模型

根据模型名称设置不同超时：

```cpp
int timeout_sec = params.value("timeout", ctx.default_timeout);
if (timeout_sec <= 0) {
    // 根据模型设置默认超时
    if (model.find("qwen") != std::string::npos || 
        model.find("llama") != std::string::npos) {
        timeout_sec = 300; // 大型模型 5 分钟
    } else {
        timeout_sec = 120; // 其他模型 2 分钟
    }
}
```

### 方案四：配置化超时参数

在 `hermes_bridge.json` 配置文件中增加超时配置：

```json
{
  "handlers": {
    "ollama": {
      "timeout_seconds": 300,
      "receive_timeout_multiplier": 2.0
    }
  }
}
```

然后在代码中读取配置。

## 推荐实施方案

**短期快速修复**：采用 **方案一**，将最小超时从 30 秒提高到 **300 秒**（5分钟），同时将接收超时设置为其他超时的 2 倍。

**代码改动示例**：

```cpp
// 修改 OllamaHandler.cpp 第 ~60-62 行：
int timeout_sec = params.value("timeout", ctx.default_timeout);
// 提高最小超时到 300 秒（5分钟）
if (timeout_sec <= 0) timeout_sec = 300;

// 接收超时加倍，其他超时保持不变
WinHttpSetTimeouts(hSession, 
    timeout_sec * 1000,      // 解析超时
    timeout_sec * 1000,      // 连接超时  
    timeout_sec * 1000,      // 发送超时
    timeout_sec * 2000);     // 接收超时（加倍）
```

## 测试建议

1. **验证修复**：使用 qwen3.5:4b 模型发送较长 prompt，观察是否仍出现 12002 错误
2. **边界测试**：
   - 极长 prompt（>1000 tokens）
   - 流式响应（stream=true）
   - 网络延迟模拟
3. **监控指标**：记录实际请求耗时，调整超时值为合理范围

## 相关文件

- `C:\lobster\hermes_bridge\src\handlers\OllamaHandler.cpp` - 主要修改文件
- `C:\lobster\hermes_bridge\hermes_bridge.json` - 可添加配置（如需）

---
**报告完成时间**：2026-04-22 14:38 (GMT+8)

**调试师**：Debugger Subagent