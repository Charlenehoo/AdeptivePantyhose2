# IActorUpdateManager ABI 兼容性调查报告

**版本**：v3.0.6beta 调查期
**日期**：2026-11-01
**报告人**：Charlene Hoo
**状态**：根因已定位，待确认用户 SKEE 版本
**关联**：

- [`onattach-thread-affinity-report-2026-10-30.md`](onattach-thread-affinity-report-2026-10-30.md) — v3.0.5 OnAttach 跨线程调查
- [`onattach-d3d-regression-report-2026-10-29.md`](onattach-d3d-regression-report-2026-10-29.md) — v3.0.3 回归调查
- [`v3.0.4-ssot-refactor.md`](v3.0.4-ssot-refactor.md) — v3.0.4 演进细节

---

## 1. 问题描述

### 1.1 两个用户报告

**AvatarWenyihaobb（Nexus，2026-10-31 ~ 11-01）**：

> 只要装上了 3.0.5 版本，OAR 就会显示错误并提示丢失 Open Animation Replacer - IED conditions 这个依赖，同时 IED 会显示找不到 Simple Dual Sheath 的 interface
> 3.0.3-3.0.5 SE 版本均会报错

试了 3.0.6beta 后：

> 主菜单还没加载完就闪退了

提供了完整 CrashLogger 日志（`crash-2026-11-01-12-41-29.log`）。

**GOODNEWSGN（Nexus，2026-10-28 ~ 11-01）**：

> 最新版本似乎会和 oar---IEDcondiction 产生冲突，使部分依赖 IED 的动作模组出问题

后续报告 3.0.6beta 也崩溃，但 `skse64.log` 显示同时加载了 `AdeptivePantyhose_SE.dll`（3.0.2.0）和 `AdeptivePantyhose_SE_Log.dll`（3.0.5.0），**双版本环境，日志不可靠**。

### 1.2 3.0.6beta 的唯一变更

`git diff v3.0.5..4d314a9 -- src/` 显示：

```diff
case SKSE::MessagingInterface::kPostPostLoad: {
-   SKEEIntegration::GetSingleton().Initialize();
    break;
}
case SKSE::MessagingInterface::kDataLoaded: {
    LoadMorphRules();
+   SKEEIntegration::GetSingleton().Initialize();
    RegisterEvents();
    break;
}
```

**`Initialize()` 从 `kPostPostLoad` 移到 `kDataLoaded`。函数体没动。**

变更动机：3.0.5 在 `kPostPostLoad` 注册 `AttachListener`，导致 `kPostPostLoad → kDataLoaded` 窗口内 SKEE 的 `OnAttach` 触发 `ApplyVertexDiff`，与 IED / SDS / 社区着色器的 D3D 初始化冲突。

### 1.3 CrashLogger 关键证据

```
Unhandled exception "EXCEPTION_ACCESS_VIOLATION" at 0x7FF83819B717 skee64.dll+00CB717
Instruction: mov eax, [rdx+0x08]
Base Register: rdx = 0x0000000000000005
Computed Address: 0x000000000000000D

RCX = ActorUpdateManager*
RAX = AttachListener*
RDX = 5
```

栈：

```
[ 0] skee64.dll+00CB717
[ 1] AdeptivePantyhose_SE_Log.dll+002F5A1
[ 2] AdeptivePantyhose_SE_Log.dll+0011D3F   ← SKEEIntegration::Initialize
[ 3] skse64_1_5_97.dll+0081182              ← 消息分发
[ 4] skse64_1_5_97.dll+000A598
[ 5] SkyrimSE.exe+05B050D
```

- `RCX = ActorUpdateManager*`：`AddInterface` 的 `this` 有效
- `RAX = AttachListener*`：`AddInterface` 的 `observer` 参数求值结果有效
- **`RDX = 5`**：`AddInterface` 的 `observer` 实际值，但寄存器里是 `5`（`bodyMorph->GetVersion()` 的残留）
- 崩在 `skee64.dll+0xCB717` 的 `mov eax, [rdx+0x08]`，`[5+8] = 0xD`

**这是 `SKEEIntegration::Initialize()` 在 `kDataLoaded` 里调用 `AddInterface` 的现场，`AddInterface` 的 vtable 偏移错，跳到了把 `RDX` 当指针访问的函数。**

---

## 2. SKEE 源码调查

### 2.1 `interfaceMap` 永远有效

`skee64/main.cpp:555`：

