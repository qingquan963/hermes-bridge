# Hermes Bridge — 项目状态

**文档版本**：v3
**更新日期**：2026-04-22
**状态**：✅ 架构方案已通过审核，进入实现准备阶段

---

## 一、总体状态

| 项目 | 状态 | 备注 |
|------|------|------|
| 需求确认 | ✅ 完成 | 百事通需求文档 v9（含第九节审核意见）|
| PM 评审 | ✅ 完成 | 有条件接（3个接单条件已明确）|
| 架构设计 | ✅ 完成 | DESIGN.md v3，结合第九节 + PM 评审 |
| 详细规格 | ✅ 完成 | SPEC.md v3，含 JSON 完整性检查、原子写入 |
| 技术选型 | ✅ 冻结 | 5项全部确认 |
| 开发阶段 | ⏳ 待启动 | P0 尚未开始 |

---

## 二、技术选型冻结清单

| 组件 | 选型 | 版本 | 状态 |
|------|------|------|------|
| HTTP 客户端 | libcurl | 8.x 静态链接 | ✅ 确认 |
| JSON 解析 | nlohmann/json | v3.10+ | ✅ 确认 |
| 线程池 | 自研 | lightweight pool，<100行 | ✅ 确认 |
| 日志 | spdlog | 异步 + 轮转 | ✅ 确认 |
| 编译器 | MSVC | VS Build Tools 2022 x64 /MT | ✅ 确认 |

---

## 三、需求评审结论

**PM 结论**：有条件接

**接单条件（必须在实现前/中解决）**：

1. ✅ **JSON 完整性检查必须实现**：Bridge 读 cmd 文件时必须检测空文件、截断 JSON，发现不完整时跳过本次处理，不能 crash 或返回错误结果。
2. ✅ **原子写入必须落地**：out 文件写入必须先写 `.tmp` 再 rename，确保 Hermes 不会读到半截结果。
3. ✅ **P0 阶段必须包含多 client 并发框架**：避免后期重构成本。

---

## 四、风险追踪

| 风险 ID | 描述 | 等级 | 状态 | 缓解措施 |
|---------|------|------|------|---------|
| R1 | WSL 文件锁与 Windows 文件锁兼容性 | 🟡 中 | ✅ 已认可 | JSON 完整性检查 + 原子写入作为兜底；Hermes 发指令天然串行 |
| R2 | libcurl 静态链接 Windows 依赖 | 🟡 中 | ⏳ 待解决 | vcpkg 管理依赖，需在 P0 启动前确认方案 |
| R3 | 崩溃自拉起可靠性 | 🟡 中 | ⏳ 待解决 | 推荐 NSSM 注册为 Windows Service |
| R4 | Hermes 轮询间隔配合 | 🟢 低 | ✅ 无风险 | 5s Bridge 轮询 + 1s Hermes 轮询，满足 <2s 延迟要求 |
| R5 | 中文文件名支持 | 🟢 低 | ✅ 无风险 | wide char API + UTF-8 encoding 参数 |
| R6 | 多 client 并发写同一文件 | 🟢 低 | ✅ 无风险 | 每个 client 独立文件，天然隔离 |

---

## 五、实现阶段状态

| 阶段 | 内容 | 预估人天 | 状态 |
|------|------|---------|------|
| **P0** | Bridge骨架 + 线程池 + exec/file_read/file_write + JSON完整性检查 + 原子写入 + 多client并发框架 | 3–4d | ⏳ 待启动 |
| **P1** | HTTP client + ollama handler | 2d | ⏳ 待启动 |
| **P2** | process_start/stop + ps_service_query | 1–2d | ⏳ 待启动 |
| **集成测试** | 端到端联调 + Hermes联调 + 边界测试 | 2–3d | ⏳ 待启动 |
| **文档部署** | README + 配置文件 + Task Scheduler/NSSM | 0.5d | ⏳ 待启动 |
| **合计** | | **8–12人天** | |

---

## 六、关键约束

1. **先验证后扩展**：先用 exec + file_read + file_write 验证核心链路，再扩展其他 action
2. **P0 必须包含多 client**：并发框架随 P0 一起实现，不留到后期
3. **JSON 完整性检查为 P0 必选**：R1 的核心兜底机制，必须在 P0 落地
4. **原子写入为 P0 必选**：out 文件写入必须在 P0 实现
5. **libcurl 方案需明确**：P0 启动前确认 vcpkg 或手动静态链接方案

---

## 七、下一步行动

| 序号 | 行动项 | 责任人 | 状态 |
|------|--------|--------|------|
| 1 | 确认 libcurl 静态链接方案（vcpkg vs 手动）| Architect | ⏳ |
| 2 | 确认崩溃自拉起方案（NSSM vs Task Scheduler）| Architect | ⏳ |
| 3 | Executor 领取 P0 任务，启动开发 | Executor | ⏳ |
| 4 | Reviewer 审查 P0 实现 | Reviewer | ⏳ |

---

*本文档由 Architect（DeepSeek）基于百事通第九节 + PM_REVIEW.md v2 生成*
