# SKEE BodyMorph 行为笔记（续，v3.0.4 调查期间）

**日期**：2026-10-29
**适用**：SKEE64（RaceMenu 的 BodyMorph 接口），v3.0.4 调查期间
**目的**：记录 v3.0.2/v3.0.3 调查期间写的 [`skee-behavior-notes.md`](skee-behavior-notes.md) 之后，v3.0.4 调查期间**新发现、修正、补充**的 SKEE 行为。

**与旧文档的关系**：

- `skee-behavior-notes.md` 是 v3.0.2/v3.0.3 调查期间的快照，**冻结不改**。
- 本文档是它的续篇，只记录 v3.0.4 期间的变化——修正旧假设、补充新行为、澄清模糊点。
- 阅读顺序：先读旧文档了解基础，再读本文档了解 v3.0.4 期间的变化。

**姊妹文档**：

- [`v3.0.4-ssot-refactor.md`](v3.0.4-ssot-refactor.md) — v3.0.4 主文档（架构决策、时序、验证、边界）
- [`v3.0.4-pitfalls-and-regression-guards.md`](v3.0.4-pitfalls-and-regression-guards.md) — 踩坑与防回归

---

## 1. 摘要：v3.0.4 期间新增/修正的行为

| 类别     | 行为                                                               | 旧文档           | 本文档 |
| -------- | ------------------------------------------------------------------ | ---------------- | ------ |
| **修正** | `HasMorphs(refr)` 的语义是"任一 morph 值非 0"                      | 未明确说         | §2.1   |
| **修正** | `SHAPEDATA` 不存在有两个原因，旧文档只说"未就绪"                   | 不完整           | §2.2   |
| **修正** | `HasShapeData` 检查根节点永远 false                                | 暗示但未强调     | §2.3   |
| **修正** | `ApplyVertexDiffToWornMeshes` 用 `hasShapeData=false` 作门禁是错的 | 旧文档 §8 表格   | §2.4   |
| **新增** | `NoHeel` 的 morph target 语义：base = 高跟形状                     | 未涉及           | §3.2   |
| **新增** | `ApplyVertexDiff(false)` 的 `!bodyData` 分支在 mesh 就绪时是安全的 | 只说"危险"       | §3.3   |
| **新增** | `BODYTRI` 字符串匹配机制                                           | 只说"找 BODYTRI" | §3.4   |
| **新增** | `MorphCache::UpdateMorphs` 额外处理 attachment holder              | 未涉及           | §3.5   |
| **新增** | `Actor::AddWornItem` 不派发 `TESEquipEvent`                        | 未涉及           | §3.6   |
| **新增** | `NoHeel` 的视觉载体只有裤袜                                        | 未涉及           | §3.7   |
| **澄清** | `REL::ID` 不是指针，要 `REL::Relocation` 解引用                    | 未涉及           | §4.1   |
| **澄清** | `skyrim_cast` 走引擎 RTTI 继承链                                   | 未涉及           | §4.2   |
| **澄清** | `partClone->parent` 是"mesh 就绪"的唯一信号                        | 未涉及           | §4.3   |

---

## 2. 修正的旧行为

### 2.1 `HasMorphs(refr)` 的语义：任一 morph 值非 0

**旧文档**：`skee-behavior-notes.md` §2.3 只说"`HasMorphs(refr)` 为假时 SKEE 不调用 `ApplyVertexDiff`"，但没说 `HasMorphs` 的判据是什么。

**v3.0.4 期间确认**：`HasMorphs` 的判据是 **"该 actor 在该 mesh 的 TRI 里，任意一个 morph 的 `GetBodyMorphs` 聚合值非 0"**。

`skee64/BodyMorphInterface.cpp` 的 `BodyMorphMap::HasMorphs`：

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