```cpp
void InterfaceExchangeMessageHandler(SKSEMessagingInterface::Message* message) {
    switch (message->type) {
    case InterfaceExchangeMessage::kMessage_ExchangeInterface: {
        InterfaceExchangeMessage* exchangeMessage = (InterfaceExchangeMessage*)message->data;
        exchangeMessage->interfaceMap = &g_interfaceMap;   // 全局对象
    }
    break;
    }
}
```

`g_interfaceMap` 是全局对象，进程生命周期内永远有效。每次 `Dispatch` 都重新填充，不存在 stale pointer。

`InterfaceExchangeMessageHandler` 在 `kPostLoad`（默认 `bEnableEarlyRegistration = false`）注册。`kPostPostLoad` 和 `kDataLoaded` 都在 `kPostLoad` 之后，`Dispatch` 都能成功。

**`interfaceMap` 和 `kPostPostLoad` / `kDataLoaded` 时机无关。**

### 2.2 `QueryInterface` 是纯名字匹配

`skee64/PluginInterface.cpp:3`：

```cpp
IPluginInterface* InterfaceMap::QueryInterface(const char* name) {
    for (auto iface : *this) {
        if (_stricmp(iface.first, name) == 0) {
            return iface.second;
        }
    }
    return NULL;
}
```

**没有版本检查。** `ActorUpdateManager` 在 `g_interfaceMap` 里存在就返回，不存在返回 `nullptr`。

### 2.3 `ActorUpdateManager` 注册时间

`git blame main.cpp:950`：

```
8f58265 skee/main.cpp (expired6978 2019-11-22 09:06:06 -0800 950)
    g_interfaceMap.AddInterface("ActorUpdateManager", &g_actorUpdateManager);
```

**2019-11-22 就注册了。** `kPostPostLoad` 和 `kDataLoaded` 时都能查到。

### 2.4 `IActorUpdateManager::AddInterface` 引入时间

`git log -L '/class IActorUpdateManager/,/^};/:skee64/IPluginInterface.h'`：

| Commit    | Message                                                            | 时间        | `IActorUpdateManager` 虚函数                          |
| --------- | ------------------------------------------------------------------ | ----------- | ----------------------------------------------------- |
| `31a0853` | Initial alpha                                                      | 最早        | 类不存在                                              |
| `4cf6d62` | Refactor various things                                            | 约 2019     | 7 个 `Add*Update` + `Flush`，共 **8 个**              |
| `c1b408f` | Refactored various files for C++20 compliance, updated for 1.6.640 | **约 2022** | 加入 `AddInterface` / `RemoveInterface`，共 **10 个** |
| `7694eab` | Interface update and API changes                                   | 2022 之后   | 加入版本枚举 + Version 2 方法，共 **12 个**           |

**关键事实：`IActorUpdateManager::AddInterface` 是 `c1b408f`（约 2022 年，1.6.640）才加入的。**

**两个 `AddInterface` 不是同一个东西：**

- `g_interfaceMap.AddInterface("ActorUpdateManager", ...)`：`InterfaceMap` 的方法，2019 年就有
- `updateManager->AddInterface(&AttachListener::GetSingleton())`：`IActorUpdateManager` 的虚函数，**2022 年才加入**

---

## 3. 根因

### 3.1 用户 `skee64.dll` 是 2019-2022 之间的版本

- `g_interfaceMap` 里有 `"ActorUpdateManager"` 条目（2019-11-22 起）
- `QueryInterface("ActorUpdateManager")` 返回非空 `&g_actorUpdateManager`
- 但 `IActorUpdateManager` 的 vtable **只有 8 个虚函数**（`GetVersion` / `Revert` + 7 个 `Add*Update` + `Flush`）
- 插件的 `updateManager->AddInterface(...)` 调 vtable 索引 10 **越界**，跳到错误函数
- `RDX = 5` 是 `bodyMorph->GetVersion()` 的残留值（`kPluginVersion5`），被错误函数当指针访问 `[5+8]`，崩在 `0xD`

**crash log 完全吻合。**

### 3.2 为什么 3.0.5 不崩

3.0.5 和 3.0.6beta 的 `Initialize()` 代码相同，只差调用时机。但 `QueryInterface("ActorUpdateManager")` 在 `kPostPostLoad` 和 `kDataLoaded` 都返回非空（2019 年就注册了）。

**唯一解释：3.0.5 的 `QueryInterface("ActorUpdateManager")` 返回了 `nullptr`，`AddInterface` 被跳过。**

可能原因：

