# SKEE BodyMorph 行为笔记

**日期**：2026-10-30（合并版）
**适用**：SKEE64（RaceMenu 的 BodyMorph 接口），v3.0.2 → v3.0.5 调查期间
**目的**：记录本插件依赖的 SKEE 行为，附源码引用与日志证据。`Architecture.md` 描述插件自身架构，本文档描述被依赖的外部行为。

**版本历史**：

- v3.0.2/v3.0.3 初版：已归档到 [`archived/skee-behavior-notes.md`](archived/skee-behavior-notes.md)
- v3.0.4 修正与补充：原 `skee-behavior-notes-2.md`，本文档已合并
- v3.0.5 OnAttach 跨线程调查：§2.3 新增
- 本文档取代以上两份，是当前唯一的行为笔记。

---

## 0. 术语说明：morph 名称是服装包的业务约定

本文档里提到的 `NoHeel`、`Heel`、`Pantyhose`、`Pressed` 等 morph 名称，都是 **cosplay 包（Caenarvon 的 Cosplay Pack / Basics）在 BodySlide 项目里定义的 slider 名称**，不是 SKEE 或引擎层面的通用约定。

SKEE 本身不关心 morph 名称的语义——它只是读写 `actorMorphs[formID][morphName][key]` 里的浮点数。morph 名称的语义（base 是高跟形状？应用 delta 变平脚？）完全由 mesh 作者在 BodySlide 里定义。

其他服装包可能：

- 用不同的 morph 名称（如 `HeelOffset`、`FootFlat`）
- 用相同的名称但语义不同（`NoHeel=0` 可能是平脚形状而不是高跟形状）
- 完全不支持这些 morph

本插件的 `MorphRules.json` 默认只适配 cosplay 包。适配其他包需要在 JSON 里定义新的 `Predicates` 和 `Morphs`，并且该包的 mesh 必须在 BodySlide 里 build 了对应的 morph target。

本文档 §5.4、§5.5 的语义描述**只对 cosplay 包成立**。SKEE 机制层面的描述（§2、§3、§4、§6、§7）是通用的。

---

## 1. 摘要

本插件依赖的 SKEE 行为，按机制分组：

1. **OnAttach 调用时机与线程**（§2）
2. **ApplyVertexDiff 语义**（§3）
3. **SHAPEDATA 机制**（§4）
4. **morph 应用机制**（§5，含 cosplay 包约定）
5. **事件与状态**（§6）
6. **技术事实澄清**（§7）

**最关键的四条**：

- `OnAttach` 可能在工作线程派发（§2.3），非主线程的 `SetAbsolute` / `ApplyVertexDiff` 必须延迟到主线程。
- `partClone->parent != nullptr` 是"mesh 就绪"的唯一正确信号（§7.3）。`SHAPEDATA` 的存在依赖 `HasMorphs`，不能用作就绪判据。
- cosplay 包的 `NoHeel=0` 是 base shape（高跟形状），不是"没有应用"（§5.4）。穿高跟时看不到变化是正常的。**其他包的语义可能不同。**
- morph 名称是服装包的业务约定（§0）。`NoHeel` 的视觉载体在本地观察是裤袜，可能还包括 toenails，取决于 TRI 是否 build（§5.5，存疑）。

---

## 2. OnAttach 调用时机与线程

### 2.1 BodyMorphInterface::OnAttach 的调用时机

**源码**（`skee64/BodyMorphInterface.cpp`）：

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

**调用链**：

```
SKEE: AttachBipedObject_Hooked
    ├── AttachBipedObject_Original(mesh 挂到场景图)
    ├── InstallArmorAddonHook(reference, params, boneTree, resultNode)
    ▼
ActorUpdateManager::OnAttach（按 observer 注册顺序遍历）
    ├── BodyMorphInterface::OnAttach
    ├── OverlayInterface::OnAttach
    ├── OverrideInterface::OnAttach
    ├── ...
    ├── AttachListener::OnAttach（我们在末尾）
```

**时机可靠性**：`OnAttach` 在 `AttachBipedObject_Original` 返回后同步调用，此时 mesh 已挂到场景图，顶点数据可用。SKEE 自己的 `ApplyVertexDiff(..., true)` 长期稳定运行反证了时机可靠。

**`HasMorphs` 条件**：`HasMorphs(refr)` 为假时 SKEE **不调用** `ApplyVertexDiff`，不创建 `SHAPEDATA`。`HasMorphs` 的判据见 §4.4。

**日志证据**（v3.0.3）：

```
[Attach] refr=00000014 attachingArmor=FE05CBC9 attachingAddon=FE05CBBF isFP=false object='' hasShapeData=false
[W] [Attach] current mesh '' has no SHAPEDATA, initializing with isAttaching=true
```

脚趾甲（`FE05CBC9`）attach 时 `hasShapeData=false`，说明 SKEE 的 `OnAttach` 没有为它创建 `SHAPEDATA`（`HasMorphs=false`）。

### 2.2 ActorUpdateManager::OnAttach 的 observer 遍历顺序

**源码**（`skee64/ActorUpdateManager.cpp`）：

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

