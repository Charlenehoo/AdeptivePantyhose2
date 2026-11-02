# IActorUpdateManager ABI 兼容性调查报告

**版本**：v3.0.6 调查期
**日期**：2026-11-01
**报告人**：Charlene Hoo
**状态**：根因已定位，决策已定 —— **放弃 SE 1.5.97 支持，SE 最终版冻结 v3.0.2**
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

> **本次报告的最终决策**：SE 1.5.97 生态被锁定在 RaceMenu 0.4.16（无 `IActorUpdateManager::AddInterface`），而 v3.0.3 起 mesh 更新已全部依赖 `OnAttach`，因此 **SE 1.5.97 正式放弃支持，最终版冻结 v3.0.2**。详细论证见 §5。

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

**关键事实：`IActorUpdateManager::AddInterface` 是 `c1b408f`（约 2022 年，1.6.640）才加入的。对应 RaceMenu 发布版本 `0.4.19.13`。**

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

### 3.2 为什么 3.0.5 不崩（历史记录，不影响最终决策）

3.0.5 和 3.0.6beta 的 `Initialize()` 代码相同，只差调用时机。但 `QueryInterface("ActorUpdateManager")` 在 `kPostPostLoad` 和 `kDataLoaded` 都返回非空（2019 年就注册了）。

**唯一解释：3.0.5 的 `QueryInterface("ActorUpdateManager")` 返回了 `nullptr`，`AddInterface` 被跳过。**

可能原因：

1. 用户 3.0.5 的日志里其实有 `ActorUpdateManager not found` 的 WARN，但被截断
2. 用户的 3.0.5 版本对应的 SKEE 更旧（`8f58265` 之前，2019-11-22 之前），`g_interfaceMap` 里没有 `"ActorUpdateManager"` 条目
3. 用户 3.0.5 和 3.0.6beta 测试时用了不同 SKEE 版本

> 该分支不影响最终决策——无论 3.0.5 为何“碰巧不崩”，**旧 SKEE 下 `AddInterface` 调用都不可靠**，必须拦截。

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

### 3.4 决策依据：SE 1.5.97 无法获得 `AddInterface`

| 事实                                                                                                  | 来源                    |
| ----------------------------------------------------------------------------------------------------- | ----------------------- |
| `AddInterface` 于 2022 年 `c1b408f` 加入，对应 RaceMenu `0.4.19.13`                                   | §2.4                    |
| RaceMenu `0.4.19.13` 更新日志为 "Added support for game version 1.6.640"（AE），**不含 SE 1.5.97**    | RaceMenu 官方 changelog |
| RaceMenu `0.4.19.9` 起转为 AE 专用，SE 1.5.97 官方指引为 "Old files" 中的 `0.4.16`                    | Nexus 页面 / 社区共识   |
| 崩溃修复补丁把 `RaceMenu v0.4.16 (SE 1.5.97)` 与 `v0.4.19.16/0.4.20.0 (AE)` 作为两个独立目标处理      | 社区补丁说明            |
| `a88e85b` 已移除 `OnEquipEvent` 的 `UpdateModelWeight`，v3.0.3+ mesh 更新**只有 `OnAttach` 一条路径** | 本仓库 git log          |

**推论**：SE 1.5.97 用户锁在 `0.4.16`，无 `AddInterface`；而 v3.0.3+ 的 mesh 更新只走 `OnAttach`，`AttachListener` 注册不上 → **插件完全不工作**。不是“降级可用”，是“功能缺失”。

**因此：SE 1.5.97 放弃支持，最终版冻结 v3.0.2。**

---

## 4. 修复方案（放弃 SE）

### 4.1 设计原则

1. **不 `#ifdef` 双路径**：源码保持单一路径，不按 SE/AE 编译分支。运行时通过 `GetVersion()` 检测 SKEE 能力。
2. **旧 SKEE 不崩**：`GetVersion() < 2` 时跳过 `AddInterface`，记录明确错误日志，引导用户去下载 v3.0.2。
3. **修复 IED 冲突**：`Initialize()` 回 `kPostPostLoad`（符合 SKEE 约定），`OnAttach` 用 `IsReady()` 门禁推迟到 `kDataLoaded` 之后。
4. **不恢复 `OnEquipEvent` 更新 mesh**：不回退 `a88e85b` 的重构，不为旧 SKEE 保留双路径。

### 4.2 代码变更

**`src/main.cpp`** —— `Initialize()` 回 `kPostPostLoad`，`kDataLoaded` 只放行 `OnAttach`：

```cpp
case SKSE::MessagingInterface::kPostPostLoad: {
    LOG_DEBUG("OnMessage: PostPostLoad");
    SKEEIntegration::GetSingleton().Initialize();
    break;
}
case SKSE::MessagingInterface::kDataLoaded: {
    LOG_DEBUG("OnMessage: DataLoaded");
    LoadMorphRules();
    SKEEIntegration::GetSingleton().SetReady();
    RegisterEvents();
    break;
}
```