1. 用户 3.0.5 的日志里其实有 `ActorUpdateManager not found` 的 WARN，但被截断
2. 用户的 3.0.5 版本对应的 SKEE 更旧（`8f58265` 之前，2019-11-22 之前），`g_interfaceMap` 里没有 `"ActorUpdateManager"` 条目
3. 用户 3.0.5 和 3.0.6beta 测试时用了不同 SKEE 版本

**需要 3.0.5 的完整日志确认。**

### 3.3 GOODNEWSGN 的环境不可靠

`skse64.log` 实锤：

```
plugin ...\AdeptivePantyhose_SE.dll (00000001 AdeptivePantyhose 03000020) loaded correctly (handle 7)
plugin ...\AdeptivePantyhose_SE_Log.dll (00000001 AdeptivePantyhose 03000050) loaded correctly (handle 8)
```

两个 DLL 同时加载：

- `AdeptivePantyhose_SE.dll` = 3.0.2.0
- `AdeptivePantyhose_SE_Log.dll` = 3.0.5.0

两个独立 SKSE listener、两个独立 `Initialize`、两个 `AddInterface`。**他的崩溃报告不可用。**

---

## 4. 修复方案

### 4.1 代码变更

`src/Integration/SKEE/SKEEIntegration.cpp` 的 `Initialize()`：

```cpp
if (updateManager != nullptr) {
    const auto umVersion = updateManager->GetVersion();
    REX::INFO("SKEEIntegration: ActorUpdateManager version={}", umVersion);
    if (umVersion >= 2) {
        m_updateManager = updateManager;
        updateManager->AddInterface(&AttachListener::GetSingleton());
        REX::INFO("SKEEIntegration: AttachListener installed");
    } else {
        REX::WARN("SKEEIntegration: ActorUpdateManager version {} too old (need >= 2), skip AddInterface", umVersion);
    }
}
```

**理由：**

- `GetVersion()` 是 `IPluginInterface` 的虚函数（vtable 索引 1），**所有版本都有**，调用安全
- `kPluginVersion2` 是 `7694eab` 加入的，此时 `AddInterface` 已存在（`c1b408f` 加入）
- `GetVersion() < 2` 说明是 `c1b408f` 之前的版本（`AddInterface` 不存在）
- 牺牲 `c1b408f` 到 `7694eab` 之间的版本（`AddInterface` 可用但 `GetVersion()=1`），保守但安全

**需要确认 `c1b408f` 版本 `ActorUpdateManager::GetVersion()` 返回值：**

```powershell
git show c1b408f:skee64/ActorUpdateManager.cpp | Select-String "GetVersion" -Context 0,5
```

如果返回 2，`GetVersion() >= 2` 可以覆盖 `c1b408f` 之后的版本，只排除 `c1b408f` 之前。如果返回 1，`GetVersion() >= 2` 会同时排除 `c1b408f` 版本。

### 4.2 同时修复 IED 冲突

接口交换回 `kPostPostLoad`（3.0.5 行为，不崩），`OnAttach` 用 `IsReady()` 门禁推迟到 `kDataLoaded` 之后（解决 IED / SDS / 着色器冲突）。

```cpp
// src/main.cpp
case SKSE::MessagingInterface::kPostPostLoad:
    SKEEIntegration::GetSingleton().Initialize();   // 接口交换 + 注册 AttachListener
    break;

case SKSE::MessagingInterface::kDataLoaded:
    LoadMorphRules();
    SKEEIntegration::GetSingleton().SetReady();     // 允许 OnAttach 工作
    RegisterEvents();
    break;
```

```cpp
// src/Integration/SKEE/AttachListener.cpp
void AttachListener::OnAttach(...) {
    if (!SKEEIntegration::GetSingleton().IsReady()) {
        return;
    }
    // ... 原逻辑
}
```

`IsReady()` 是 `std::atomic<bool>`。

### 4.3 为什么这个方案同时修两个问题

| 问题                         | 修复前                                      | 修复后                                              |
| ---------------------------- | ------------------------------------------- | --------------------------------------------------- |
| `AddInterface` vtable 越界崩 | 3.0.6beta 在 `kDataLoaded` 调，旧版 SKEE 崩 | `GetVersion() >= 2` 检查，旧版 SKEE 跳过            |
| IED / SDS / 着色器冲突       | 3.0.5 的 `OnAttach` 在窗口期内做 D3D        | `OnAttach` 用 `IsReady()` 推迟到 `kDataLoaded` 之后 |
| 接口交换时机                 | 3.0.6beta 违反 SKEE 约定                    | 回 `kPostPostLoad`，符合 SKEE 0.4.19.10 约定        |

