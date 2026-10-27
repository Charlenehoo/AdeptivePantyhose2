````markdown
# OnAttach 驱动 mesh 更新：修复脚趾甲首次同帧穿消失的方案

**版本**：v3.0.3 设计草案
**日期**：2026-10-27
**关联**：[`toenail-report-2026-10-27.md`](toenail-report-2026-10-27.md) 方案 B 精化
**状态**：设计完成，待实现与验证

---

## 1. 问题回顾

rudeboybox 在 v3.0.2 报告：NPC **首次**同帧穿脚趾甲 + 裤袜/鞋时，脚趾甲闪烁一瞬后消失；必须先单独穿脚趾甲（“初始化”），之后再同帧穿才正常。初始化后，同一 NPC 实例上永久正常；穿 vanilla 鞋（不触发本插件规则）不受影响。

报告 §3.2 定位根因：`MorphFileCache::ApplyMorph` 在 `!bodyData` 时用**当前顶点**创建 `SHAPEDATA` 作为基准快照，且该分支不受 `isAttaching` 保护。v3.0.2 的 `OnEquipEvent` 在 `TESEquipEvent` 期间同步调用 `UpdateModelWeight`，此时脚趾甲 mesh 尚未 attach 就绪，`SHAPEDATA` 捕获了错误的 base 并被永久固化。

“初始化后永久正常”是因为“先单独穿脚趾甲”让 `UpdateModelWeight` 在 mesh 就绪后执行，`SHAPEDATA` 捕获了正确 base；之后 `if (!bodyData)` 不再成立，不会重新捕获。

---

## 2. 方案核心

**把 mesh 初始化的驱动源从 `UpdateModelWeight`（事件时全 actor 遍历）改为 `OnAttach`（每个 mesh 就绪时精确处理）。**

`IAddonAttachmentInterface::OnAttach` 是 SKEE 在 armor addon mesh 挂到场景图后调用的回调。此时该 mesh 的顶点数据已就绪——否则 SKEE 自身的 `BodyMorphInterface::OnAttach` 也会在错误时机创建 `SHAPEDATA`，而 SKEE 长期稳定运行反证了这个时机可靠。

**关键设计**：在 `OnAttach` 中，**只对“其他已存在的 mesh”调用 `ApplyVertexDiff(..., false)`，当前 mesh 交给 SKEE 的 `BodyMorphInterface::OnAttach`（`isAttaching=true`）处理**。这样：

- 当前 mesh 的 `SHAPEDATA` 由 SKEE 用刚就绪的顶点创建，基准正确；
- 其他已存在 mesh 基于各自正确的 `SHAPEDATA` 重置后重新应用新 morph 值；
- 尚未 attach 的 mesh 不会被触及（`GetWornArmor` 在事件期间返回旧值，或 `bipedData` 尚无记录），避免在未就绪 mesh 上创建错误 `SHAPEDATA`。

---

## 3. `ApplyVertexDiff` 接口详解

### 3.1 签名与参数名不一致

`IPluginInterface.h`：

```cpp
virtual void ApplyVertexDiff(TESObjectREFR* refr, NiAVObject* rootNode, bool erase = false) = 0;
```
````

`BodyMorphInterface` 实现：

```cpp
void Impl_ApplyVertexDiff(TESObjectREFR* refr, NiAVObject* rootNode, bool attach)
{
    if (!refr || !rootNode) return;
    if (g_enableBodyMorph)
        morphCache.ApplyMorphs(refr, rootNode, attach, true);
}
```

接口里叫 `erase`，实现里叫 `attach`，最终传给 `MorphCache::ApplyMorphs` 的形参是 `isAttaching`。**以 `isAttaching` 为准**，`erase` 这个名字容易误导。

### 3.2 第三个参数控制的分支

`MorphFileCache::ApplyMorph` 的核心逻辑：

