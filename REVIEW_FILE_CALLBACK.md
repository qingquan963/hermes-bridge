# REVIEW_FILE_CALLBACK.md — P0-2 file:// 崩溃修复审查

**项目路径**：`C:\lobster\hermes_bridge\src\CallbackClient.cpp`
**审查日期**：2026-04-23
**审查轮次**：P0-2 专项审查

---

## 审查结论：✅ 通过

---

## 修复内容对照

| # | 修复项 | 状态 | 证据 |
|---|--------|------|------|
| 1 | `isBlockedUrl()` 函数签名更新 — 新增 `client_id` 和 `cmd_id` 参数 | ✅ | 行 19：`bool isBlockedUrl(const std::string& url, const std::string& client_id, const std::string& cmd_id)` |
| 2 | 显式拒绝 `file://` 协议 — 函数开头拦截 | ✅ | 行 27-30：`if (lower.rfind("file://", 0) == 0) { LOG_WARN(...); return true; }` |
| 3 | `url too short` 路径加日志 | ✅ | 行 24：`LOG_WARN("[callback] SSRF blocked: url too short (client={}, cmd_id={})", client_id, cmd_id)` |
| 4 | 加强协议检查 — 使用 `isHttp` 布尔变量 | ✅ | 行 32：`bool isHttp = (lower.rfind("http://", 0) == 0) \|\| (lower.rfind("https://", 0) == 0)` |
| 5 | 清理调用点 — 移除冗余旧日志 | ✅ | 行 72：调用点 `isBlockedUrl(url, client_id, cmd_id)` 传入新参数 |

---

## 验收标准达成

- [x] `file://` 被显式拦截（不走到 WinHTTP）
- [x] `http://` 和 `https://` 通过检查
- [x] 所有阻止日志含 client_id + cmd_id

---

## 关键代码片段

### isBlockedUrl() 函数头
```cpp
bool isBlockedUrl(const std::string& url, const std::string& client_id, const std::string& cmd_id) {
    if (url.size() < 7) {
        LOG_WARN("[callback] SSRF blocked: url too short (client={}, cmd_id={})", client_id, cmd_id);
        return true;
    }
    std::string lower = url;
    for (auto& ch : lower) ch = (char)tolower((unsigned char)ch);

    // Explicitly reject file:// protocol (prevents WinHTTP crash)
    if (lower.rfind("file://", 0) == 0) {
        LOG_WARN("[callback] SSRF blocked: file:// protocol not allowed (client={}, cmd_id={})", client_id, cmd_id);
        return true;
    }

    // Only allow http:// and https://
    bool isHttp = (lower.rfind("http://", 0) == 0) || (lower.rfind("https://", 0) == 0);
    if (!isHttp) {
        LOG_WARN("[callback] SSRF blocked: non-HTTP protocol (client={}, cmd_id={})", client_id, cmd_id);
        return true;
    }
    // ... 后续内网 IP 检查 ...
}
```

### 调用点
```cpp
void asyncCallback(const std::string& url, const std::string& json_body,
                   const std::string& client_id, const std::string& cmd_id) {
    if (isBlockedUrl(url, client_id, cmd_id)) {
        return;
    }
    // ... 后续 WinHTTP 处理 ...
}
```

---

## 日志完整性检查

| 阻止路径 | 日志消息 | 含 client_id | 含 cmd_id |
|----------|----------|:------------:|:---------:|
| URL 太短 | `SSRF blocked: url too short` | ✅ | ✅ |
| file:// 协议 | `SSRF blocked: file:// protocol not allowed` | ✅ | ✅ |
| 非 HTTP 协议 | `SSRF blocked: non-HTTP protocol` | ✅ | ✅ |

---

**结论**：P0-2 file:// 崩溃修复完全通过验收，5 项修复均已正确实施。
