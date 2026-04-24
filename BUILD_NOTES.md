# Hermes Bridge — C++ 编译规范（MSVC 必读）

**适用环境**：VS 2026 Community MSVC 19.50，CMake 4.2.3
**维护者**：每次代码变更后负责验证编译通过

---

## 一、CMake 编译命令（正确）

```powershell
# 在项目根目录执行（非 Release/ 子目录）
cd C:\lobster\hermes_bridge
cmd /c 'mkdir Release 2>nul'
cmake -G "Visual Studio 18 2026" -A x64 -S . -B Release
cmake --build Release --config Release
```

**注意**：不要用 `cmake ..` 在子目录运行；Generator 必须是 `"Visual Studio 18 2026" -A x64`

---

## 二、MSVC 兼容性问题清单（高频错误）

### 1. `std::search` C2589
**错误**：`error C2589: "(": "::"右边的非法标记`  
**原因**：Windows 头文件定义了 `min`/`max` 宏，与 `<algorithm>` 中的 `std::min`/`std::max` 冲突  
**解决方案**：始终用括号包裹 `std::min`/`std::max`
```cpp
// ❌ 错误
std::min(a, b)

// ✅ 正确
(std::min)(a, b)
```

### 2. `nullptr` 不能隐式转换 C2446
**错误**：`error C2446: "==": 没有从"nullptr"到"SOCKET"的转换`  
**原因**：`SOCKET` 是 `UINT_PTR` typedef，不是指针类型  
**解决方案**：始终用 `INVALID_SOCKET` 比较 socket，用 `SOCKET` 类型
```cpp
// ❌ 错误
SOCKET s = nullptr;
if (s == nullptr)

// ✅ 正确
SOCKET s = INVALID_SOCKET;
if (s == INVALID_SOCKET)
```

### 3. `void*` vs `SOCKET` 类型混乱
**错误**：函数声明和定义返回类型不匹配  
**解决方案**：`SOCKET` 类型的函数不要返回 `void*`，也不要将 `SOCKET` 传给 `void*` 参数
```cpp
// ❌ 错误
void* createSocket();
bool handleClient(void* sock);

// ✅ 正确
SOCKET createSocket();
void handleClient(SOCKET sock);
```

### 4. `nlohmann/json.hpp` JSON 解析命名冲突
**错误**：`error C2653: "json": 不是类或命名空间名称`  
**原因**：`using json = nlohmann::json;` 只在某个 .cpp 中声明，其他文件用不到  
**解决方案**：每个用到 json 的 .cpp 文件顶部必须声明
```cpp
#include <nlohmann/json.hpp>
using json = nlohmann::json;
```

### 5. `std::chrono` 与 Windows timeval 冲突
**错误**：`error C2446` 或类型不匹配  
**解决方案**：Windows 程序中用 `<winsock2.h>` 的 `timeval`，避免混用 `std::chrono`

### 6. `strstr` 大小写敏感问题
Windows `strstr` 是 `strstr` 不是 `StrStr`，与某些库混用时注意大小写。

---

## 三、正确的主文件头文件结构

每个新增的 .cpp 文件必须：
1. `#include <nlohmann/json.hpp>` 后立即写 `using json = nlohmann::json;`
2. 用 `std::min` / `std::max` 时加括号 `(std::min)(a, b)`
3. socket 类型统一用 `SOCKET`，不用 `void*`
4. socket 判空用 `== INVALID_SOCKET`，不用 `== nullptr`

---

## 四、快速验证命令

```powershell
# 只做 CMake 配置（不编译）
cmake -G "Visual Studio 18 2026" -A x64 -S . -B Release

# 编译单个文件测试
cmake --build Release --config Release --target HermesBridge 2>&1 | Select-String "error|warning"
```

---

## 五、已知兼容的库版本

| 库 | 版本 | 来源 |
|----|------|------|
| nlohmann/json | v3.10.5 | include/nlohmann/ |
| spdlog | 内置 | Logger.h 自研轮转日志 |

**不要随意升级或替换上述库版本。**