void ActorUpdateManager::OnAttach(...) {
    std::lock_guard<std::recursive_mutex> scs(m_lock);
    for (auto observer : m_observers) {
        observer->OnAttach(...);
    }
}
```

**SKEE 自身的注册**（`skee64/main.cpp` 的 `SKSEPlugin_Load`）：

```cpp
g_actorUpdateManager.AddInterface(&g_skeletonExtenderInterface);
g_actorUpdateManager.AddInterface(&g_bodyMorphInterface);
g_actorUpdateManager.AddInterface(&g_overlayInterface);
g_actorUpdateManager.AddInterface(&g_overrideInterface);
g_actorUpdateManager.AddInterface(&g_itemDataInterface);
```

SKEE 在 `Load` 阶段注册，我们在 `kPostPostLoad` 注册，必然在末尾。`OnAttach` 按顺序遍历，我们的回调执行时 `BodyMorphInterface::OnAttach` 已完成——**当前 mesh 已被 SKEE 用旧 morph 值处理过**。

**日志证据**（v3.0.3）：

```
[Attach] refr=00000014 attachingArmor=00000D64 attachingAddon=00000D67 isFP=true object='3BA' hasShapeData=true
```

`3BA` 的 `SHAPEDATA` 已由 SKEE 的 `OnAttach` 创建。我们的回调执行时 SKEE 已完成。

### 2.3 OnAttach 跨线程（v3.0.5 调查）

**`OnAttach` 不是主线程信号。** NPC 批量加载（cell 加载、NPC 进入视野）时，引擎在工作线程上派发 `AttachBipedObject`，`OnAttach` 随之在工作线程执行。

**日志证据**（v3.0.4 Debug，2026-10-30）：

```
[12:59:39.958] [3296 ] [I] SKSEPlugin_Load: loaded                    ← 主线程 3296
[12:59:45.489] [34160] [D] OnMessage: DataLoaded                     ← 工作线程 34160（启动期）
[13:00:02.347] [23620] [D] [Attach] refr=0009B0DB armor=000C02FB ...  ← 工作线程 23620（NPC 批量加载）
[13:00:02.347] [23620] [D]     [SKEE] SetAbsolute actor=0009B0DB morph='NoHeel' value=1
[13:00:02.347] [23620] [D] [ApplyOne] refr=0009B0DB mesh='' isAttaching=false
[13:00:02.521] [3296 ] [D] [Attach] refr=00000014 armor=FE004B06 ...  ← 主线程 3296（玩家 mesh）
[13:00:07.778] [3296 ] [D] [Equip] actor=''(00000014) armor=...        ← 主线程 3296（玩家操作）
```

- **玩家操作**（`TESEquipEvent`、玩家 mesh 的 `OnAttach`）在 3296 主线程，与 v3.0.2 的 `TESEquipEvent` 栈行为一致。
- **NPC 批量加载的 `OnAttach`** 在 23620 工作线程，当前实现在该线程执行了 `SetAbsolute` 和 `ApplyVertexDiff`。

**三个操作在工作线程的风险**：

| 操作                                         | 风险                                                                                                    | 严重性 |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------- | ------ |
| `EvaluateRules` → `GetWornArmor`             | 读 actor 装备状态，可能与主线程的装备修改竞争                                                           | 中     |
| `SetAbsolute` → `ClearMorph` / `SetMorph`    | 修改 SKEE `actorMorphs`（`unordered_map`），主线程的 `UpdateModelWeight` / `ApplyBodyMorphs` 同时遍历它 | **高** |
| `ApplyVertexDiff` → `DeepCopy` / `SHAPEDATA` | 修改 `NiSkinPartition` 和 `NiBinaryExtraData`，非线程安全                                               | **高** |

**SKEE 自己的保护范围**：`MorphFileCache::ApplyMorphs` 里 `defer` 参数被覆盖为 `(*g_main)->threadId != GetCurrentThreadId()`，**只保护 `NIOVTaskUpdateSkinPartition::Run`（D3D 上传）**，不保护 `DeepCopy` 和 `SHAPEDATA` 创建。SKEE 长期稳定说明跨线程实际罕见，或竞争窗口极窄。

**修复方向**：非主线程延迟到 SKSE Task，执行时重新求值。详见 [`onattach-thread-affinity-report-2026-10-30.md`](onattach-thread-affinity-report-2026-10-30.md) §5.1。

**为什么 6000 用户没崩**：NPC 批量加载串行在同一工作线程（23620），不是多线程并发；多数 NPC mesh 的 TRI 没有目标 morph，`ApplyVertexDiff` 内部 `HasMorphs=false` 时提前返回；`SetAbsolute` 对 NPC 写的值大多是 0，`SetMorph` 被跳过。**这是运气，不是设计对。**

**顺带发现**：非人形 actor（鸡、螃蟹）也在被处理。`chicken`、`Crab2`、`Farm02FHat` 的 NPC 在 `OnAttach` 里被 `EvaluateRules` + `ApplyVertexDiff`。性能浪费，不是正确性问题（`HasMorphs=false` 提前返回）。

---

## 3. ApplyVertexDiff 语义

### 3.1 只处理传入的 object 子树

**接口**（`IPluginInterface.h`）：

```cpp
virtual void ApplyVertexDiff(TESObjectREFR* refr, NiAVObject* rootNode, bool erase = false) = 0;
```

**实现**（`BodyMorphInterface.cpp`）：

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

**语义**：

- 传入的是 geometry 本身 → 只处理它。
- 传入的是容器节点 → 遍历子树，处理匹配 `BODYTRI` 的 geometry。
- **不遍历 actor 的其他 biped 节点**。

**对插件的影响**：要让 actor 的所有 mesh 响应新 morph 值，必须对每个 armor addon 的 mesh 分别调用。这是 `ApplyVertexDiffToWornMeshes` 遍历 `bipedData` 的原因。

### 3.2 isAttaching 参数

接口里叫 `erase`，实现里叫 `attach`，最终传给 `MorphCache::ApplyMorphs` 的形参是 `isAttaching`。**以 `isAttaching` 为准**，`erase` 这个名字容易误导。

`isAttaching` 控制两条分支：

- **`true`**（attach 中）：跳过 `SHAPEDATA` 重置；`!SHAPEDATA` 时用**当前顶点**创建基准；应用 morph。
- **`false`**（已存在 mesh 重新应用）：用已有 `SHAPEDATA` 重置到 base；应用 morph。

`defer` 参数在 `MorphFileCache::ApplyMorphs` 里被覆盖为 `(*g_main)->threadId != GetCurrentThreadId()`，主线程下 `NIOVTaskUpdateSkinPartition::Run()` 同步执行。

### 3.3 `!bodyData` 分支的安全性

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
}
```