```cpp
NiPointer<NiBinaryExtraData> bodyData = GetExtraData(bodyGeometry, "SHAPEDATA");
bool existingMorphs = !isAttaching && bodyData;

if (bodyMorph.second.HasMorphs(refr) || existingMorphs)
{
    // DeepCopy skin partition
    if (bodyData && newSkinPartition && !isAttaching)
    {
        // 用 SHAPEDATA 重置 vertex data 到 base
        memcpy(..., bodyData->m_data, ...);
    }

    if (!bodyData)  // 不受 isAttaching 保护
    {
        // 用当前 m_RawVertexData 创建 SHAPEDATA
        bodyData = NiBinaryExtraData::Create("SHAPEDATA", ...);
        bodyGeometry->AddExtraData(bodyData);
    }

    // 应用 morph delta
}
```

三种模式：

| `isAttaching` | `bodyData` 存在 | 行为                                                                      | 适用场景                        |
| :-----------: | :-------------: | ------------------------------------------------------------------------- | ------------------------------- |
|    `true`     |       否        | 用**当前就绪顶点**创建 `SHAPEDATA`，应用 morph                            | mesh 刚 attach，首次初始化      |
|    `false`    |       是        | 用 `SHAPEDATA` 重置到 base，应用 morph                                    | 已存在 mesh 重新应用新 morph 值 |
|    `false`    |       否        | 用**当前顶点**创建 `SHAPEDATA`（危险：当前顶点可能不是 base），应用 morph | 不应发生；若发生则固化错误基准  |

**关键**：`!bodyData` 创建 `SHAPEDATA` 的分支**不受 `isAttaching` 保护**。所以“对未就绪 mesh 调用 `isAttaching=false`”是危险路径——这正是 v3.0.2 的 bug 所在。

### 3.3 `rootNode` 的处理范围

`ApplyVertexDiff` **只处理传入的 `rootNode` 子树**，不遍历 actor 的其他 biped 节点：

```cpp
BSGeometry* geometry = rootNode->GetAsBSGeometry();
if (geometry) {
    execMorphUpdate(geometry);
} else {
    VisitObjects(rootNode, [&](NiAVObject* object) {
        if (BSGeometry* bodyGeometry = object->GetAsBSGeometry()) {
            if (bodyGeometry->m_name == nodeName.data) {
                execMorphUpdate(bodyGeometry);
            }
        }
        return false;
    });
}
```

一次调用只覆盖一个 armor addon 的 mesh 树。要更新 actor 的所有 mesh，必须对每个 armor addon 分别调用——这是 `UpdateModelWeight`（内部 `MorphCache::UpdateMorphs` → `VisitBipedNodes`）的职责。

### 3.4 `defer` 参数实际无效

`Impl_ApplyVertexDiff` 传的第四个参数 `true` 在 `MorphFileCache::ApplyMorphs` 里被立即覆盖：

```cpp
defer = (*g_main)->threadId != GetCurrentThreadId();
```

最终是否 defer 取决于调用线程：主线程同步执行，非主线程丢给 `g_task`。`OnAttach` 和 `UpdateModelWeight` 通常都在主线程，所以是同步的。

---

## 4. 观察者顺序与 SKEE 自身行为

### 4.1 我们是最后一个观察者

SKEE 在 `Load` 阶段注册自己的观察者：

```cpp
g_actorUpdateManager.AddInterface(&g_skeletonExtenderInterface);
g_actorUpdateManager.AddInterface(&g_bodyMorphInterface);
g_actorUpdateManager.AddInterface(&g_overlayInterface);
g_actorUpdateManager.AddInterface(&g_overrideInterface);
g_actorUpdateManager.AddInterface(&g_itemDataInterface);
if (g_enableTintHairSlot) {
    g_actorUpdateManager.AddInterface(&g_tintMaskInterface);
}
```

我们在 `kPostPostLoad` 通过 `IActorUpdateManager::AddInterface` 注册，必然追加到 vector 末尾。`ActorUpdateManager::OnAttach` 按 vector 顺序遍历，所以**我们的回调执行时，`BodyMorphInterface::OnAttach` 已经跑完**。

### 4.2 SKEE 的 OnAttach 已经在做我们想做的事