- 当所有相关 morph 值都是 0（例如 `NoHeel=0` 且裤袜只有 `NoHeel` 一个 morph target）时，`HasMorphs(refr)` 返回 false，SKEE 的 `BodyMorphInterface::OnAttach` **不创建 `SHAPEDATA`**。
- 这不等于 mesh 未就绪——mesh 可能已挂到场景图，顶点就绪，只是没有 morph 需要应用。
- `SHAPEDATA` 不存在的两个原因（见 §2.2）必须区分。

**关键区分**：

| 场景                                         | `HasMorphs` | `SHAPEDATA`          | mesh 就绪 |
| -------------------------------------------- | ----------- | -------------------- | --------- |
| 穿高跟，`NoHeel=0`，裤袜只有 `NoHeel` target | false       | 不存在               | ✅ 已就绪 |
| 脱高跟，`NoHeel=1`                           | true        | 存在（由 SKEE 创建） | ✅ 已就绪 |
| 首次同帧穿，mesh 未 attach                   | true        | 不存在               | ❌ 未就绪 |
| 同帧 swap，旧 `SHAPEDATA` 已固化             | true        | 存在（但 base 错误） | ✅ 已就绪 |

**日志证据**（v3.0.4 本地测试）：

```
[12:30:52.844] [Attach] armor=FE00BB67 object='' (裤袜)
[12:30:52.844]   morph 'Pantyhose' -> slot-match
[12:30:52.844]     rule[1] value=0 equipped=true -> MATCH
[12:30:52.844]     SetAbsolute(Pantyhose=0)
[12:30:52.844]   morph 'Pressed' -> slot-match
[12:30:52.844]     rule[1] value=0 equipped=true -> MATCH
[12:30:52.844]     SetAbsolute(Pressed=0)
[12:30:52.844] [ApplyOne] refr=00000014 mesh='' isAttaching=false
```

裤袜的 `NoHeel` 也是 0（gala 刚设），`Pantyhose` 和 `Pressed` 也是 0，所以 `HasMorphs=false`，SKEE 的 `OnAttach` 不创建 `SHAPEDATA`。我们的 `ApplyVertexDiff(false)` 走 `!bodyData` 分支，用当前 base 顶点创建 `SHAPEDATA`，应用 `NoHeel=0`、`Pantyhose=0`、`Pressed=0` 的 delta（全是 0）—— 结果正确。

### 2.2 `SHAPEDATA` 不存在有两个原因

**旧文档**：`skee-behavior-notes.md` §4.3 只说"`SHAPEDATA` 不存在"时 `ApplyVertexDiff(false)` 是危险路径，暗示原因是"mesh 未就绪"。

**v3.0.4 期间确认**：`SHAPEDATA` 不存在有**两个**原因：

| 原因                                         | 判据                           | `ApplyVertexDiff(false)` 是否安全                          |
| -------------------------------------------- | ------------------------------ | ---------------------------------------------------------- |
| **mesh 未就绪**（v3.0.2 的 bug 路径）        | `partClone->parent == nullptr` | ❌ 危险：用错误顶点创建 base，永久固化                     |
| **`HasMorphs=false`**（所有 morph 值都是 0） | `partClone->parent != nullptr` | ✅ 安全：用当前 base 顶点创建 `SHAPEDATA`，应用 0 个 delta |

旧文档把两个原因混为一谈，导致 v3.0.4 WIP 期间把 `HasShapeData` 作为"mesh 就绪"门禁，`NoHeel=0` 时裤袜被错误跳过（见 §2.4）。

**判据**：`partClone->parent != nullptr` 区分两个场景。这是 `NiAVObject::parent` 字段（CommonLibSSE 暴露），语义是"mesh 是否已挂到场景图"。

### 2.3 `HasShapeData` 检查根节点永远 false

**旧文档**：`skee-behavior-notes.md` §4.3 提到 v3.0.2 日志里 `has no SHAPEDATA` 后，v3.0.3 改为跳过。但没说清"检查根节点永远 false"这个事实。