**`!bodyData` 创建 `SHAPEDATA` 的分支不受 `isAttaching` 保护。** v3.0.2/v3.0.3 的初版文档把这一行标为"危险"，但没区分危险的条件。

**`!bodyData` 分支的安全性取决于 `parent != nullptr`，不是 `isAttaching`**：

| `isAttaching` | `bodyData` | `parent != nullptr` | 行为                                   | 安全性                                        |
| ------------- | ---------- | ------------------- | -------------------------------------- | --------------------------------------------- |
| `true`        | 否         | 非 null             | 用当前顶点创建 `SHAPEDATA`             | ✅ 正确（SKEE `OnAttach` 路径）               |
| `false`       | 是         | 非 null             | 用 `SHAPEDATA` 重置到 base，应用 morph | ✅ 正确（重应用路径）                         |
| `false`       | 否         | **null**            | 用当前顶点创建 `SHAPEDATA`             | ❌ **危险**（v3.0.2 bug 路径）                |
| `false`       | 否         | 非 null             | 用当前顶点创建 `SHAPEDATA`             | ✅ **安全**（`HasMorphs=false` 时的正确路径） |

**判据**：`!bodyData` 分支的安全性取决于 `parent != nullptr`。旧文档说"不受 `isAttaching` 保护"是对的，但"危险"的判据是 `parent == nullptr`，不是"`isAttaching=false` 且 `!bodyData`"。

**日志证据**（v3.0.2 的 bug 路径）：

```
[ApplyOther] mesh=' (FE05CBBF)[1]/ (FE05CBC9) [ 0%]' has no SHAPEDATA, fallback to isAttaching=true
```

这些 mesh 在 `bipedData` 中已存在但 `SHAPEDATA` 为空，说明它们尚未被 SKEE 的 `OnAttach` 处理。对它们调 `ApplyVertexDiff(false)` 会走第三行，用当前顶点创建错误 `SHAPEDATA`，永久固化。

---

## 4. SHAPEDATA 机制

### 4.1 SHAPEDATA 的创建不受 isAttaching 保护

见 §3.3。`if (!bodyData)` 分支用当前 `m_RawVertexData` 创建 `SHAPEDATA`，无论 `isAttaching` 是 true 还是 false。

`SHAPEDATA` 是"未经任何 morph 修改的原始顶点"的快照。每次 `ApplyMorph` 都先 `DeepCopy` partition，用 `SHAPEDATA` 重置 vertex data，再应用 morph delta。

### 4.2 SHAPEDATA 不存在有两个原因

**v3.0.4 期间确认**：`SHAPEDATA` 不存在有**两个**原因：

| 原因                                         | 判据                           | `ApplyVertexDiff(false)` 是否安全                          |
| -------------------------------------------- | ------------------------------ | ---------------------------------------------------------- |
| **mesh 未就绪**（v3.0.2 的 bug 路径）        | `partClone->parent == nullptr` | ❌ 危险：用错误顶点创建 base，永久固化                     |
| **`HasMorphs=false`**（所有 morph 值都是 0） | `partClone->parent != nullptr` | ✅ 安全：用当前 base 顶点创建 `SHAPEDATA`，应用 0 个 delta |

初版文档把两个原因混为一谈，导致 v3.0.4 WIP 期间把 `HasShapeData` 作为"mesh 就绪"门禁，`NoHeel=0` 时裤袜被错误跳过（见 §4.3）。

**判据**：`partClone->parent != nullptr` 区分两个场景。

**日志证据**（v3.0.4 本地测试）：

```
[12:30:52.844] [Attach] armor=FE00BB67 object='' (裤袜)
[12:30:52.844]   morph 'Pantyhose' -> slot-match
[12:30:52.844]     SetAbsolute(Pantyhose=0)
[12:30:52.844] [ApplyOne] refr=00000014 mesh='' isAttaching=false
```

裤袜的 `NoHeel` 是 0，`Pantyhose` 和 `Pressed` 也是 0，`HasMorphs=false`，SKEE 的 `OnAttach` 不创建 `SHAPEDATA`。我们的 `ApplyVertexDiff(false)` 走 `!bodyData` 分支，用当前 base 顶点创建 `SHAPEDATA`，应用 0 个 delta——正确。

### 4.3 HasShapeData 检查根节点永远 false

**v3.0.4 期间确认**：`SHAPEDATA` 是挂在**子 geometry（`BSGeometry`）**上的 `NiBinaryExtraData`，不是 armor addon 的根节点上。`NiAVObject::GetExtraData("SHAPEDATA")` 对根节点永远返回 `nullptr`。

**源码**（`MorphFileCache::ApplyMorph`）：

```cpp
// rootNode 是 armor addon 的根节点，可能是 NiNode 容器
BSGeometry* geometry = rootNode->GetAsBSGeometry();
if (geometry) {
    execMorphUpdate(geometry);   // 根节点是 geometry 本身
} else {
    VisitObjects(rootNode, [&](NiAVObject* object) {
        if (BSGeometry* bodyGeometry = object->GetAsBSGeometry()) {
            if (bodyGeometry->m_name == nodeName.data) {
                execMorphUpdate(bodyGeometry);   // bodyGeometry 是子 BSGeometry
            }
        }
        return false;
    });
}
```

`execMorphUpdate` 里 `NifUtils::GetExtraData(bodyGeometry, "SHAPEDATA")` 的 `bodyGeometry` 是 `BSGeometry`，不是根节点。

**对插件的影响**：

- 任何 `HasShapeData(partClone)` 检查必须**递归**遍历子树，找到 `BSGeometry` 再检查 `GetExtraData("SHAPEDATA")`。
- v3.0.3 的 Bug 1（toenails 浮动）就是检查根节点，永远 false，toenails 被 skip。
- v3.0.4 WIP 期间 `HasShapeDataRecursive` 正确递归，但依赖 `HasMorphs`，引入了新问题（见 §4.4）。