---

## 5. 待确认事项

### 5.1 用户侧

**AvatarWenyihaobb：**

1. `Data\SKSE\Plugins\skee64.dll` 的文件版本（右键 → 属性 → 详细信息）
2. RaceMenu 的版本号（Nexus 页面）
3. 3.0.5 的完整日志（从 `Plugin version` 到 `RegisterEvents`，确认 `ActorUpdateManager found` 或 `not found` 是否出现）
4. 禁用 `skee64-memleak-patch.dll` 和 `SKEEcosaveLoadCrashFix.dll` 后重试 3.0.6beta

**GOODNEWSGN：**

1. 清理 `Data\SKSE\Plugins\` 下所有 `AdeptivePantyhose*.dll`，只保留一个 3.0.6beta 的 `SE_Log`，再测
2. 提供 `skee64.dll` 版本

### 5.2 源码侧

```powershell
# 确认 c1b408f 版本 ActorUpdateManager::GetVersion() 返回值
git show c1b408f:skee64/ActorUpdateManager.cpp | Select-String "GetVersion" -Context 0,5

# 确认 IAddonAttachmentInterface::OnAttach 签名从 2019 到现在是否变过
git log --oneline -L '/class IAddonAttachmentInterface/,/^};/:skee64/IPluginInterface.h'
```

---

## 6. 验证清单

实现修复后让用户测：

| 场景                                   | 期望                                                                                         |
| -------------------------------------- | -------------------------------------------------------------------------------------------- |
| 旧版 SKEE（`c1b408f` 之前）+ 3.0.6beta | `ActorUpdateManager version=1 too old` WARN，不崩，`AttachListener` 不工作（Modex 路径降级） |
| 新版 SKEE（`7694eab` 之后）+ 3.0.6beta | `ActorUpdateManager version=2` INFO，`AttachListener installed`，不崩                        |
| 新版 SKEE + IED / SDS / 着色器         | `OnAttach` 推迟到 `kDataLoaded` 之后，不冲突                                                 |
| 玩家穿脱高跟                           | `TESEquipEvent` 路径不变，正常                                                               |
| Modex 三件同穿                         | `OnAttach` 路径正常（新版 SKEE）或降级（旧版 SKEE，`TESEquipEvent` 不触发）                  |
| 双版本 DLL（GOODNEWSGN）               | **不支持**，文档说明必须清理                                                                 |

---

## 7. 变更记录

### v3.0.6（待发布）

**修复**：

- `Initialize()` 里 `AddInterface` 前加 `GetVersion() >= 2` 检查，旧版 SKEE 跳过，避免 vtable 越界崩溃
- `Initialize()` 回 `kPostPostLoad`（符合 SKEE 0.4.19.10 约定）
- `AttachListener::OnAttach` 加 `IsReady()` 门禁，实际工作推迟到 `kDataLoaded` 之后（解决 IED / SDS / 着色器冲突）

**根因**：`IActorUpdateManager::AddInterface` 是 SKEE `c1b408f`（约 2022 年，1.6.640）才加入的虚函数。用户旧版 SKEE 的 vtable 只有 8 个虚函数，`AddInterface` 调用越界，跳到错误函数，`RDX=5`（`GetVersion()` 残留）被当指针，崩在 `0xD`。

**文档**：新增本文档，`Architecture.md` §11 已知限制同步更新。

---

## 8. 相关文件

| 文件                                                   | 作用                       |
| ------------------------------------------------------ | -------------------------- |
| `docs/onattach-thread-affinity-report-2026-10-30.md`   | v3.0.5 OnAttach 跨线程调查 |
| `docs/onattach-d3d-regression-report-2026-10-29.md`    | v3.0.3 回归调查            |
| `docs/v3.0.4-ssot-refactor.md`                         | v3.0.4 演进细节            |
| `docs/v3.0.4-pitfalls-and-regression-guards.md`        | v3.0.4 踩坑与防回归        |
| `docs/skee-behavior-notes.md`                          | SKEE BodyMorph 行为笔记    |
| `skee64/IPluginInterface.h`（SKSE64Plugins master）    | `IActorUpdateManager` 定义 |
| `skee64/main.cpp:950`（SKSE64Plugins master）          | `ActorUpdateManager` 注册  |
| `skee64/PluginInterface.cpp:3`（SKSE64Plugins master） | `QueryInterface` 实现      |

---

**报告结束。**