```cpp
void BodyMorphInterface::OnAttach(...)
{
    if (HasMorphs(refr))
    {
        if (g_actorUpdateManager.isReverting())
            g_actorUpdateManager.AddBodyUpdate(refr->formID);
        else
            ApplyVertexDiff(refr, object, true);   // isAttaching = true
    }
}
```

只要 `actorMorphs` 里该 actor 有记录（`SetAbsolute` 写入的），`HasMorphs(refr)` 为真，SKEE 就会在**当前 mesh** 上以 `isAttaching=true` 调用 `ApplyVertexDiff`，用刚就绪的顶点创建正确的 `SHAPEDATA`。

**所以当前 mesh 不需要我们处理**。我们的 listener 只需处理“其他已存在的 mesh”。

---

## 5. 时序分析

### 5.1 同帧首次穿脚趾甲 + 裤袜

```
1. TESEquipEvent(脚趾甲) → EvaluateAndApply → SetAbsolute("NoHeel", 1.0)
2. TESEquipEvent(裤袜)   → EvaluateAndApply → SetAbsolute("NoHeel", 1.0)（幂等）
3. 引擎 attach 脚趾甲 mesh
   ├─ SKEE BodyMorphInterface::OnAttach → ApplyVertexDiff(脚趾甲, true)
   │    └─ 用刚就绪顶点创建 SHAPEDATA ✅
   └─ 我们的 AttachListener::OnAttach
        ├─ 遍历 GetWornArmor → 裤袜可能已 attach，脚趾甲可能不在（旧值）
        └─ 对找到的其他 mesh 调 ApplyVertexDiff(..., false)
4. 引擎 attach 裤袜 mesh
   ├─ SKEE BodyMorphInterface::OnAttach → ApplyVertexDiff(裤袜, true)
   │    └─ 用刚就绪顶点创建 SHAPEDATA ✅
   └─ 我们的 AttachListener::OnAttach
        └─ 对脚趾甲（已存在）调 ApplyVertexDiff(脚趾甲, false)
             └─ 用正确 SHAPEDATA 重置，应用 NoHeel=1.0 ✅
```

两个 mesh 的 `SHAPEDATA` 都由 SKEE 在正确时机创建，我们的 listener 只负责让已存在的 mesh 响应新 morph 值。**问题解决**。

### 5.2 已穿脚趾甲 + 裤袜，再穿高跟鞋

```
1. TESEquipEvent(高跟鞋) → EvaluateAndApply → SetAbsolute("NoHeel", 0.0)
2. 引擎 attach 高跟鞋 mesh
   ├─ SKEE BodyMorphInterface::OnAttach → ApplyVertexDiff(高跟鞋, true)（创建 SHAPEDATA）
   └─ 我们的 AttachListener::OnAttach
        └─ 遍历 GetWornArmor → 找到脚趾甲、裤袜
             └─ 对每个调 ApplyVertexDiff(..., false)
                  └─ 用各自正确 SHAPEDATA 重置，应用 NoHeel=0.0 ✅
```

脚趾甲和裤袜的 mesh 已存在，OnAttach 不会重新触发，但我们的 listener 在**高跟鞋的 OnAttach** 中主动遍历并更新了它们。**这个场景也解决了**——这是纯方案 B（只依赖 SKEE 的 `BodyMorphInterface::OnAttach`）无法覆盖的，因为 SKEE 的 OnAttach 只处理当前 mesh。

### 5.3 读档

`kPostLoadGame` 在存档完全恢复后触发。此时玩家 mesh 已 attach，各 mesh 的 `SHAPEDATA` 应已在各自 OnAttach 中正确创建。`OnActorLoaded` 保留 `UpdateModelWeight` 作为兜底：mesh 已就绪，`SHAPEDATA` 已正确，`UpdateModelWeight` 用正确 base 重置并应用新 morph。

不依赖“引擎是否为读档装备触发 OnAttach”这个未验证假设。

---

## 6. 关键依赖与假设

### 6.1 依赖 `GetWornArmor` 在 OnAttach 期间返回旧值