**日志证据**（v3.0.4 WIP 早期）：

```
[12:18:36.823] [Attach] armor=FE00BB67 object=''
[12:18:36.823] [Attach] current mesh '' no SHAPEDATA, skip re-apply
```

根节点 `object=''` 是空的 `NiNode` 名字，`GetExtraData` 在它上面找不到 `SHAPEDATA`。子 geometry 里才有。

### 4.4 HasMorphs 的语义：任一 morph 值非 0

**v3.0.4 期间确认**：`HasMorphs(refr)` 的判据是 **"该 actor 在该 mesh 的 TRI 里，任意一个 morph 的 `GetBodyMorphs` 聚合值非 0"**。

**源码**（`skee64/BodyMorphInterface.cpp` 的 `BodyMorphMap::HasMorphs`）：

```cpp
bool BodyMorphMap::HasMorphs(TESObjectREFR* refr) const {
    for (auto& morph : *this) {
        float morphFactor = g_bodyMorphInterface.GetBodyMorphs(refr, morph.first.c_str());
        if (morphFactor != 0.0f)
            return true;
    }
    return false;
}
```

`GetBodyMorphs` 是所有 key（本插件的 `AdeptivePantyhoseMorphKey`、SKEE 自己的 `RSMBodyGen`、其他插件的 key）下该 morph 值的聚合（`g_bodyMorphMode` 控制求和/平均/最大）。

**对插件的影响**：

- 当所有相关 morph 值都是 0（例如 `NoHeel=0` 且裤袜只有 `NoHeel` 一个 morph target）时，`HasMorphs(refr)` 返回 false，SKEE 的 `OnAttach` **不创建 `SHAPEDATA`**。
- 这不等于 mesh 未就绪——mesh 可能已挂到场景图，顶点就绪，只是没有 morph 需要应用。
- `SHAPEDATA` 不存在的两个原因（见 §4.2）必须区分。

**关键区分**：

| 场景                                         | `HasMorphs` | `SHAPEDATA`          | mesh 就绪 |
| -------------------------------------------- | ----------- | -------------------- | --------- |
| 穿高跟，`NoHeel=0`，裤袜只有 `NoHeel` target | false       | 不存在               | ✅ 已就绪 |
| 脱高跟，`NoHeel=1`                           | true        | 存在（由 SKEE 创建） | ✅ 已就绪 |
| 首次同帧穿，mesh 未 attach                   | true        | 不存在               | ❌ 未就绪 |
| 同帧 swap，旧 `SHAPEDATA` 已固化             | true        | 存在（但 base 错误） | ✅ 已就绪 |

**判据**：`NoHeel=0` 时 `HasMorphs=false`，`SHAPEDATA` 不存在，但 mesh 已就绪。用 `HasShapeData` 作门禁会误判"未就绪"。

---

## 5. morph 应用机制

### 5.1 UpdateModelWeight 遍历 actor 所有 biped 节点

**源码**（`BodyMorphInterface.cpp` 的 `MorphCache::UpdateMorphs`）：

```cpp
void MorphCache::UpdateMorphs(TESObjectREFR* refr, bool deferUpdate) {
    VisitBipedNodes(refr, [&](bool isFirstPerson, UInt32 bipedIndex, NiNode* rootNode, TESForm* armo, TESForm* arma, NiAVObject* object) {
        ApplyMorphs(refr, object, false, deferUpdate);
    });
    // ...
}
```

`VisitBipedNodes` 遍历 `bipedData->unk10` / `unk13C0` 中所有已 attach 的 mesh（CommonLibSSE-NG 中为 `BipedAnim::objects` / `bufferedObjects`）。**不区分 mesh 是否已就绪**。

**与 `ApplyVertexDiff` 的差异**：

|                     | `ApplyVertexDiff(object)` | `UpdateModelWeight(actor)`                |
| ------------------- | ------------------------- | ----------------------------------------- |
| 遍历范围            | 传入的 `object` 子树      | actor 所有 biped 节点 + attachment holder |
| 触发条件            | 调用方决定                | 调用方决定                                |
| 是否处理未就绪 mesh | 取决于传入的 `object`     | **是**（只要在 `bipedData` 中）           |

`OnEquipEvent` 中调用 `UpdateModelWeight` 时，`bipedData` 可能已记录尚未 attach 的 mesh（`TESEquipEvent` 与 mesh attach 的时序窗口），导致为未就绪 mesh 创建错误 `SHAPEDATA`。

**日志证据**（v3.0.2）：

```
[23:08:11.564] [Equip] actor='' armor='Cosplay Shoes Pumps - Gloss' equipped=false
[23:08:11.564]   [Update] UpdateModelWeight actor=''
[23:08:11.579] [Attach] refr=00000014 attachingArmor=00000D64 attachingAddon=00000D6E isFP=false object='Feet' hasShapeData=false
```

`UpdateModelWeight` 在 `[Attach]` 之前执行，说明它遍历的 `bipedData` 与 mesh 实际 attach 存在时序差。

### 5.2 UpdateModelWeight 额外处理 attachment holder

**v3.0.4 期间确认**：`MorphCache::UpdateMorphs` 除了 `VisitBipedNodes`，还遍历 `VisitSkeletalRoots` 找 `NiAttachments [NiOA]` 节点：

```cpp
void MorphCache::UpdateMorphs(TESObjectREFR* refr, bool deferUpdate) {
    VisitBipedNodes(refr, λ(...) {
        ApplyMorphs(refr, object, false, deferUpdate);
    });
    BSFixedString attachmentName(AttachmentInterface::ATTACHMENT_HOLDER);  // "NiAttachments [NiOA]"
    VisitSkeletalRoots(refr, λ(NiNode* rootNode, bool) {
        NiAVObject* attachmentNode = rootNode->GetObjectByName(&attachmentName.data);
        if (attachmentNode) {
            ApplyMorphs(refr, attachmentNode, false, deferUpdate);
        }
    });
}
```