**v3.0.4 期间确认**：`SHAPEDATA` 是挂在**子 geometry（`BSGeometry`）**上的 `NiBinaryExtraData`，不是 armor addon 的根节点上。`NiAVObject::GetExtraData("SHAPEDATA")` 对根节点永远返回 `nullptr`。

`skee64/BodyMorphInterface.cpp` 的 `MorphFileCache::ApplyMorph`：

```cpp
// rootNode 是 armor addon 的根节点，可能是 NiNode 容器
BSGeometry* geometry = rootNode->GetAsBSGeometry();
if (geometry) {
    execMorphUpdate(geometry);   // 根节点是 geometry 本身
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

`execMorphUpdate` 里：

```cpp
NiPointer<NiBinaryExtraData> bodyData =
    (NiBinaryExtraData*)NifUtils::GetExtraData(bodyGeometry, "SHAPEDATA");
```

`bodyGeometry` 是 `BSGeometry`，不是根节点。`SHAPEDATA` 挂在 `bodyGeometry` 上。

**对插件的影响**：

- 任何 `HasShapeData(partClone)` 检查必须**递归**遍历子树，找到 `BSGeometry` 再检查 `GetExtraData("SHAPEDATA")`。
- v3.0.3 的 Bug 1 就是检查根节点，永远 false，toenails 被 skip。
- v3.0.4 WIP 期间 `HasShapeDataRecursive` 正确递归，但依赖 `HasMorphs`，引入了新问题（见 §2.4）。

**日志证据**（v3.0.4 WIP 早期）：

```
[12:18:36.823] [Attach] armor=FE00BB67 object=''
[12:18:36.823] [Attach] current mesh '' no SHAPEDATA, skip re-apply
```

根节点 `object=''` 是空的 `NiNode` 名字，`GetExtraData` 在它上面找不到 `SHAPEDATA`。子 geometry 里才有。

### 2.4 `ApplyVertexDiffToWornMeshes` 用 `hasShapeData=false` 作门禁是错的

**旧文档**：`skee-behavior-notes.md` §8 的表格：

| SKEE 行为                               | 插件对应设计                                                    |
| --------------------------------------- | --------------------------------------------------------------- |
| `!bodyData` 分支不受 `isAttaching` 保护 | `ApplyVertexDiffToWornMeshes` 跳过 `hasShapeData=false` 的 mesh |

**v3.0.4 期间确认**：这个设计是**错误的**。跳过 `hasShapeData=false` 的 mesh 会导致 `NoHeel=0` 时裤袜被跳过（`HasMorphs=false`，`SHAPEDATA` 不存在），**脱高跟时裤袜保持 base（高跟形状），无法变成平脚形状**。

**正确的门禁**：`partClone->parent != nullptr`。

| 场景                            | `parent` | `HasShapeData` | 旧门禁                   | 新门禁  |
| ------------------------------- | -------- | -------------- | ------------------------ | ------- |
| 穿高跟裤袜 attach（`NoHeel=0`） | 非 null  | false          | skip（错）               | 处理 ✅ |
| 脱高跟裤袜重应用（`NoHeel=1`）  | 非 null  | false          | skip（**错，视觉错误**） | 处理 ✅ |
| 同帧穿新装备（未 attach）       | **null** | false          | skip（正确但原因是错的） | skip ✅ |

**日志证据**（v3.0.4 WIP 后期）：

```
[12:19:11.116] [Equip] gala equipped=false
[12:19:11.116] SetAbsolute(NoHeel=1)      ← 0→1
[12:19:11.122] [Attach] Feet no SHAPEDATA, skip re-apply
（没有 [ApplyOther] 对裤袜）
```

脱高跟时裤袜的 `SHAPEDATA` 不存在（`NoHeel=0` 时 SKEE 没创建），旧门禁 skip，裤袜保持 base —— **视觉错误**。

换成 `parent` 门禁后（v3.0.4 最终方案）：

```
[12:30:24.115] [Equip] Cosplay Shoes Ankle - Gloss equipped=false
[12:30:24.115] SetAbsolute(NoHeel=1)
[12:30:24.115] [ApplyOther] mesh=' (00000D67)[1]/ (FE004B06)' parent=0x2a737342380
[12:30:24.116] [ApplyOther] mesh=' (FE00BB4E)[1]/ (FE00BB66)' parent=0x2a737342380   ← toenails
[12:30:24.116] [ApplyOther] mesh=' (FE009DD3)[1]/ (FE009DED)' parent=0x2a737342380   ← 鞋
[12:30:24.117] [ApplyOther] mesh=' (FE00BB4F)[1]/ (FE00BB67)' parent=0x2a737342380   ← 裤袜 ⭐
```

裤袜被处理，`ApplyVertexDiff(false)` 走 `!bodyData` 分支，用当前 base 顶点创建 `SHAPEDATA`，应用 `NoHeel=1` 的 delta → 平脚形状。正确。

---

## 3. 新增行为

### 3.1 `NoHeel` 的 morph target 语义：base 是形状

**旧文档**：未涉及 `NoHeel` 的 morph target 定义。

**v3.0.4 期间确认**：`NoHeel` 的 morph target 语义是 **"base = 高跟形状，应用 delta = 平脚形状"**。

| `NoHeel` 值 | 应用 delta | 结果                           |
| ----------- | ---------- | ------------------------------ |
| `0`         | 不应用     | base（高跟形状）               |
| `1`         | 应用 100%  | base + delta（平脚形状）       |
| `0.5`       | 应用 50%   | base + 0.5 × delta（中间形状） |

**对插件的影响**：

- 穿高跟时 `NoHeel=0`，裤袜是 base（高跟形状），**视觉上"没有 morph"是正常的**——裤袜本来就是高跟形状，不需要变形。
- 脱高跟时 `NoHeel=1`，裤袜应用 delta 变平脚形状——**这才是"有 morph"的可见状态**。
- 之前的分析一度把"穿高跟时看不出变化"误判为 bug，浪费了一轮排查。

**关键判据**：如果某个 morph 变化后视觉无变化，先确认"变化前后是否都是 base 或都是 base+delta"。`NoHeel 0→0`（都是 base）看不出变化是正常的；只有 `0→1` 或 `1→0` 才能看到。

### 3.2 `ApplyVertexDiff(false)` 的 `!bodyData` 分支在 mesh 就绪时是安全的

**旧文档**：`skee-behavior-notes.md` §4.2 的表格：

| `isAttaching` | `bodyData` 存在 | 行为                                               |
| ------------- | --------------- | -------------------------------------------------- |
| `false`       | 否              | 用**当前顶点**创建 `SHAPEDATA`，应用 morph（危险） |

旧文档把这行标为"危险"，但没说清"危险的条件是什么"。

**v3.0.4 期间确认**：这行**只在 mesh 未就绪时才危险**。mesh 就绪（`parent != nullptr`）时，用当前顶点创建 `SHAPEDATA` 是**正确**的——因为当前顶点就是 base。

| `isAttaching` | `bodyData` | `parent != nullptr` | 行为                                   | 安全性                                        |
| ------------- | ---------- | ------------------- | -------------------------------------- | --------------------------------------------- |
| `true`        | 否         | 非 null             | 用当前顶点创建 `SHAPEDATA`，应用 morph | ✅ 正确（SKEE `OnAttach` 路径）               |
| `false`       | 是         | 非 null             | 用 `SHAPEDATA` 重置到 base，应用 morph | ✅ 正确（重应用路径）                         |
| `false`       | 否         | **null**            | 用当前顶点创建 `SHAPEDATA`，应用 morph | ❌ **危险**（v3.0.2 bug 路径）                |
| `false`       | 否         | 非 null             | 用当前顶点创建 `SHAPEDATA`，应用 morph | ✅ **安全**（`HasMorphs=false` 时的正确路径） |

**判据**：`!bodyData` 分支的安全性取决于 `parent != nullptr`，不是 `isAttaching`。旧文档说"不受 `isAttaching` 保护"是对的，但"危险"的判据是 `parent == nullptr`，不是"`isAttaching=false` 且 `!bodyData`"。

### 3.3 `BODYTRI` 字符串匹配机制

**旧文档**：`skee-behavior-notes.md` §3.1 引用了 `MorphFileCache::ApplyMorph` 的 `BODYTRI` 查找逻辑，但没说清"匹配失败会怎样"。

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

### 3.4 `MorphCache::UpdateMorphs` 额外处理 attachment holder

**旧文档**：`skee-behavior-notes.md` §6.1 引用了 `MorphCache::UpdateMorphs` 遍历 `VisitBipedNodes`，但没说清它**还遍历 attachment holder**。

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

### 3.5 `Actor::AddWornItem` 不派发 `TESEquipEvent`

**旧文档**：未涉及。

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

### 3.6 `NoHeel` 的视觉载体只有裤袜

**旧文档**：未涉及。

**v3.0.4 期间确认**：从 `skee.log` 的 `MorphCache::CacheFile` 输出确认各 mesh 的 TRI 内容：

| mesh      | TRI 文件                                              | 含 `NoHeel`                                              |
| --------- | ----------------------------------------------------- | -------------------------------------------------------- |
| 裤袜      | `caenarvon\csplay\cpb_s.tri`                          | ✅ `NoHeel` 3368 顶点                                    |
| 裸足 Feet | `actors\character\character assets\femalefeet.tri`    | ❌ 只有 `AnkleSize` + `FeetFeminine`                     |
| 鞋的 Feet | `caenarvon\csplay\gala_heelsa.tri` 的 `Feet` shape    | ❌ 只有 `AnkleSize` + `FeetFeminine` + `Heel`            |
| 鞋的鞋体  | `caenarvon\csplay\gala_heelsa.tri` 的 `heelA_*` shape | ❌ 只有 `AnkleSize` + `FeetFeminine` + `buffer` + `Heel` |
| toenails  | `caenarvon\csplay\cpb_nails.tri`                      | ❌ 只有 `NailsLong` + `NailsSharp` + `NailsBuffer`       |

**`NoHeel` 的唯一视觉载体是裤袜。** 裸足、鞋、toenails 的 TRI 里都没有 `NoHeel` target。

**脚部变形由鞋的 `Heel` morph 控制**（`gala_heelsa.tri` 的 `Feet` shape 有 `Heel`），由 RaceMenu High Heel 系统设置，不是我们的 `NoHeel`。

**对插件的影响**：

- `[ApplyOther]` 对 toenails、鞋、裸足也调 `ApplyVertexDiff(false)`，但它们的 TRI 里没有 `NoHeel`，`ApplyMorph` 静默失败——**无害，但不是"没有处理"**。
- "脚部没有变形"不是 bug，是 `NoHeel` 的职责范围只有裤袜。
- 排查时不要假设"所有 body mesh 都响应所有 morph"。

---

## 4. 关键技术事实澄清

### 4.1 `REL::ID` 不是指针

**旧文档**：未涉及。

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

### 4.2 `skyrim_cast` 走引擎 RTTI 继承链

**旧文档**：未涉及。

**v3.0.4 期间确认**：`skyrim_cast<T*>(obj)` 走引擎 RTTI 的继承链，能识别 `BSFadeNode`、`BSFaceGenNiNode` 等 `NiNode` 子类。**不要用 `GetRTTI() == Xxx::Ni_RTTI` 精确比较**，会漏掉子类。

```cpp
// 错误：只匹配精确的 NiNode，漏掉 BSFadeNode 等子类
if (a_object->GetRTTI() == NiNodeRTTI()) {
    auto* node = static_cast<RE::NiNode*>(a_object);   // 编译错误：不能 static_cast 到错误类型
}