我们利用“事件期间 `GetWornArmor` 返回旧值”来天然排除当前正在 attach 的 mesh。这个行为在 `TESEquipEvent` 期间已被报告 §7.7 确认，但 **OnAttach 期间是否仍然如此，未直接验证**。

**鲁棒性**：即使 OnAttach 期间返回新值，方案仍然正确。如果当前 armor 出现在 `GetWornArmor` 里，`VisitArmorAddon` 会找到它的 mesh（`bipedData` 已更新），然后调用 `ApplyVertexDiff(mesh, false)`。此时 `SHAPEDATA` 刚被 SKEE 用正确 base 创建，`isAttaching=false` 会用它重置再应用新 morph——结果正确，只是多一次调用。所以“返回旧值”只影响遍历的精确性，不影响正确性。

**建议**：实现时加一条 DEBUG 日志确认实际行为：

```cpp
REX::DEBUG("[OnAttach] refr={:08X} attaching={:08X} worn(kFeet)={:08X}",
           a_refr->GetFormID(), a_armor->GetFormID(),
           a_refr->GetWornArmor(Slot::kFeet) ? a_refr->GetWornArmor(Slot::kFeet)->GetFormID() : 0);
```

### 6.2 依赖 `bipedData` 对未 attach mesh 无记录

`VisitArmorAddon` 依赖 `bipedData` 里已有该 mesh 的记录。`AttachBipedObject` 尚未执行时，`bipedData` 里没有记录，`VisitArmorAddon` 找不到它，跳过。**不会在未就绪的 mesh 上创建 `SHAPEDATA`**，这是方案的核心安全性来源。

### 6.3 假设 `OnAttach` 是同步派发的

从 SKEE 的 `AttachBipedObject_Hooked` 看：

```cpp
NiAVObject* AttachBipedObject_Hooked(...)
{
    NiAVObject* retVal = AttachBipedObject_Original(...);
    // ...
    InstallArmorAddonHook(reference, bipedInfo->unk10[bipedIndex], bipedInfo->root, retVal);
    return retVal;
}
```

`InstallArmorAddonHook` 同步调用 `g_actorUpdateManager.OnAttach`。所以每个 mesh 的 OnAttach 在 `AttachBipedObject` 返回后立即同步派发。

**未验证**：同一次装备操作涉及多个 armor addon 时，多个 `AttachBipedObject` 调用是否都在同一帧内完成。如果是，我们的 listener 在每个 OnAttach 中遍历 `GetWornArmor` 时，可能看到部分 mesh 已 attach、部分未 attach——但由于我们只对已存在 mesh 调 `ApplyVertexDiff(false)`，未 attach 的自然跳过，后续它们的 OnAttach 会处理。**跨帧也不影响正确性**，只影响“一次 OnAttach 中更新多少 mesh”。

---

## 7. 实现结构

### 7.1 `AttachListener`

```cpp
// src/Integration/SKEE/AttachListener.h
#pragma once

class AttachListener final : public IAddonAttachmentInterface {
public:
    static auto GetSingleton() -> AttachListener&;

    void OnAttach(RE::TESObjectREFR* a_refr,
                  RE::TESObjectARMO* a_armor,
                  RE::TESObjectARMA* a_addon,
                  RE::NiAVObject* a_object,
                  bool a_isFirstPerson,
                  RE::NiNode* a_skeleton,
                  RE::NiNode* a_root) override;

private:
    AttachListener() = default;
};
```