**`src/Integration/SKEE/SKEEIntegration.cpp`** —— `Initialize()` 加版本门禁：

```cpp
auto* updateManager = /* QueryInterface("ActorUpdateManager") 结果 */;
if (updateManager != nullptr) {
    const auto umVersion = updateManager->GetVersion();
    if (umVersion >= 2) {
        m_updateManager = updateManager;
        updateManager->AddInterface(&AttachListener::GetSingleton());
        REX::INFO("SKEEIntegration: AttachListener installed (ActorUpdateManager version={})", umVersion);
    } else {
        REX::ERROR("SKEEIntegration: ActorUpdateManager version={} is too old (need >= 2). "
                   "This build requires SKEE 0.4.19.13+ (RaceMenu 0.4.19.13+). "
                   "SE 1.5.97 users must use AdeptivePantyhose v3.0.2.",
                   umVersion);
    }
} else {
    REX::ERROR("SKEEIntegration: ActorUpdateManager not found. "
               "SKEE/RaceMenu missing or too old. SE 1.5.97 users must use v3.0.2.");
}
```

**`src/Integration/SKEE/AttachListener.cpp`** —— `OnAttach` 加 `IsReady()` 门禁：

```cpp
void AttachListener::OnAttach(...) {
    if (!SKEEIntegration::GetSingleton().IsReady()) {
        return;
    }
    // ... 原逻辑
}
```

`IsReady()` 为 `std::atomic<bool>`，`SetReady()` 在 `kDataLoaded` 调用。

### 4.3 为什么是“放弃”而不是“降级”

| 方案                                      | 旧 SKEE 行为                      | 代码成本                                           | 用户预期               |
| ----------------------------------------- | --------------------------------- | -------------------------------------------------- | ---------------------- |
| **降级**（门禁 + 恢复 `OnEquipEvent`）    | 穿脱高跟可用，Modex 不可用        | 长期维护两条 mesh 更新路径，`a88e85b` 重构部分回退 | “能用但不全”           |
| **放弃**（门禁 + 明确错误 + 引导 v3.0.2） | 不崩、无功能、日志明确指向 v3.0.2 | 单一代码路径，无 `#ifdef`，无回退                  | “不支持的版本请用旧版” |

**选择放弃的理由：**

1. `a88e85b` 之后 `OnAttach` 是唯一 mesh 更新入口，恢复 `OnEquipEvent` 等于回退 v3.0.3 的架构转向，代价大于收益。
2. SE 1.5.97 用户已被社区锁定在 RaceMenu 0.4.16，**没有升级路径**，长期维护双路径是在为“永远不会变新”的环境付利息。
3. v3.0.2 已经稳定，作为 SE 最终版足够；SE 用户不需要 v3.0.6 的任何新功能。
4. 单一代码路径 + 运行时检测，比 `#ifdef` 双编译清晰得多。

### 4.4 为什么这个方案同时修两个问题

| 问题                         | 修复前                                    | 修复后                                                    |
| ---------------------------- | ----------------------------------------- | --------------------------------------------------------- |
| `AddInterface` vtable 越界崩 | 3.0.6beta 在 `kDataLoaded` 调，旧 SKEE 崩 | `GetVersion() >= 2` 拦截，旧 SKEE 不崩、有明确 ERROR 日志 |
| IED / SDS / 着色器冲突       | 3.0.5 的 `OnAttach` 在窗口期内做 D3D      | `OnAttach` 用 `IsReady()` 推迟到 `kDataLoaded` 之后       |
| 接口交换时机                 | 3.0.6beta 违反 SKEE 约定                  | 回 `kPostPostLoad`，符合 SKEE 0.4.19.10 约定              |
| 代码维护                     | 双版本构建 + 潜在双路径                   | 单一源码路径，运行时检测，无 `#ifdef`                     |

---

## 5. SE 1.5.97 支持策略

### 5.1 官方立场

- **SE 1.5.97 最终版 = v3.0.2**。Release 页面 v3.0.2 标注“SE 1.5.97 最终版”。
- **v3.0.6+ 仅发布 AE 版**，最低 SKEE 版本要求 **RaceMenu 0.4.19.13**（含 `IActorUpdateManager::AddInterface`）。
- SE 用户若误装 v3.0.6+：**不崩**，日志出现 `ActorUpdateManager version=1 is too old ... must use v3.0.2`，插件无功能。

### 5.2 源码策略

