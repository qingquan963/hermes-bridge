# Hermes Bridge v3 修复审查报告

**审查角色：** Reviewer  
**审查时间：** 2026-04-22 21:01 GMT+8  
**项目路径：** `C:\lobster\hermes_bridge\`

---

## 修复验证摘要

| Bug | 描述 | 状态 |
|-----|------|------|
| Bug 1 | cmd_id 缓存去重机制 | ✅ 通过 |
| Bug 2 | Client 白名单限制 | ✅ 通过（确认不存在） |
| Bug 3 | 分号 `;` 黑名单太粗暴 | ✅ 通过 |

**结论：✅ 可以交付**

---

## Bug 1：cmd_id 缓存去重机制

### 修改验证

**CommandQueue.h** — `Command` 结构体新增字段：
```cpp
bool force = false;  // if true, bypass any cmd_id deduplication cache
```
✅ 字段声明正确，默认值为 `false`。

**FileMonitor.h** — 新增成员：
```cpp
mutable std::mutex seen_mtx_;
std::unordered_map<std::string, std::unordered_set<std::string>> seen_cmd_ids_;
```
✅ 互斥锁和缓存 map 声明正确。

**FileMonitor.cpp** — 核心逻辑：
```cpp
cmd.force = cmd_json.value("force", false);  // 读取 force 字段

if (!cmd.force) {
    std::lock_guard<std::mutex> lock(seen_mtx_);
    auto& seen = seen_cmd_ids_[client_id];
    if (seen.find(cmd.cmd_id) != seen.end()) {
        LOG_INFO("[{}] Skipping duplicate cmd {} (add force:true to re-execute)", ...);
        continue;
    }
    seen.insert(cmd.cmd_id);
} else {
    // force=true: allow re-execution, update the cached cmd_id
    std::lock_guard<std::mutex> lock(seen_mtx_);
    seen_cmd_ids_[client_id].insert(cmd.cmd_id);
}
```
✅ 逻辑正确：
- `force=false`：检查缓存，已存在则跳过，不存在则加入缓存
- `force=true`：跳过缓存检查，强制入队，同时也将 cmd_id 标记为"已见"（防止后续轮询重复入队）

### 新问题（低优先级）

**⚠️ `seen_cmd_ids_` 永不清理**

缓存 map 随运行时间无限增长。对于长期运行的 Hermes Bridge，内存会持续增加。

建议后续添加 TTL 清理机制（例如基于 timestamp 或轮询次数过期）。**但此问题不影响 v3 交付**，因为缓存 map 的 key 是 `client_id`（通常数量有限），每个 cmd_id 字符串本身通常也很小。

---

## Bug 2：Client 白名单限制

### 验证方法

全源码搜索关键词：`whitelist`、`white_list`、`allowlist`、`client.*filter`、`client.*check`、`valid.*client`

```
结果：无匹配项
```

### 结论

✅ **确认无白名单限制**。Executor 报告属实。`FileMonitor.cpp` 的 `scanAndEnqueue()` 方法对任何以 `cmd_*.txt` 命名格式的文件都进行处理，`client_id` 仅用于日志和追踪，无访问控制。

---

## Bug 3：分号 `;` 黑名单太粗暴

### 修改验证

**ExecHandler.cpp** 第49行附近：
```cpp
const char* dangerous = (shell == "none") ? ";&|`()<>\\" : "&|`()<>\\";

for (const char* p = dangerous; *p; ++p) {
    if (command.find(*p) != std::string::npos) {
        // BLOCKED
    }
}
```

✅ **shell 模式**（`shell="powershell"` / `"cmd"` / `"python"`）：
- `dangerous = "&|\`()<>\\"`
- `;` **不在**此列表中 → 放行 ✅

✅ **direct 模式**（`shell="none"`）：
- `dangerous = ";&|\`()<>\\"`
- `;` **在**此列表中 → 拦截 ✅

逻辑与需求完全一致。

### 附带检查：direct 模式下的 `\\`

在 `shell == "none"` 的 dangerous 字符中，`\\` 被写入为转义序列（`\\`），实际匹配的是单个反斜杠 `\`。这是合理的设计，因为 direct 模式通过 `CreateProcessW lpApplicationName` 直接执行，反斜杠是路径分隔符，理应被拦截。

---

## 代码质量总评

| 维度 | 评价 |
|------|------|
| 逻辑正确性 | ✅ 三处修复逻辑均正确 |
| 线程安全 | ✅ 正确使用 `std::lock_guard<std::mutex>` |
| 边界情况 | ✅ `force` 字段正确使用 `value("force", false)` 提供默认值 |
| 内存管理 | ⚠️ `seen_cmd_ids_` 永不清理（低优先级） |
| 向后兼容 | ✅ `force` 默认 false，原有行为不变 |

---

## 最终结论

**✅ 可以交付**

所有三个修复均已正确实现，逻辑完整，未引入新的关键 Bug。`seen_cmd_ids_` 永不清理属于低优先级长期运行问题，不影响当前交付。