`NiAttachments [NiOA]` 是 SKEE 的 `AttachmentInterface::AttachMesh` 挂动态 mesh 的父节点。动态 mesh **不在 `bipedData` 里**，`ApplyVertexDiffToWornMeshes` 遍历 `bipedData` 找不到它们。

**对插件的影响**：

- `UpdateModelWeight` 会处理 attachment holder，`ApplyVertexDiffToWornMeshes` 不会。
- v3.0.4 保留 `UpdateModelWeight` 在 `OnActorLoaded`（读档）里的唯一理由：**读档时需要覆盖 attachment holder**。
- 如果未来要把 `OnActorLoaded` 也改成 `ApplyVertexDiffToWornMeshes`，需要额外写 `NiAttachments [NiOA]` 遍历逻辑。

### 5.3 BODYTRI 字符串匹配机制

**v3.0.4 期间确认**：`ApplyVertexDiff` 的完整路径：

```
ApplyVertexDiff(refr, rootNode, isAttaching)
  → morphCache.ApplyMorphs(refr, rootNode, isAttaching, deferUpdate)
    → VisitObjects(rootNode, λ):
        找 "BODYTRI" NiStringExtraData
        有 → CreateTRIPath(str) → CacheFile(path)
        CacheFile 内部:
          读 TRI 文件 → 解析 shape 名 + morph 名 + delta
          fileCache 为 null（文件不存在或解析失败）→ 跳过
    → fileCache->ApplyMorphs(refr, rootNode, isAttaching, deferUpdate)
      → 对 fileCache 里每个 shape:
          ApplyMorph(refr, rootNode, isAttaching, (shapeName, morphMap))
            在 rootNode 子树里找 shape 名匹配的 BSGeometry
            匹配失败 → 静默跳过
            匹配成功 → SHAPEDATA 重置/创建 → 应用 morph delta
```

**关键失败点**：

| 失败点                                | 表现                                                              | 排查                                                                            |
| ------------------------------------- | ----------------------------------------------------------------- | ------------------------------------------------------------------------------- |
| `rootNode` 子树里没有 `BODYTRI`       | `fileCache` 为 null，静默跳过                                     | 检查 mesh 的 NIF 是否挂了 `BODYTRI` extra data                                  |
| `BODYTRI` 字符串指向的 TRI 文件不存在 | `CacheFile` 返回 false，`fileCache` 为 null                       | 检查 `skee.log` 里 `MorphCache::CacheFile - Failed to load` 或没有 `Parsing` 行 |
| TRI 文件解析失败                      | `CacheFile` 返回 false                                            | 检查 `skee.log` 里的 `Too many vertices` / `Read empty name` 等错误             |
| shape 名不匹配                        | `ApplyMorph` 里 `bodyGeometry->m_name != nodeName.data`，静默跳过 | 对比 NIF 里的 shape 名和 TRI 里的 shape 名                                      |

**对插件的影响**：

- `[ApplyOther]` 对某 mesh 打印了，但视觉无变化，可能是 shape 名不匹配或 TRI 文件缺失。
- `skee.log` 里 `MorphFileCache::ApplyMorphs` 是否对目标 mesh 执行（`on node ...` 行）是判断是否找到 `BODYTRI` 的信号。
- `MorphCache::CacheFile - Parsing: ...` 行是判断 TRI 文件是否找到的信号。

### 5.4 cosplay 包的 NoHeel morph target 语义

**注意**：`NoHeel` 是 cosplay 包在 BodySlide 项目里定义的 morph 名称，不是 SKEE 的通用约定。其他服装包可能用不同的名称或语义（见 §0）。本节描述只对 cosplay 包成立。

**cosplay 包的约定**（基于本地观察和 rudeboybox 的测试）：

| `NoHeel` 值 | 应用 delta | 结果               |
| ----------- | ---------- | ------------------ |
| `0`         | 不应用     | base               |
| `1`         | 应用 100%  | base + delta       |
| `0.5`       | 应用 50%   | base + 0.5 × delta |

**"base 是高跟形状"这个语义**是基于以下推断：

- 穿高跟时 `NoHeel=0`，裤袜视觉上是高跟形状。
- 脱高跟时 `NoHeel=1`，裤袜视觉上是平脚形状。
- 因此 base = 高跟形状，delta = 平脚方向。

但这只是 cosplay 包的约定。如果 mesh 作者在 BodySlide 里把 base 设成平脚形状、delta 设成高跟方向，语义就反过来了。插件本身不关心 morph 的语义方向——它只是把 `MorphRules.json` 里的 `Value` 写入 SKEE 的 `actorMorphs`。

**对插件的影响**：

- 穿高跟时 `NoHeel=0`，裤袜是 base（高跟形状），**视觉上"没有 morph"是正常的**——裤袜本来就是高跟形状，不需要变形。
- 脱高跟时 `NoHeel=1`，裤袜应用 delta 变平脚形状——**这才是"有 morph"的可见状态**。
- 之前的分析一度把"穿高跟时看不出变化"误判为 bug，浪费了一轮排查。
- 如果某个 morph 变化后视觉无变化，先确认"变化前后是否都是 base 或都是 base+delta"。`NoHeel 0→0`（都是 base）看不出变化是正常的；只有 `0→1` 或 `1→0` 才能看到。
- **换服装包时，morph 名称和语义都可能不同**，不能假设 `NoHeel=0` 一定是高跟形状。

### 5.5 NoHeel 的视觉载体（本地环境观察，存疑）

**本地环境的 `skee.log` 观察**（`MorphCache::CacheFile` 输出）：