- **不做 `#ifdef SE` / `#ifdef AE` 双路径**。代码库统一，运行时用 `GetVersion()` 检测 SKEE 能力。
- 构建系统可保留 SE/AE 双 preset（历史延续），但 **Release 只发 AE 产物**；SE preset 仅用于本地验证“旧 SKEE 不崩”这一条。
- 若将来需要彻底移除 SE 构建，另开提交处理，不在本次范围。

### 5.3 用户沟通

- **Nexus v3.0.2 页面**：置顶注明“SE 1.5.97 最终版，后续版本仅支持 AE”。
- **v3.0.6 Release notes**：首段写明最低 SKEE 版本，附“SE 用户请下载 v3.0.2”链接。
- **双版本 DLL 环境**（GOODNEWSGN 案例）：文档明确“不支持同时加载多个 `AdeptivePantyhose*.dll`，必须清理 `Data\SKSE\Plugins\` 后只保留一个”。

---

## 6. 待确认事项

### 6.1 用户侧（仅作记录，不阻塞发布）

**AvatarWenyihaobb**：

1. `Data\SKSE\Plugins\skee64.dll` 的文件版本（右键 → 属性 → 详细信息），确认是否在 `0.4.19.13` 之前
2. 3.0.6beta 的完整日志，确认出现 `ActorUpdateManager version=1 is too old` ERROR

**GOODNEWSGN**：

1. 清理 `Data\SKSE\Plugins\` 下所有 `AdeptivePantyhose*.dll`，只保留一个 3.0.6beta，再测

### 6.2 源码侧

```powershell
# 确认 c1b408f 版本 ActorUpdateManager::GetVersion() 返回值，决定 >= 2 边界是否覆盖 c1b408f
git show c1b408f:skee64/ActorUpdateManager.cpp | Select-String "GetVersion" -Context 0,5
```

- 若返回 1：`>= 2` 会同时排除 `c1b408f` 这一档（保守但安全，`c1b408f` 是 1.6.640 首发，用户极少）
- 若返回 2：`>= 2` 只排除 `c1b408f` 之前，边界更精确

---

## 7. 验证清单

实现修复后：

| 场景                                | 期望                                                                                  |
| ----------------------------------- | ------------------------------------------------------------------------------------- |
| 旧 SKEE（`c1b408f` 之前）+ 3.0.6+   | `ActorUpdateManager version=1 is too old ... must use v3.0.2` ERROR，**不崩**，无功能 |
| 新版 SKEE（`7694eab` 之后）+ 3.0.6+ | `ActorUpdateManager version=2` INFO，`AttachListener installed`，**不崩**             |
| 新版 SKEE + IED / SDS / 着色器      | `OnAttach` 推迟到 `kDataLoaded` 之后，不冲突                                          |
| 玩家穿脱高跟                        | `OnAttach` 路径正常                                                                   |
| Modex 三件同穿                      | `OnAttach` 路径正常                                                                   |
| 双版本 DLL（GOODNEWSGN）            | **不支持**，文档说明必须清理                                                          |
| SE 1.5.97 用户                      | 使用 v3.0.2，功能正常（v3.0.2 架构）                                                  |

---

## 8. 变更记录

### v3.0.6（待发布）

**决策**：

- **放弃 SE 1.5.97 支持，SE 最终版冻结 v3.0.2**。v3.0.6+ 仅发布 AE 版，最低 SKEE 版本 **RaceMenu 0.4.19.13**。
- 源码不 `#ifdef` 双路径，运行时 `GetVersion() >= 2` 检测。

**修复**：

- `Initialize()` 回 `kPostPostLoad`（符合 SKEE 0.4.19.10 约定）
- `Initialize()` 里 `AddInterface` 前加 `GetVersion() >= 2` 检查，旧版 SKEE **跳过并记录 ERROR**，引导用户下载 v3.0.2
- `AttachListener::OnAttach` 加 `IsReady()` 门禁，实际工作推迟到 `kDataLoaded` 之后（解决 IED / SDS / 着色器冲突）
- 不恢复 `OnEquipEvent` 的 mesh 更新路径（不回退 `a88e85b`）

**根因**：`IActorUpdateManager::AddInterface` 是 SKEE `c1b408f`（约 2022 年，1.6.640 / RaceMenu 0.4.19.13）才加入的虚函数。SE 1.5.97 被锁定在 RaceMenu 0.4.16，vtable 只有 8 个虚函数，`AddInterface` 调用越界，`RDX=5`（`GetVersion()` 残留）被当指针，崩在 `0xD`。且 v3.0.3 起 mesh 更新只走 `OnAttach`，旧 SKEE 下 `AttachListener` 注册不上，功能完全缺失——无“降级可用”空间。

**文档**：

- 新增本文档
- `Architecture.md` §11 已知限制同步更新
- Release notes 新增“SE 1.5.97 最终版 v3.0.2”说明
- README 新增最低 SKEE 版本要求

---

## 9. 相关文件

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
