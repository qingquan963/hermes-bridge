# Hermes Bridge v2.1 V4 Bug 修复验收报告

**验收时间**：2026/4/23 12:47 GMT+8  
**验收人**：verifier subagent  
**exe 版本**：418,304 bytes（2026/4/23 12:46:59）

---

## 测试结果

| 测试项 | 期望结果 | 实际结果 | 状态 |
|--------|----------|----------|------|
| V4 超大 Payload（70KB） | HTTP/1.1 413 Payload Too Large | HTTP/1.1 400 Bad Request | ❌ 失败 |
| V1 正常请求回归 | HTTP/1.1 200 OK | HTTP/1.1 200 OK | ✅ 通过 |

---

## 详细日志

### V4 验证（70KB body）
- **发送**：POST /callback，Content-Length: 70000
- **期望**：413 Payload Too Large
- **实际**：400 Bad Request
- **问题**：返回了 400 而不是 413，说明 content_length 检查仍未正确触发，或检查逻辑有问题

### V1 回归验证（正常 body）
- **发送**：POST /callback，JSON body（~70 bytes）
- **期望**：200 OK
- **实际**：200 OK ✅

---

## 结论

**V4 Bug 修复未完全生效**。V1 正常请求仍正常响应（200），但 V4 大 Payload 请求返回的是 `400 Bad Request` 而非预期的 `413 Payload Too Large`。

可能原因：
1. content_length 检查仍不在 validateJson 之前
2. 检查逻辑中 max_content_length 阈值不对（可能是 64KB 而非 64KB+1）
3. 413 响应的 status code 设置有误

**需要 Executor 重新检查代码中 content_length 检查的位置和 413 响应的设置。**