| mesh      | TRI 文件                                              | 本地观察到的 morph                               | 含 `NoHeel` |
| --------- | ----------------------------------------------------- | ------------------------------------------------ | ----------- |
| 裤袜      | `caenarvon\csplay\cpb_s.tri`                          | `NoHeel` 3368 顶点                               | ✅          |
| 裸足 Feet | `actors\character\character assets\femalefeet.tri`    | `AnkleSize` + `FeetFeminine`                     | ❌          |
| 鞋的 Feet | `caenarvon\csplay\gala_heelsa.tri` 的 `Feet` shape    | `AnkleSize` + `FeetFeminine` + `Heel`            | ❌          |
| 鞋的鞋体  | `caenarvon\csplay\gala_heelsa.tri` 的 `heelA_*` shape | `AnkleSize` + `FeetFeminine` + `buffer` + `Heel` | ❌          |
| toenails  | `caenarvon\csplay\cpb_nails.tri`                      | `NailsLong` + `NailsSharp` + `NailsBuffer`       | ❌（本地）  |

**重要存疑**：本地观察到的 toenails TRI 不含 `NoHeel`，但：

- 本地环境的 `cpb_nails.tri` 可能没有用 BodySlide build，或者 build 时没有包含 `NoHeel` slider。本地配置较简单，未完整 build cosplay 包的所有 TRI。
- rudeboybox 在 v3.0.3 报告 toenails 浮动/错位，说明在他的环境里 toenails 确实响应了 morph 变化（很可能是 `NoHeel`，也可能是其他 morph）。
- 因此，"toenails 不响应 `NoHeel`"可能是本地环境配置问题，不是 cosplay 包的固定事实。

**结论（存疑）**：

- **裤袜的 `NoHeel` 是本地确认的**。
- **toenails 是否响应 `NoHeel` 存疑**，取决于 `cpb_nails.tri` 是否在 BodySlide 里 build 了 `NoHeel` slider。需要 rudeboybox 的环境提供 `skee.log` 确认 `cpb_nails.tri` 的 morph 列表。
- **裸足、鞋的 TRI 不含 `NoHeel`（本地确认）**。脚部变形由鞋的 `Heel` morph 控制（RaceMenu High Heel 系统），不是我们的 `NoHeel`。这一条不依赖 build 完整性——鞋的 TRI 里明确有 `Heel` 而不是 `NoHeel`。

**对插件的影响**：

- `[ApplyOther]` 对所有已 attach 的 mesh 调 `ApplyVertexDiff(false)`，不管它的 TRI 是否含目标 morph。`ApplyMorph` 对不含目标 morph 的 mesh 静默跳过——**无害，但不是"没有处理"**。
- 排查"某个 mesh 不响应 morph"时，先确认该 mesh 的 TRI 是否在 BodySlide 里 build 了目标 morph，不要假设"所有 body mesh 都响应所有 morph"。
- **换服装包时，TRI 内容和 morph 名称可能完全不同**，需要重新确认。

### 5.6 Actor::AddWornItem 不派发 TESEquipEvent

**v3.0.4 期间确认**：`TESEquipEvent` 只由 `ActorEquipManager` 的装备/卸下路径派发。`Actor::AddWornItem` 是底层 API，**不派发 `TESEquipEvent`**。

| API                                          | 派发 `TESEquipEvent` | mesh attach |
| -------------------------------------------- | -------------------- | ----------- |
| `ActorEquipManager::EquipObject`             | ✅                   | ✅          |
| `Actor::EquipItem`                           | ✅                   | ✅          |
| `Actor::AddWornItem`（**Modex 用这个**）     | ❌                   | ✅          |
| Papyrus `EquipItem`                          | ✅                   | ✅          |
| `ActorEquipManager::UnequipObject`（脱装备） | ✅                   | ✅          |

**对插件的影响**：

- v3.0.2/v3.0.3 依赖 `TESEquipEvent` 求值 + 更新 mesh。Modex 的穿装备路径不触发事件，求值不执行，morph 值不更新——**视觉上"看不到变化"**。
- v3.0.4 把 `OnAttach` 作为 SSOT 入口，在 `OnAttach` 里主动求值，覆盖 Modex 路径。
- 脱装备路径（`UnequipObject`）仍触发事件，所以 v3.0.4 的 `OnEquipEvent` 在脱装备时仍执行。

**日志证据**（v3.0.4 本地测试，Modex 三件同穿）：

```
（AdeptivePantyhose.log 零条 [Equip]）
[12:30:52.815] [Attach] refr=00000014 armor=FE00BB66 object='' (toenails)
[12:30:52.815]   EvaluateRules → SetAbsolute(NoHeel=1)
[12:30:52.815] [ApplyOne] refr=00000014 mesh='' isAttaching=false
[12:30:52.834] [Attach] refr=00000014 armor=FE00CB69 object='' (gala)
[12:30:52.834]   EvaluateRules → SetAbsolute(NoHeel=0)
[12:30:52.834] [ApplyOne] refr=00000014 mesh='' isAttaching=false
[12:30:52.844] [Attach] refr=00000014 armor=FE00BB67 object='' (裤袜)
[12:30:52.844]   EvaluateRules → SetAbsolute(Pantyhose=0, Pressed=0)
[12:30:52.844] [ApplyOne] refr=00000014 mesh='' isAttaching=false
```

零条 `[Equip]`，但三条 `[Attach]` 各自求值 + 应用。

---

## 6. 事件与状态

### 6.1 TESEquipEvent 期间 GetWornArmor 返回旧值

**引擎行为**：`TESEquipEvent` 派发期间，`RE::Actor::GetWornArmor(slot)` 返回**事件前**状态。穿鞋事件里它仍返回 `nullptr`（或旧的那双鞋），脱鞋事件里它仍指向正要被卸下的鞋。

这是引擎的事件派发时序决定的，不是 SKEE 的行为，但与本插件的求值逻辑强相关。

**对插件的影响**：`OnEquipEvent` 必须用 `(armor, equipped)` 合成"事件后该槽的真实状态"，不能直接读 `GetWornArmor`。详见 `Architecture.md` §5.2。

**日志证据**（v3.0.2 的修复日志）：