```cpp
// src/Integration/SKEE/AttachListener.cpp
auto AttachListener::GetSingleton() -> AttachListener& {
    static AttachListener s_singleton;
    return s_singleton;
}

void AttachListener::OnAttach(RE::TESObjectREFR* a_refr,
                              RE::TESObjectARMO* a_armor,
                              RE::TESObjectARMA* a_addon,
                              RE::NiAVObject* a_object,
                              bool a_isFirstPerson,
                              RE::NiNode* a_skeleton,
                              RE::NiNode* a_root) {
    if (a_refr == nullptr || a_armor == nullptr || a_object == nullptr) {
        return;
    }

    // 当前 mesh 的 SHAPEDATA 由 SKEE 的 BodyMorphInterface::OnAttach
    // 以 isAttaching=true 处理（我们在 vector 末尾，它已经跑完）。
    // 我们只负责让"其他已存在的 mesh"响应新 morph 值。

    auto& engine = MorphRules::Engine::MorphRuleEngine::GetSingleton();
    if (!engine.HasAnyRuleFor(a_refr)) {
        return;
    }

    // 遍历该 actor 当前所有已 attach 的 armor addon mesh，
    // 对每个调 ApplyVertexDiff(..., false)。
    // GetWornArmor 在事件期间返回旧值，天然排除当前 mesh；
    // 即使返回新值，对当前 mesh 调 false 也无害（SHAPEDATA 已正确）。
    SKEEIntegration::GetSingleton().ApplyVertexDiffToWornMeshes(*a_refr);
}
```

### 7.2 `SKEEIntegration` 新增方法

```cpp
// SKEEIntegration.h
void ApplyVertexDiffToWornMeshes(RE::TESObjectREFR& a_refr);

// SKEEIntegration.cpp
void SKEEIntegration::ApplyVertexDiffToWornMeshes(RE::TESObjectREFR& a_refr) {
    if (m_bodyMorph == nullptr) {
        return;
    }

    // 对每个 biped 槽，找到已 attach 的 armor addon mesh，
    // 调 ApplyVertexDiff(mesh, false)。
    // 未 attach 的 mesh 不在 bipedData 里，VisitArmorAddon 找不到，自然跳过。
    for (UInt32 slot = 0; slot < 32; ++slot) {
        auto* armor = a_refr.GetWornArmor(static_cast<Slot>(1u << slot));
        if (armor == nullptr) {
            continue;
        }
        for (UInt32 i = 0; i < armor->armorAddons.count; ++i) {
            RE::TESObjectARMA* addon = nullptr;
            if (!armor->armorAddons.GetNthItem(i, addon) || addon == nullptr) {
                continue;
            }
            VisitArmorAddon(&a_refr, armor, addon,
                [this](bool, RE::NiNode*, RE::NiAVObject* a_object) {
                    if (a_object != nullptr) {
                        m_bodyMorph->ApplyVertexDiff(&a_refr, a_object, false);
                    }
                });
        }
    }
}
```

> `VisitArmorAddon` 是 SKEE 的 `NifUtils.h` 里的工具函数，签名与 SKEE 内部一致。需要确认 vendor/SKEE 头文件里是否导出它——如果没有，需要用 `VisitBipedNodes` 自己写等价遍历。

### 7.3 `MorphRuleEngine` 新增辅助查询

```cpp
// MorphRuleEngine.h
[[nodiscard]] auto HasAnyRuleFor(RE::Actor* a_actor) const -> bool;
```

用于在 listener 中快速过滤“该 actor 是否有任何 morph 规则关心”。如果所有 morph 的 `TriggeredBy` 槽都为空，或 actor 没有任何已穿装备匹配规则，就不必遍历。实现可以简化为“morphs 非空即返回 true”，或者更精确地检查 `GetWornArmor`。初期用最简版本即可。

### 7.4 `OnEquipEvent` 改动

**移除** `UpdateModelWeight` 调用，保留 `SetAbsolute`：

```cpp
void MorphRuleEngine::OnEquipEvent(RE::Actor* a_actor, RE::TESObjectARMO* a_armor, bool a_equipped) {
    // ... 求值逻辑不变 ...

    // 移除：
    // if (anyApplied) {
    //     SKEEIntegration::GetSingleton().UpdateModelWeight(*a_actor);
    // }

    // mesh 更新由 AttachListener::OnAttach 驱动：
    // - 当前 mesh：SKEE BodyMorphInterface::OnAttach（isAttaching=true）
    // - 其他已存在 mesh：AttachListener::OnAttach 中的 ApplyVertexDiff(false)
}
```

### 7.5 `OnActorLoaded` 保持不变

保留 `UpdateModelWeight` 作为读档兜底。读档时 mesh 已就绪，`SHAPEDATA` 已正确，调用安全。

