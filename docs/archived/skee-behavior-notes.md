> ⚠️ **历史快照，已冻结**。当前文档见 [`../skee-behavior-notes.md`](../skee-behavior-notes.md)。
> 本文档描述的 v3.0.x 行为已被后续版本取代，保留仅供追溯。

# SKEE BodyMorph 行为笔记

**日期**：2026-10-27
**适用**：SKEE64（RaceMenu 的 BodyMorph 接口），v3.0.2 / v3.0.3 调查期间
**目的**：记录本插件依赖的 SKEE 行为，附源码引用与日志证据。`docs/Architecture.md` 描述插件自身架构，本文档描述被依赖的外部行为。

---

## 1. 摘要

本插件依赖 SKEE 的以下几项行为：

1. `BodyMorphInterface::OnAttach` 在 mesh attach 后、顶点就绪时调用。
2. `ApplyVertexDiff(refr, object, isAttaching)` 只处理传入的 `object` 子树，不遍历 actor 的其他 biped 节点。
3. `MorphFileCache::ApplyMorph` 的 `!bodyData` 分支（创建 `SHAPEDATA`）不受 `isAttaching` 保护。这是首次错误的根本来源。
4. `isAttaching=true` 跳过用已有 `SHAPEDATA` 重置；`false` 先用 `SHAPEDATA` 重置再应用 morph。
5. `ActorUpdateManager::OnAttach` 按 observer 注册顺序遍历。我们在 vector 末尾，回调时 SKEE 自身的 `BodyMorphInterface::OnAttach` 已完成。
6. `UpdateModelWeight` 遍历 actor 所有 biped 节点，包括尚未 attach 的 mesh。
7. `TESEquipEvent` 派发期间 `GetWornArmor` 返回旧值。

---

## 2. 行为 1：`BodyMorphInterface::OnAttach` 的调用时机

### 2.1 源码

`skee64/BodyMorphInterface.cpp`：

```cpp
void BodyMorphInterface::OnAttach(TESObjectREFR* refr, TESObjectARMO* armor, TESObjectARMA* addon,
                                  NiAVObject* object, bool isFirstPerson, NiNode* skeleton, NiNode* root) {
    if (HasMorphs(refr)) {
        if (g_actorUpdateManager.isReverting())
            g_actorUpdateManager.AddBodyUpdate(refr->formID);
        else
            ApplyVertexDiff(refr, object, true);   // isAttaching = true
    }
}
```

调用链：

```
SKEE: AttachBipedObject_Hooked
    ├── AttachBipedObject_Original(mesh 挂到场景图)
    ├── InstallArmorAddonHook(reference, params, boneTree, resultNode)
    │
    ▼
ActorUpdateManager::OnAttach（按注册顺序遍历 observer）
    ├── BodyMorphInterface::OnAttach
    ├── OverlayInterface::OnAttach
    ├── OverrideInterface::OnAttach
    ├── ...
```

### 2.2 时机可靠性

`OnAttach` 在 `AttachBipedObject_Original` 返回后立即同步调用。此时 mesh 已挂到场景图，顶点数据可用。如果顶点未就绪，SKEE 自己的 `ApplyVertexDiff(..., true)` 会创建错误的 `SHAPEDATA`，SKEE 长期稳定运行反证了时机可靠。

### 2.3 条件分支

`HasMorphs(refr)` 为假时 SKEE **不调用** `ApplyVertexDiff`，不创建 `SHAPEDATA`。这是 v3.0.3 `AttachListener` 兜底 `ApplyCurrent` 存在的原因。

### 2.4 日志证据

v3.0.3 下，`[Attach]` 时当前 mesh 的 `hasShapeData`：

```
[Attach] refr=00000014 attachingArmor=FE05CBC9 attachingAddon=FE05CBBF isFP=false object='' hasShapeData=false
[W] [Attach] current mesh '' has no SHAPEDATA, initializing with isAttaching=true
[ApplyCurrent] refr=00000014 mesh=''
```

脚趾甲（`FE05CBC9`）attach 时 `hasShapeData=false`，说明 SKEE 的 `BodyMorphInterface::OnAttach` 没有为它创建 `SHAPEDATA`（可能 `HasMorphs` 为假，或 TRI 不匹配）。我们以 `isAttaching=true` 兜底。

---

## 3. 行为 2：`ApplyVertexDiff` 只处理传入的 `object` 子树

### 3.1 源码

`IPluginInterface.h`：