// 正确：skyrim_cast 走继承链
auto* node = skyrim_cast<RE::NiNode*>(a_object);
if (node != nullptr) {
    // node 是 NiNode 或其子类
}
```

**日志证据**（v3.0.4 WIP 期间 `[ApplyOther]` 只对裤袜触发，body/nails/shoes 全部被跳过）：RTTI 精确匹配漏掉了 `BSFadeNode` 等子类。

### 4.3 `partClone->parent` 是"mesh 就绪"的唯一信号

**旧文档**：未涉及。

**v3.0.4 期间确认**：判断 mesh 是否已挂到场景图、顶点是否就绪，唯一可靠的信号是 `partClone->parent != nullptr`。

| 门禁                           | 优点                                            | 缺点                                              |
| ------------------------------ | ----------------------------------------------- | ------------------------------------------------- |
| `HasShapeData`                 | 直接反映 SKEE 是否处理过                        | 依赖 `HasMorphs`，`NoHeel=0` 时误判（§2.2、§2.4） |
| RTTI 精确匹配                  | 简单                                            | 漏子类（§4.2）                                    |
| `partClone->parent != nullptr` | mesh 已挂到场景图 = 顶点就绪；正交于 morph 状态 | 需要 `NiAVObject::parent` 字段（CommonLibSSE 有） |
| `lastUpdatedFrameCounter`      | 可判断"本帧是否更新过"                          | 需要额外逻辑                                      |

**判据**：`parent != nullptr` 是唯一正确的"mesh 就绪"信号。不要用 `SHAPEDATA`（依赖 `HasMorphs`），不要用 RTTI 精确匹配（漏子类）。

---

## 5. 对插件设计的更新影响

旧文档 §8 的"对插件设计的影响"表格，v3.0.4 期间的更新：

| SKEE 行为                                                          | 旧设计（v3.0.2/v3.0.3）                                         | v3.0.4 设计                                                                                               |
| ------------------------------------------------------------------ | --------------------------------------------------------------- | --------------------------------------------------------------------------------------------------------- |
| `OnAttach` 在 mesh 就绪后调用                                      | `AttachListener` 用 `OnAttach` 驱动其他 mesh 更新               | `AttachListener` 用 `OnAttach` 驱动当前 mesh 更新                                                         |
| `BodyMorphInterface::OnAttach` 有 `HasMorphs` 条件                 | `AttachListener` 在 `hasShapeData=false` 时兜底 `ApplyCurrent`  | `AttachListener` 在 `OnAttach` 里调 `EvaluateRules` + `ApplyVertexDiff(false)`（`hasShapeData` 不是判据） |
| `ApplyVertexDiff` 只处理传入子树                                   | `ApplyVertexDiffToWornMeshes` 遍历 `BipedAnim` 逐个调用         | 不变                                                                                                      |
| `!bodyData` 分支不受 `isAttaching` 保护                            | `ApplyVertexDiffToWornMeshes` 跳过 `hasShapeData=false` 的 mesh | **修正**：用 `partClone->parent != nullptr` 门禁                                                          |
| observer 顺序：SKEE 先于我们                                       | 当前 mesh 交给 SKEE 处理                                        | 不变（但 `OnAttach` 里我们也要主动求值）                                                                  |
| `UpdateModelWeight` 遍历所有 biped 节点                            | `OnEquipEvent` 不调 `UpdateModelWeight`                         | `OnEquipEvent` 不调；`OnActorLoaded` 仍调（额外处理 attachment holder）                                   |
| `TESEquipEvent` 期间 `GetWornArmor` 返回旧值                       | `OnEquipEvent` 用 `(armor, equipped)` 合成状态                  | 不变                                                                                                      |
| **`HasMorphs` 是"任一 morph 值非 0"**                              | 未涉及                                                          | `SHAPEDATA` 不存在可能是 `HasMorphs=false`，不是 mesh 未就绪                                              |
| **`ApplyVertexDiff(false)` 的 `!bodyData` 分支在 mesh 就绪时安全** | 标为"危险"                                                      | 修正：只在 `parent == nullptr` 时危险                                                                     |
| **`Actor::AddWornItem` 不派发 `TESEquipEvent`**                    | 未涉及                                                          | `OnAttach` 是 SSOT，必须主动求值                                                                          |
| **`NoHeel` 的视觉载体只有裤袜**                                    | 未涉及                                                          | 不要假设"所有 body mesh 都响应所有 morph"                                                                 |

---

## 6. 未验证 / 存疑

以下问题在 v3.0.4 期间**没有完全验证**，留给后续调查：

1. **Modex 三件同穿的 attach 顺序依赖**：`OnAttach` 只处理当前 mesh。如果裤袜先 attach、高跟后 attach，裤袜可能不响应高跟的 `NoHeel` 变化。本地测试恰好是安全顺序（toenails → 高跟 → 裤袜），反序未测。需要 rudeboybox 复测。

2. **`BodyMorphInterface::OnAttach` 对脚趾甲不创建 `SHAPEDATA` 的确切原因**：`HasMorphs` 为假（脚趾甲只有 `NailsLong`、`NailsSharp`、`NailsBuffer`，全是 0）还是 TRI 不匹配？`skee.log` 里脚趾甲 attach 时没有 `MorphCache::CacheFile - Parsing: cpb_nails.tri` 行——说明 `HasMorphs=false` 时 SKEE 完全不处理，连 TRI 文件都不加载。这与 §3.3 的 `BODYTRI` 查找路径一致。

3. **`SHAPEDATA` 的 `vertexCount` 越界风险**（多 partition 场景）：见 `toenail-report-2026-10-27.md` §2.4.1。脚趾甲单 partition，未触发。

4. **`g_actorUpdateManager.isReverting()` 期间 `OnAttach` 的行为**：SKEE 的 `BodyMorphInterface::OnAttach` 在 `isReverting()` 为 true 时走 `AddBodyUpdate(refr->formID)` 而不是 `ApplyVertexDiff`。我们的 `AttachListener::OnAttach` 不检查 `isReverting`，会在 `isReverting` 期间主动求值 + 应用。是否有问题未验证。

5. **`MorphCache::CacheFile` 的并行加载与主线程同步**：`g_parallelMorphing` 为 true 时 `MorphFileCache::ApplyMorphs` 用 `concurrency::create_task` 并行处理 `vertexMap`，但 `NIOVTaskUpdateSkinPartition::Run()` 仍是主线程同步。是否有竞态未验证。

---

## 7. 相关文件

| 文件                                                | 作用                                                               |
| --------------------------------------------------- | ------------------------------------------------------------------ |
| `docs/skee-behavior-notes.md`                       | 旧版（v3.0.2/v3.0.3 快照，冻结）                                   |
| `docs/v3.0.4-ssot-refactor.md`                      | v3.0.4 主文档（架构决策、时序、验证、边界）                        |
| `docs/v3.0.4-pitfalls-and-regression-guards.md`     | 踩坑与防回归                                                       |
| `docs/onattach-d3d-regression-report-2026-10-29.md` | v3.0.3 回归调查                                                    |
| `docs/toenail-report-2026-10-27.md`                 | 脚趾甲异常调查报告                                                 |
| `vendor/SKEE/include/IPluginInterface.h`            | SKEE 接口定义（插件侧可见的唯一 SKEE 头文件）                      |
| `skee64/BodyMorphInterface.cpp`                     | SKEE 实现：`ApplyVertexDiff`、`OnAttach`、`SHAPEDATA`、`HasMorphs` |
| `skee64/ActorUpdateManager.cpp`                     | observer 注册与遍历                                                |
| `skee64/AttachmentInterface.cpp`                    | `NiAttachments [NiOA]` 挂载逻辑                                    |

---

**文档结束。**