### 7.6 生命周期

`AttachListener` 是单例，在 `main.cpp` 的 `kPostPostLoad`（`SKEEIntegration::Initialize` 之后）注册：

```cpp
auto* updateManager = static_cast<IActorUpdateManager*>(
    exchange.interfaceMap->QueryInterface("ActorUpdateManager"));
if (updateManager != nullptr) {
    updateManager->AddInterface(&AttachListener::GetSingleton());
    m_updateManager = updateManager;  // 保存用于卸载
}
```

插件卸载时（如果 SKSE 提供卸载回调）调用 `RemoveInterface`。SKEE 的 `AddInterface` 传裸指针，不持有所有权，listener 必须是静态存储期对象。

---

## 8. 与 v3.0.3 防抖方案的对比

| 维度                          | v3.0.3 防抖                                                                | 本方案                                            |
| ----------------------------- | -------------------------------------------------------------------------- | ------------------------------------------------- |
| 核心思路                      | 延迟 `UpdateModelWeight` 到下一帧                                          | 用 `OnAttach` 精确驱动每个 mesh 的初始化          |
| 首次同帧穿脚趾甲 + 裤袜       | 一帧后 mesh 可能仍未就绪，错误 `SHAPEDATA` 仍可能被固化                    | 当前 mesh 由 SKEE OnAttach 用就绪顶点初始化，正确 |
| 已穿裤袜再穿高跟鞋            | 延迟 `UpdateModelWeight` 会更新已存在 mesh，但新 mesh 的初始化时机仍不确定 | 高跟鞋 OnAttach 中遍历并更新脚趾甲/裤袜，正确     |
| 已存在错误 `SHAPEDATA` 的存档 | 无法修复                                                                   | 无法修复（`if (!bodyData)` 不成立），需卸载重载   |
| 时序不确定性                  | “一帧”是启发式，mesh attach 可能跨帧                                       | 不依赖帧数，依赖 `OnAttach` 信号本身              |
| 架构影响                      | 引入跨帧状态（`m_pendingUpdates`、`m_updateScheduled`）                    | 无状态，listener 每次独立判断                     |
| 代码量                        | 小（防抖队列）                                                             | 中（listener + 遍历 + 接口封装）                  |

---

## 9. 边界情况

### 9.1 已存在的错误 `SHAPEDATA`

方案 B 只保证**新 attach 的 mesh** 正确。如果用户存档中某些 NPC 已在 v3.0.2 下被错误初始化，`SHAPEDATA` 已用错误 base 创建，方案 B 不会干预（`if (!bodyData)` 不成立），这些 NPC 需卸载重载才能修复。

**决策**：暂不处理。理由：

- 错误 `SHAPEDATA` 的影响是“脚趾甲消失”，不是崩溃；
- 主动重置 `SHAPEDATA` 会影响所有 armor 的 morph 状态，风险高；
- NPC 卸载重载是自然修复路径；
- 如果反馈集中，再考虑提供控制台命令手动重置某个 actor。

### 9.2 第一/第三人称重复 OnAttach

同一 armor 会触发两次 `OnAttach`（`a_isFirstPerson` true/false）。两套 mesh 有各自的 `SHAPEDATA`，都需要处理。`AttachListener` 不区分第一/第三人称，`ApplyVertexDiffToWornMeshes` 遍历所有 biped 节点，两套都会被覆盖。

如果确认 NPC 只渲染第三人称、且第一人称脚部不需要 morph，可以加 `if (a_isFirstPerson) return;` 减少开销。但玩家角色的第一人称脚部也可能需要 morph，是否跳过取决于实际需求。**初期不做优化**。

### 9.3 性能

每次 OnAttach 遍历 `GetWornArmor`（32 个槽）并对每个 armor addon 调 `ApplyVertexDiff(false)`。一次装备操作有 N 个 armor 触发 OnAttach，总调用次数 O(N²)。脚部/裤袜场景 N 通常 1–3，可接受。全身装备（10+ 件）同帧批量更换时可能略多，但 OnAttach 只在装备变化时触发，不是每帧，实际影响有限。