```cpp
virtual void ApplyVertexDiff(TESObjectREFR* refr, NiAVObject* rootNode, bool erase = false) = 0;
```

`BodyMorphInterface.cpp`：

```cpp
void BodyMorphInterface::Impl_ApplyVertexDiff(TESObjectREFR* refr, NiAVObject* rootNode, bool attach) {
    if (!refr || !rootNode) return;
    if (g_enableBodyMorph)
        morphCache.ApplyMorphs(refr, rootNode, attach, true);
}
```

`MorphFileCache::ApplyMorph` 的入口：

```cpp
BSGeometry* geometry = rootNode->GetAsBSGeometry();
if (geometry) {
    execMorphUpdate(geometry);
} else {
    BSFixedString nodeName = bodyMorph.first.c_str();
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

### 3.2 语义

- 传入的是 geometry 本身 → 只处理它。
- 传入的是容器节点 → 遍历子树，处理匹配 `BODYTRI` 的 geometry。
- **不遍历 actor 的其他 biped 节点**。

### 3.3 对插件的影响

`ApplyVertexDiff(refr, object, ...)` 只更新 `object` 子树中的 mesh。要让 actor 的所有 mesh 响应新 morph 值，必须对每个 armor addon 的 mesh 分别调用。这是 `ApplyVertexDiffToWornMeshes` 遍历 `BipedAnim` 的原因。

---

## 4. 行为 3：`SHAPEDATA` 的创建不受 `isAttaching` 保护

### 4.1 源码

`BodyMorphInterface.cpp` 的 `MorphFileCache::ApplyMorph`：

```cpp
NiPointer<NiBinaryExtraData> bodyData = (NiBinaryExtraData*)NifUtils::GetExtraData(bodyGeometry, "SHAPEDATA");
bool existingMorphs = !isAttaching && bodyData;

if (bodyMorph.second.HasMorphs(refr) || existingMorphs) {
    NiSkinPartition* newSkinPartition = nullptr;
    CALL_MEMBER_FN(skinPartition, DeepCopy)((NiObject**)&newSkinPartition);

    // 重置 vertex data（仅 !isAttaching 时）
    if (bodyData && newSkinPartition) {
        if (!isAttaching) {
            for (UInt32 p = 0; p < newSkinPartition->m_uiPartitions; ++p) {
                auto& partition = newSkinPartition->m_pkPartitions[p];
                UInt32 vertexSize = newSkinPartition->GetVertexSize(partition.vertexDesc);
                memcpy(partition.shapeData->m_RawVertexData, bodyData->m_data,
                       newSkinPartition->vertexCount * vertexSize);
            }
        }
    }

    if (newSkinPartition) {
        // 创建 SHAPEDATA（不检查 isAttaching！）
        if (!bodyData) {
            auto& partition = newSkinPartition->m_pkPartitions[0];
            UInt32 vertexSize = newSkinPartition->GetVertexSize(partition.vertexDesc);
            bodyData = NiBinaryExtraData::Create("SHAPEDATA",
                reinterpret_cast<char*>(partition.shapeData->m_RawVertexData),
                newSkinPartition->vertexCount * vertexSize);
            bodyGeometry->AddExtraData(bodyData);
        }
        // ... 应用 morph ...
    }
}
```

### 4.2 两种调用模式的差异

| `isAttaching` | `bodyData` 存在 | 行为                                               |
| :-----------: | :-------------: | -------------------------------------------------- |
|    `true`     |       否        | 用**当前顶点**创建 `SHAPEDATA`，应用 morph         |
|    `false`    |       是        | 用 `SHAPEDATA` 重置到 base，应用 morph             |
|    `false`    |       否        | 用**当前顶点**创建 `SHAPEDATA`，应用 morph（危险） |

第三行是 v3.0.2 的 bug 路径：`UpdateModelWeight` 在 mesh 未就绪时调 `ApplyVertexDiff(false)`，`SHAPEDATA` 被错误的 base 固化。

### 4.3 日志证据

v3.0.2 的日志中，首次同帧穿脚趾甲 + 裤袜时大量出现：

```
[ApplyOther] mesh=' (FE05CBBF)[1]/ (FE05CBC9) [ 0%]' has no SHAPEDATA, fallback to isAttaching=true
```

这些 mesh 在 `bipedData` 中已存在但 `SHAPEDATA` 为空，说明它们尚未被 SKEE 的 `OnAttach` 处理。对它们调 `ApplyVertexDiff(false)` 会走第三行，用当前顶点创建错误 `SHAPEDATA`。

v3.0.3 改为跳过这些 mesh：

```
[ApplyOther] mesh=' (FE05CBBF)[1]/ (FE05CBC9) [ 0%]' has no SHAPEDATA, skip (will be handled by its own OnAttach)
```

---

## 5. 行为 4：`ActorUpdateManager::OnAttach` 的 observer 遍历顺序

### 5.1 源码

`skee64/ActorUpdateManager.cpp`：

```cpp
void ActorUpdateManager::AddInterface(IAddonAttachmentInterface* observer) {
    std::lock_guard<std::recursive_mutex> scs(m_lock);
    auto it = std::find_if(m_observers.begin(), m_observers.end(), [&](const IAddonAttachmentInterface* ob) {
        return ob == observer;
    });
    if (it == m_observers.end()) {
        m_observers.push_back(observer);   // 追加到末尾
    }
}