```
穿高跟鞋：GetWornArmor = nullptr → 求值结果 Default → NoHeel=1.0（错误，期望 HighHeel → NoHeel=0.0）
脱高跟鞋：GetWornArmor = 高跟鞋 → 求值结果 HighHeel → NoHeel=0.0（错误，期望 Default → NoHeel=1.0）
```

**注意**：`GetWornArmor` 返回旧值 ≠ `bipedData->partClone` 也是旧值/就绪状态。这是两个独立数据源。v3.0.4 WIP 中间失败尝试（用 `GetWornArmor` 旧值集合过滤 `ApplyVertexDiffToWornMeshes`）的根因就是混淆了这两者。

---

## 7. 技术事实澄清

### 7.1 REL::ID 不是指针

**v3.0.4 期间确认**：`RE::Xxx::Ni_RTTI` 是 `REL::ID`（地址库 ID），不是 `const NiRTTI*`。要比较必须先 `REL::Relocation` 解引用。

```cpp
// 错误：编译失败，REL::ID 不能和 NiRTTI* 比较
if (a_object->GetRTTI() == RE::NiNode::Ni_RTTI) { ... }

// 正确：REL::Relocation 解引用
[[nodiscard]] auto NiNodeRTTI() -> const RE::NiRTTI* {
    static const REL::Relocation<const RE::NiRTTI*> s_rtti{RE::NiNode::Ni_RTTI};
    return s_rtti.get();
}
if (a_object->GetRTTI() == NiNodeRTTI()) { ... }
```

### 7.2 skyrim_cast 走引擎 RTTI 继承链

**v3.0.4 期间确认**：`skyrim_cast<T*>(obj)` 走引擎 RTTI 的继承链，能识别 `BSFadeNode`、`BSFaceGenNiNode` 等 `NiNode` 子类。**不要用 `GetRTTI() == Xxx::Ni_RTTI` 精确比较**，会漏掉子类。

```cpp
// 错误：只匹配精确的 NiNode，漏掉 BSFadeNode 等子类
if (a_object->GetRTTI() == NiNodeRTTI()) { ... }

// 正确：skyrim_cast 走继承链
auto* node = skyrim_cast<RE::NiNode*>(a_object);
if (node != nullptr) { ... }
```

### 7.3 partClone->parent 是"mesh 就绪"的唯一信号

**v3.0.4 期间确认**：判断 mesh 是否已挂到场景图、顶点是否就绪，唯一可靠的信号是 `partClone->parent != nullptr`。

| 门禁                           | 优点                                            | 缺点                                              |
| ------------------------------ | ----------------------------------------------- | ------------------------------------------------- |
| `HasShapeData`                 | 直接反映 SKEE 是否处理过                        | 依赖 `HasMorphs`，`NoHeel=0` 时误判（§4.2、§4.4） |
| RTTI 精确匹配                  | 简单                                            | 漏子类（§7.2）                                    |
| `partClone->parent != nullptr` | mesh 已挂到场景图 = 顶点就绪；正交于 morph 状态 | 需要 `NiAVObject::parent` 字段（CommonLibSSE 有） |
| `lastUpdatedFrameCounter`      | 可判断"本帧是否更新过"                          | 需要额外逻辑                                      |

**判据**：`parent != nullptr` 是唯一正确的"mesh 就绪"信号。不要用 `SHAPEDATA`（依赖 `HasMorphs`），不要用 RTTI 精确匹配（漏子类）。

---

## 8. 对插件设计的影响

按机制分组，当前（v3.0.5 设计期）的设计决策：

| SKEE 行为                                                                             | 插件对应设计                                                                                                                                      |
| ------------------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------------------------------- |
| `OnAttach` 在 mesh 就绪后调用，**可能在工作线程**（§2.1、§2.3）                       | `AttachListener` 在工作线程延迟到 SKSE Task；主线程走原路径                                                                                       |
| `BodyMorphInterface::OnAttach` 有 `HasMorphs` 条件（§2.1、§4.4）                      | `AttachListener` 不依赖 `HasShapeData` 判据，直接对当前 mesh 调 `ApplyVertexDiff(false)`                                                          |
| `ApplyVertexDiff` 只处理传入子树（§3.1）                                              | `ApplyVertexDiffToWornMeshes` 遍历 `BipedAnim` 逐个调用                                                                                           |
| `!bodyData` 分支安全性取决于 `parent`（§3.3）                                         | 门禁用 `partClone->parent != nullptr`，不用 `HasShapeData`                                                                                        |
| observer 顺序：SKEE 先于我们（§2.2）                                                  | 当前 mesh 交给 SKEE 处理，我们只补新 morph 值                                                                                                     |
| `UpdateModelWeight` 遍历所有 biped 节点 + attachment holder（§5.1、§5.2）             | `OnEquipEvent` 用 `ApplyVertexDiffToWornMeshes`（不含 attachment holder）；`OnActorLoaded` 保留 `UpdateModelWeight`（读档覆盖 attachment holder） |
| `TESEquipEvent` 期间 `GetWornArmor` 返回旧值（§6.1）                                  | `OnEquipEvent` 用 `(armor, equipped)` 合成状态                                                                                                    |
| `HasMorphs` 是"任一 morph 值非 0"（§4.4）                                             | `SHAPEDATA` 不存在可能是 `HasMorphs=false`，不是 mesh 未就绪                                                                                      |
| `ApplyVertexDiff(false)` 的 `!bodyData` 分支在 mesh 就绪时安全（§3.3）                | 只在 `parent == nullptr` 时危险                                                                                                                   |
| `Actor::AddWornItem` 不派发 `TESEquipEvent`（§5.6）                                   | `OnAttach` 是 SSOT，必须主动求值                                                                                                                  |
| `NoHeel` 的视觉载体包括裤袜，可能还包括 toenails（§5.5，存疑），取决于 TRI 是否 build | 不要假设"所有 body mesh 都响应所有 morph"；换包时重新确认 TRI 内容                                                                                |
| cosplay 包的 `NoHeel=0` 是 base shape（§5.4），其他包的语义可能不同                   | 穿高跟时看不到变化是正常的；换包时不能假设 `NoHeel=0` 一定是高跟形状                                                                              |