如果担心，可以在 listener 里按 `a_refr` 维护“本批次已处理”集合，但会引入状态，与无状态求值原则冲突。**初期不做**，实测后再评估。

### 9.4 `VisitArmorAddon` 的可用性

`VisitArmorAddon` 是 SKEE 的 `NifUtils.h` 里的工具函数。当前 `vendor/SKEE` 只包含 `IPluginInterface.h`，没有 `NifUtils.h`。如果不想引入更多 SKEE 头文件依赖，可以用 `VisitBipedNodes`（同样在 `NifUtils.h`）或自己写等价遍历：

```cpp
// 遍历 bipedData 找到已 attach 的 mesh，等价于 VisitArmorAddon 的核心逻辑
for (UInt32 k = 0; k <= 1; ++k) {
    auto* weightModel = a_refr.GetBiped(k);
    if (weightModel == nullptr || weightModel->bipedData == nullptr) continue;
    for (UInt32 i = 0; i < 42; ++i) {
        auto& data = weightModel->bipedData->unk10[i];
        if (data.object != nullptr && data.armor == armor) {
            m_bodyMorph->ApplyVertexDiff(&a_refr, data.object, false);
        }
    }
    for (UInt32 i = 0; i < 42; ++i) {
        auto& data = weightModel->bipedData->unk13C0[i];
        if (data.object != nullptr && data.armor == armor) {
            m_bodyMorph->ApplyVertexDiff(&a_refr, data.object, false);
        }
    }
}
```

需要确认 `RE::Biped::Data` 的字段名在 CommonLibSSE 里是否如此暴露——不同版本可能不同。实现时以实际头文件为准。

---

## 10. 验证清单

实现后让 rudeboybox（或本地）验证：

| 场景                              | 期望                                  |
| --------------------------------- | ------------------------------------- |
| 新 NPC，首次同帧穿脚趾甲 + 裤袜   | 脚趾甲正常显示，不闪烁消失            |
| 新 NPC，先单独穿脚趾甲，再穿裤袜  | 正常（与修复前一致）                  |
| 已初始化 NPC，同帧穿脚趾甲 + 裤袜 | 正常                                  |
| 已穿脚趾甲 + 裤袜，再穿高跟鞋     | 脚趾甲和裤袜响应 `NoHeel=0`，正常     |
| 已穿高跟鞋，再穿脚趾甲            | 脚趾甲 attach 时应用 `NoHeel=0`，正常 |
| 玩家读档，已穿脚趾甲 + 裤袜       | 正常（`OnActorLoaded` 兜底）          |
| NPC 卸载重载后同帧穿              | 正常（OnAttach 重新初始化）           |
| v3.0.2 存档中已错误初始化的 NPC   | 可能仍异常，卸载重载后修复            |

**日志验证**：在 `AttachListener::OnAttach` 中打印 `refr`、`armor`、`GetWornArmor(kFeet)` 的 formID，确认 OnAttach 期间 `GetWornArmor` 的实际返回值。同时打印 `ApplyVertexDiffToWornMeshes` 实际处理了哪些 armor，确认遍历范围符合预期。

---

## 11. 待确认事项

1. **`GetWornArmor` 在 OnAttach 期间的实际行为**：加日志验证。即使返回新值方案仍正确，但日志帮助理解遍历范围。
2. **`VisitArmorAddon` 的可用性**：确认 `vendor/SKEE` 是否需要新增 `NifUtils.h`，或改用 `VisitBipedNodes` / 手写遍历。
3. **`RE::Biped::Data` 字段名**：以 CommonLibSSE 实际头文件为准。
4. **`IActorUpdateManager` 的查询名**：`"ActorUpdateManager"` 是 SKEE 内部注册名，换 RaceMenu 版本时需核对。
5. **插件卸载时的 `RemoveInterface`**：SKSE 是否提供卸载回调？如果没有，静态单例在进程生命周期内一直有效，无需移除。

---

**文档结束。**

```

```