void ActorUpdateManager::OnAttach(TESObjectREFR* refr, TESObjectARMO* armor, TESObjectARMA* addon,
                                  NiAVObject* object, bool isFirstPerson, NiNode* skeleton, NiNode* root) {
    std::lock_guard<std::recursive_mutex> scs(m_lock);
    for (auto observer : m_observers) {
        observer->OnAttach(refr, armor, addon, object, isFirstPerson, skeleton, root);
    }
}
```

### 5.2 SKEE 自身的注册

`skee64/main.cpp` 的 `SKSEPlugin_Load`：

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

SKEE 在 `Load` 阶段注册，我们在 `kPostPostLoad` 注册，必然在末尾。`OnAttach` 按顺序遍历，我们的回调执行时 `BodyMorphInterface::OnAttach` 已完成。

### 5.3 日志证据

v3.0.3 的 `[Attach]` 日志中，当前 mesh 的 `hasShapeData` 有时为 `true`：

```
[Attach] refr=00000014 attachingArmor=00000D64 attachingAddon=00000D67 isFP=true object='3BA' hasShapeData=true
```

`3BA` 这个 mesh 的 `SHAPEDATA` 已由 SKEE 的 `BodyMorphInterface::OnAttach` 创建。我们的回调执行时 SKEE 已完成。这直接证明了 observer 顺序假设成立。

---

## 6. 行为 5：`UpdateModelWeight` 遍历 actor 所有 biped 节点

### 6.1 源码

`BodyMorphInterface.cpp`：

```cpp
void MorphCache::UpdateMorphs(TESObjectREFR* refr, bool deferUpdate) {
    // ...
    VisitBipedNodes(refr, [&](bool isFirstPerson, UInt32 bipedIndex, NiNode* rootNode, TESForm* armo, TESForm* arma, NiAVObject* object) {
        ApplyMorphs(refr, object, false, deferUpdate);
    });
    // ...
}
```

`VisitBipedNodes` 遍历 `bipedData->unk10` / `unk13C0` 中所有已 attach 的 mesh（CommonLibSSE-NG 中为 `BipedAnim::objects` / `bufferedObjects`）。**不区分 mesh 是否已就绪**。

### 6.2 与 `ApplyVertexDiff` 的差异

|                     | `ApplyVertexDiff(object)` | `UpdateModelWeight(actor)`      |
| ------------------- | ------------------------- | ------------------------------- |
| 遍历范围            | 传入的 `object` 子树      | actor 所有 biped 节点           |
| 触发条件            | 调用方决定                | 调用方决定                      |
| 是否处理未就绪 mesh | 取决于传入的 `object`     | **是**（只要在 `bipedData` 中） |

`OnEquipEvent` 中调用 `UpdateModelWeight` 时，`bipedData` 可能已记录尚未 attach 的 mesh（`TESEquipEvent` 与 mesh attach 的时序窗口），导致为未就绪 mesh 创建错误 `SHAPEDATA`。

### 6.3 日志证据

v3.0.2 下，`[Equip]` 后紧跟 `[Update] UpdateModelWeight`，但 `[Attach]` 在之后：

```
[23:08:11.564] [Equip] actor='' armor='Cosplay Shoes Pumps - Gloss' equipped=false
[23:08:11.564]   morph 'NoHeel' triggeredBy=00000080 -> slot-match
[23:08:11.564]     rule[1] value=1 equipped=false -> MATCH
[23:08:11.564]   [Update] UpdateModelWeight actor=''
[23:08:11.579] [Attach] refr=00000014 attachingArmor=00000D64 attachingAddon=00000D6E isFP=false object='Feet' hasShapeData=false
```

`UpdateModelWeight` 在 `[Attach]` 之前执行，说明它遍历的 `bipedData` 与 mesh 实际 attach 存在时序差。

---

## 7. 行为 6：`TESEquipEvent` 期间 `GetWornArmor` 返回旧值

### 7.1 引擎行为

`TESEquipEvent` 派发期间，`RE::Actor::GetWornArmor(slot)` 返回**事件前**状态。穿鞋事件里它仍返回 `nullptr`（或旧的那双鞋），脱鞋事件里它仍指向正要被卸下的鞋。

这是引擎的事件派发时序决定的，不是 SKEE 的行为，但与本插件的求值逻辑强相关，在此记录。

### 7.2 对插件的影响

`OnEquipEvent` 必须用 `(armor, equipped)` 合成“事件后该槽的真实状态”，不能直接读 `GetWornArmor`。详见 `docs/Architecture.md` §7.7。

### 7.3 日志证据

v3.0.2 的修复日志（`docs/Architecture.md` §7.7 引用）：

```
穿高跟鞋：GetWornArmor = nullptr → 求值结果 Default → NoHeel=1.0（错误，期望 HighHeel → NoHeel=0.0）
脱高跟鞋：GetWornArmor = 高跟鞋 → 求值结果 HighHeel → NoHeel=0.0（错误，期望 Default → NoHeel=1.0）
```

---

## 8. 对插件设计的影响

| SKEE 行为                                          | 插件对应设计                                                              |
| -------------------------------------------------- | ------------------------------------------------------------------------- |
| `OnAttach` 在 mesh 就绪后调用                      | `AttachListener` 用 `OnAttach` 驱动 mesh 更新                             |
| `BodyMorphInterface::OnAttach` 有 `HasMorphs` 条件 | `AttachListener` 在 `hasShapeData=false` 时兜底 `ApplyCurrent`            |
| `ApplyVertexDiff` 只处理传入子树                   | `ApplyVertexDiffToWornMeshes` 遍历 `BipedAnim` 逐个调用                   |
| `!bodyData` 分支不受 `isAttaching` 保护            | `ApplyVertexDiffToWornMeshes` 跳过 `hasShapeData=false` 的 mesh           |
| observer 顺序：SKEE 先于我们                       | 当前 mesh 交给 SKEE 处理，我们只处理其他 mesh                             |
| `UpdateModelWeight` 遍历所有 biped 节点            | `OnEquipEvent` 不调 `UpdateModelWeight`；`OnActorLoaded` 在读档后安全调用 |
| `TESEquipEvent` 期间 `GetWornArmor` 返回旧值       | `OnEquipEvent` 用 `(armor, equipped)` 合成状态                            |

---

## 9. 未验证 / 存疑

1. **`OnAttach` 是否在同一帧内为同一次装备操作的所有 armor addon 同步派发**：日志显示多个 `[Attach]` 的时间戳接近（毫秒级），但未逐帧验证。跨帧也不影响正确性——`AttachListener` 每个 `OnAttach` 独立处理。
2. **`BodyMorphInterface::OnAttach` 为何对脚趾甲不创建 `SHAPEDATA`**：日志显示 `hasShapeData=false`，可能 `HasMorphs` 为假，或脚趾甲的 TRI 不匹配。未深究，`ApplyCurrent` 兜底已覆盖。
3. **`SHAPEDATA` 的 `vertexCount` 越界风险**（多 partition 场景）：见 `docs/toenail-report-2026-10-27.md` §2.4.1。脚趾甲单 partition，未触发。

---

## 10. 相关文件

| 文件                                     | 作用                                                    |
| ---------------------------------------- | ------------------------------------------------------- |
| `docs/Architecture.md`                   | 插件自身架构（v3.0.2 快照）                             |
| `docs/v3.0.3-onattach-fix.md`            | v3.0.3 变更记录                                         |
| `docs/toenail-report-2026-10-27.md`      | 脚趾甲异常调查报告（含 SKEE 源码分析）                  |
| `vendor/SKEE/include/IPluginInterface.h` | SKEE 接口定义（插件侧可见的唯一 SKEE 头文件）           |
| `skee64/BodyMorphInterface.cpp`          | SKEE 实现，`ApplyVertexDiff` / `OnAttach` / `SHAPEDATA` |
| `skee64/ActorUpdateManager.cpp`          | observer 注册与遍历                                     |

```

```