---

## 9. 未验证 / 存疑

1. **Modex 三件同穿的 attach 顺序依赖**：`OnAttach` 只处理当前 mesh。如果裤袜先 attach、高跟后 attach，裤袜可能不响应高跟的 `NoHeel` 变化。本地测试恰好是安全顺序（toenails → 高跟 → 裤袜），反序未测。需要 rudeboybox 复测。详见 `Architecture.md` §11.1。

2. **`BodyMorphInterface::OnAttach` 对脚趾甲不创建 `SHAPEDATA` 的确切原因**：`HasMorphs` 为假（脚趾甲只有 `NailsLong`、`NailsSharp`、`NailsBuffer`，全是 0）还是 TRI 不匹配？`skee.log` 里脚趾甲 attach 时没有 `MorphCache::CacheFile - Parsing: cpb_nails.tri` 行——说明 `HasMorphs=false` 时 SKEE 完全不处理，连 TRI 文件都不加载。与 §5.3 的 `BODYTRI` 查找路径一致。

3. **`SHAPEDATA` 的 `vertexCount` 越界风险**（多 partition 场景）：见 `archived/toenail-report-2026-10-27.md` §2.4.1。脚趾甲单 partition，未触发。

4. **`g_actorUpdateManager.isReverting()` 期间 `OnAttach` 的行为**：SKEE 的 `BodyMorphInterface::OnAttach` 在 `isReverting()` 为 true 时走 `AddBodyUpdate(refr->formID)` 而不是 `ApplyVertexDiff`。我们的 `AttachListener::OnAttach` 不检查 `isReverting`，会在 `isReverting` 期间主动求值 + 应用。是否有问题未验证。

5. **`MorphCache::CacheFile` 的并行加载与主线程同步**：`g_parallelMorphing` 为 true 时 `MorphFileCache::ApplyMorphs` 用 `concurrency::create_task` 并行处理 `vertexMap`，但 `NIOVTaskUpdateSkinPartition::Run()` 仍是主线程同步。是否有竞态未验证。

6. **OnAttach 跨线程的机制层面确认**（v3.0.5）：日志实锤 NPC 批量加载时 `OnAttach` 在工作线程（23620）。但哪些场景会触发工作线程派发（cell 加载？预加载？异步 NPC 生成？）未完全枚举。IED 冲突的 hook 位置确认（HookMap）也未做。

7. **`NoHeel` 的视觉载体是否包括 toenails**（§5.5）：本地环境未完整 build cosplay 包的 TRI，`cpb_nails.tri` 的 morph 列表可能不完整。rudeboybox 观察到 toenails 浮动，说明他的环境里 toenails 响应了 morph 变化。需要 rudeboybox 提供 `skee.log` 里的 `MorphCache::CacheFile - Parsing: cpb_nails.tri` 行，确认该 TRI 的 morph 列表是否含 `NoHeel`。

8. **其他服装包的 morph 名称约定**（§0）：本文档 §5.4、§5.5 的语义描述只对 cosplay 包成立。其他包可能用不同的 morph 名称（`HeelOffset`、`FootFlat`）、不同的 delta 方向、不同的 base 形状。插件的 `MorphRules.json` 默认只适配 cosplay 包，适配其他包需要在 JSON 里定义新的 `Predicates` 和 `Morphs`，并且该包的 mesh 必须在 BodySlide 里 build 了对应的 morph target。这一点在 README 的"兼容性"部分也应该说明（如果还没说明的话）。

---

## 10. 相关文件

| 文件                                                                                               | 作用                                                               |
| -------------------------------------------------------------------------------------------------- | ------------------------------------------------------------------ |
| [`Architecture.md`](Architecture.md)                                                               | 插件自身架构（v3.0.4 当前）                                        |
| [`onattach-d3d-regression-report-2026-10-29.md`](onattach-d3d-regression-report-2026-10-29.md)     | v3.0.3 回归调查                                                    |
| [`onattach-thread-affinity-report-2026-10-30.md`](onattach-thread-affinity-report-2026-10-30.md)   | v3.0.5 OnAttach 跨线程调查                                         |
| [`v3.0.4-ssot-refactor.md`](v3.0.4-ssot-refactor.md)                                               | v3.0.4 演进细节                                                    |
| [`v3.0.4-pitfalls-and-regression-guards.md`](v3.0.4-pitfalls-and-regression-guards.md)             | v3.0.4 踩坑与防回归                                                |
| [`archived/skee-behavior-notes.md`](archived/skee-behavior-notes.md)                               | v3.0.2/v3.0.3 初版（已冻结）                                       |
| [`archived/toenail-report-2026-10-27.md`](archived/toenail-report-2026-10-27.md)                   | 脚趾甲异常调查报告（v3.0.3 设计期）                                |
| [`archived/toenail-onattach-design-2026-10-27.md`](archived/toenail-onattach-design-2026-10-27.md) | v3.0.3 设计草案（已被 v3.0.4 取代）                                |
| [`archived/v3.0.3-onattach-fix.md`](archived/v3.0.3-onattach-fix.md)                               | v3.0.3 实现记录（已被 v3.0.4 取代）                                |
| `vendor/SKEE/include/IPluginInterface.h`                                                           | SKEE 接口定义（插件侧可见的唯一 SKEE 头文件）                      |
| `skee64/BodyMorphInterface.cpp`                                                                    | SKEE 实现：`ApplyVertexDiff`、`OnAttach`、`SHAPEDATA`、`HasMorphs` |
| `skee64/ActorUpdateManager.cpp`                                                                    | observer 注册与遍历                                                |
| `skee64/AttachmentInterface.cpp`                                                                   | `NiAttachments [NiOA]` 挂载逻辑                                    |

---

**文档结束。**
