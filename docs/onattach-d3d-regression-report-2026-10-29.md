# v3.0.3 回归调查报告：OnAttach 栈内的 D3D 操作与两个用户 bug

**版本**：v3.0.3 已发布后的回归调查
**日期**：2026-10-29
**报告人**：Charlene Hoo
**状态**：根因已定位，修复方向已确定，待实现与验证
**关联**：

- [`toenail-report-2026-10-27.md`](toenail-report-2026-10-27.md) — v3.0.3 的设计来源
- v3.0.2 → v3.0.3 的实现变更

---

## 1. 问题描述

### 1.1 rudeboybox 的复测反馈（v3.0.3 发布后）

> Hi Charlene,
> I have tested the new version. Although you did manage to fix the toenail-hosiery-sandal combo, meaning they can be assigned to an NPC all at once and all appear and behave as expected, when it is just toenails and sandals, the former seem to be falling out of sync and floating, as if the morph wasn't applied. Specifically, this occurs when the NPC starts flat-footed (toenails in their right place), and then assign sandals: the toenails appear displaced and floating.
> All-in-all, this is a worse situation compared to the previous one where, once initialized, everything would work smoothly and reliably. That is, 3.0.2 better than 3.0.3.
> Kind regards!

**关键现象**：

| 场景                                           | v3.0.2           | v3.0.3                    |
| ---------------------------------------------- | ---------------- | ------------------------- |
| 新 NPC 首次同帧穿 toenails + hosiery + sandals | 脚趾甲闪烁后消失 | 正常 ✅                   |
| 新 NPC 平底足单独穿 toenails，再穿 sandals     | 正常             | **toenails 错位/浮动** ❌ |
| 已初始化 NPC 重复上述操作                      | 稳定             | 稳定                      |

**v3.0.3 修好了首次同帧，但引入了“旧装备不响应新装备 attach”的回归。** 用户评价“3.0.2 优于 3.0.3”。

### 1.2 第二个用户报告（2026-10-28）

> 最新版本似乎会和 oar---IEDcondiction 产生冲突，使部分依赖 IED 的动作模组出问题

**关键信息**：

- 冲突对象：**OAR - IED Conditions**（给 OAR 添加 IED/SDS 条件的插件），可能包含 IED 本体。
- IED 不依赖 SKEE。
- IED 有“大量和 D3D 相关的模块”。
- 现象：依赖 IED 条件的 OAR 动画模组失效。

---

## 2. diff 拆解：v3.0.3 相对 v3.0.2 改了什么

v3.0.3 相对 v3.0.2 只有两处行为变化：

**变化 1：`OnEquipEvent` 不再调 `UpdateModelWeight`**

v3.0.2：`TESEquipEvent` 栈里同步调 `UpdateModelWeight`，触发 D3D 操作。

v3.0.3：`OnEquipEvent` 只 `SetAbsolute`，不碰 D3D。

**变化 2：新增 `AttachListener`，在 `OnAttach` 里调 `ApplyVertexDiff`**

`AttachListener` 通过 SKEE 的 `IActorUpdateManager` 注册，`OnAttach` 在 SKEE 的 `AttachBipedObject_Hooked` 中同步派发。v3.0.3 把 D3D 操作**移进了 `AttachBipedObject` 的 hook 链**。

日志印证：`[ApplyCurrent]` 和 `[ApplyOther]` 之后，SKEE 内部走 `NIOVTaskUpdateSkinPartition::Run()`，做 `UpdateSubresource` / `Map` / `Unmap` / `CopyResource`，需要 `ID3D11DeviceContext`。此时引擎正在 attach mesh，D3D 状态可能被引擎持有。

---

## 3. 根因分析：D3D 操作放错了栈

### 3.1 两个 bug 的共同根因

v3.0.3 的核心错误决策：**把“其他已存在 mesh 的 morph 重应用”塞进了 `AttachBipedObject` 的 hook 栈。**

`OnAttach` 的本来职责是“当前 mesh 就绪信号”，用它创建当前 mesh 的 `SHAPEDATA` 是正确的（SKEE 自己的 `BodyMorphInterface::OnAttach` 就在做这件事）。但“已存在 mesh 响应新 morph 值”的触发源是**morph 值变了**（`SetAbsolute` 之后），不是**某个 mesh attach 了**。把后者当成前者的信号，导致 D3D 操作在引擎 attach mesh 的窗口里执行。

### 3.2 两个 bug 的具体表现

**Bug 1（toenails 浮动）**：

v3.0.3 的 `ApplyVertexDiffToWornMeshes` 用 `HasShapeData(data.partClone)` 判断“这个 mesh 是否已就绪”。但 `SHAPEDATA` 挂在**子 geometry**（`BSGeometry`）上，不在 armor addon 的**根节点**（`data.partClone`）上。`HasShapeData` 检查根节点，永远为 false，于是 toenails 作为“其他 mesh”被 `skip`，morph 没有被重新应用。sandals attach 后脚部状态变化，toenails 保持旧顶点，出现浮动。

**即使修复 `HasShapeData` 让它递归检查子 geometry，toenails 会在 sandals 的 `OnAttach` 栈里被真正 `ApplyVertexDiff(false)`——这正是 Bug 2 的模式。** Bug 1 和 Bug 2 不是独立 bug，是同一个错误决策的两个表现。

**Bug 2（IED 冲突）**：

v3.0.3 把 D3D 操作从 `TESEquipEvent` 栈移进了 `AttachBipedObject` hook 栈。IED 如果也在同一 hook 链里做 D3D 操作（它有大量 D3D 模块），两个插件在同一个不稳定的 D3D 窗口里交错，冲突概率显著上升。v3.0.2 的 `UpdateModelWeight` 在 `TESEquipEvent` 栈里，引擎不在 attach mesh，D3D 状态相对稳定，所以没有冲突报告。

### 3.3 为什么 v3.0.2 “感觉更好”

v3.0.2 的 `UpdateModelWeight` 在 `TESEquipEvent` 栈里，不在 attach 栈里。对已初始化的 NPC，它的 `SHAPEDATA` 已正确，`UpdateModelWeight` 只是用正确 base 重新应用新值，D3D 操作在相对稳定的窗口里。所以“初始化后稳定可靠”。

v3.0.2 只错了一件事：首次同帧时 mesh 未就绪，`UpdateModelWeight` 为未就绪 mesh 创建了错误 `SHAPEDATA`。这个问题只需要 `OnAttach` 里对**当前 mesh** 兜底就能解决，不需要把“其他 mesh 的更新”也搬进 `OnAttach`。

---

## 4. `UpdateModelWeight` 的精确拆解

### 4.1 完整调用链

```text
IBodyMorphInterface::UpdateModelWeight(refr, immediate=false)
  → BodyMorphInterface::Impl_UpdateModelWeight(refr, immediate)
      actor = DynamicCast<Actor>(refr)
      task = new NIOVTaskUpdateModelWeight(actor)
      if immediate: task->Run(); task->Dispose()
      else: g_task->AddTask(task)          ← 非 immediate 时先排队
  → NIOVTaskUpdateModelWeight::Run()
      actor = LookupFormByID(m_formId)
      g_bodyMorphInterface.ApplyBodyMorphs(actor, false)
  → BodyMorphInterface::Impl_ApplyBodyMorphs(refr, deferUpdate=false)
      if g_enableBodyMorph: morphCache.UpdateMorphs(refr, false)
  → MorphCache::UpdateMorphs(refr, deferUpdate=false)
      VisitBipedNodes(refr, λ(bool isFP, UInt32 slot, NiNode* root, TESForm* armo,
                              TESForm* arma, NiAVObject* object) {
          ApplyMorphs(refr, object, false, deferUpdate);       ← 每个 biped mesh 一次
      });
      VisitSkeletalRoots(refr, λ(NiNode* root, bool) {
          if (attachmentHolder) ApplyMorphs(refr, attachmentHolder, false, deferUpdate);
      });
  → MorphCache::ApplyMorphs(refr, rootNode, isAttaching=false, deferUpdate)
      在 rootNode 子树里找 BODYTRI，CacheFile，拿到 MorphFileCache
      fileCache->ApplyMorphs(refr, rootNode, isAttaching, deferUpdate)
  → MorphFileCache::ApplyMorphs(refr, rootNode, isAttaching, defer)
      for each shape in vertexMap:
          ApplyMorph(refr, rootNode, isAttaching, it)
              → 找匹配 shape 的 geometry
              → DeepCopy skinPartition
              → 用 SHAPEDATA 重置 vertex（仅 !isAttaching）
              → 若 !SHAPEDATA 则创建
              → 应用 morph delta
              → partitionUpdates.push(new NIOVTaskUpdateSkinPartition(...))
      defer = (*g_main)->threadId != GetCurrentThreadId()   ← 参数被覆盖
      for update in partitionUpdates:
          if defer: g_task->AddTask(update)
          else: update->Run(); update->Dispose()
  → NIOVTaskUpdateSkinPartition::Run()
      EnterCriticalSection(&g_renderManager->lock)
      UpdateSubresource / Map / Unmap / CopyResource
      LeaveCriticalSection
```

**两个关键点**：

1. **`deferUpdate` 参数在 `MorphFileCache::ApplyMorphs` 里被覆盖**为 `(*g_main)->threadId != GetCurrentThreadId()`。在 `TESEquipEvent` / `OnAttach` 里都是主线程，所以 `NIOVTaskUpdateSkinPartition::Run()` 是**同步执行**的，D3D 操作立即发生。

2. **`UpdateModelWeight(immediate=false)` 的第一个 `g_task->AddTask` 只在非主线程调用时发生**。在主线程调用时，整条链从头到尾同步。

### 4.2 `DoUpdate(e)` 的精确对应物

```text
DoUpdate(e) ≈ MorphCache::ApplyMorphs(refr, e, isAttaching=false, defer=?)
```

而 `IBodyMorphInterface::ApplyVertexDiff(refr, e, false)` 的内部实现正是：

```cpp
void Impl_ApplyVertexDiff(TESObjectREFR* refr, NiAVObject* rootNode, bool attach)
{
    if (g_enableBodyMorph)
        morphCache.ApplyMorphs(refr, rootNode, attach, true);
}
```

所以：

> **`DoUpdate(e)` = `ApplyVertexDiff(refr, e, false)`**，当 `e` 是 `bipedData` 里某个 `data.partClone` 时，两者对同一个 mesh 做的 D3D 操作**完全相同**。主线程下都是同步执行。

### 4.3 `UpdateModelWeight` 比单次 `ApplyVertexDiff` 多的东西

1. **attachment holder（`NiAttachments [NiOA]`）**：SKEE `AttachmentInterface` 挂上去的动态 mesh，不在 `bipedData` 里。
2. **`Shrink()`**：cache 管理，和 morph 正确性无关。

对 `bipedData` 里的常规 armor addon，`UpdateModelWeight` 精确等于：

```text
for each biped node in VisitBipedNodes(actor):
    if node.object:
        ApplyVertexDiff(actor, node.object, false)
for each attachment holder:
    if holder:
        ApplyVertexDiff(actor, holder, false)
```

---

## 5. 修复方向

### 5.1 核心洞察：`GetWornArmor` 在 `TESEquipEvent` 期间返回旧值

报告 §7.7 已确认：`TESEquipEvent` 派发期间 `GetWornArmor` 返回事件前状态。

- **穿上事件**：`GetWornArmor` 不包含正在穿的装备 → 遍历结果全是事件前就存在的其他装备。
- **卸下事件**：`GetWornArmor` 包含正在卸下的装备，但它 mesh 还在、`SHAPEDATA` 还在，对它调 `ApplyVertexDiff(false)` 是“用正确 base 重置 + 应用 Default”，随后引擎移除 mesh。多余但无害；想省一次 D3D 可以显式排除。

关键：**遍历 `GetWornArmor` 拿到的 armor，其 mesh 一定已经 attach 过**。v3.0.2 的 `UpdateModelWeight` 走 `bipedData`，`bipedData` 在事件期间可能已经包含新装备的记录（引擎已经开始更新），于是为未就绪 mesh 创建了错误 `SHAPEDATA`。换成 `GetWornArmor` 旧值遍历，这个坑自动消失。

### 5.2 精确的伪代码

```text
UpdateModelWeight(actor) =
    for (bipedNode in VisitBipedNodes(actor)):
        if bipedNode.object:
            ApplyVertexDiff(actor, bipedNode.object, false)
    for (holder in attachmentHolders(actor)):
        if holder:
            ApplyVertexDiff(actor, holder, false)

我们要做的 =
    oldArmors = { GetWornArmor(slot) for all slots }   // 旧值
    for (bipedNode in VisitBipedNodes(actor)):
        if bipedNode.armor in oldArmors:
            ApplyVertexDiff(actor, bipedNode.object, false)
```

**遍历源仍然是 `bipedData`**（通过 `VisitBipedNodes` 或等价遍历），不是直接遍历 `oldArmors`——因为 `oldArmors` 是 `TESObjectARMO*`，需要映射到 mesh。用 `bipedData` 遍历 + 集合过滤是最高效的：`bipedData` 已经给出了 `(armor, addon, object)` 三元组，只需检查 `armor` 是否在 `oldArmors` 里。

**“在 `oldArmors` 里”这个检查天然排除了当前装备**，因为 `GetWornArmor` 在 `TESEquipEvent` 期间返回旧值。这就是为什么不需要 `HasShapeData` 门禁——集合成员资格保证了 mesh 已就绪。

### 5.3 修复后的结构

**`OnEquipEvent`**（`TESEquipEvent` 栈）：

```text
SetAbsolute(...)
oldArmors = collect GetWornArmor(all slots)   // 旧值
for k in {0, 1}:
    weightModel = actor.GetBiped(k)
    if !weightModel || !weightModel.bipedData: continue
    for data in {objects, bufferedObjects}:
        if !data.partClone: continue
        armor = data.item ? data.item->As<TESObjectARMO>() : nullptr
        if armor == nullptr: continue
        if armor 不在 oldArmors 中（按 FormID 比较）: continue
        // 可选：if (armor == a_armor) continue;  // 卸下事件省一次
        ApplyVertexDiff(actor, data.partClone, false)
```

**`AttachListener::OnAttach`**：

```text
只保留当前 mesh 的兜底：
if (!HasShapeData(a_object)) {
    ApplyVertexDiffCurrentMesh(...)   // isAttaching=true
}
// 删掉 ApplyVertexDiffToWornMeshes 调用
```

**`HasShapeData`**：只保留给“当前 mesh 是否需要兜底”这个判断用。`ApplyVertexDiffToWornMeshes` 里的门禁删除。

### 5.4 为什么这个方案同时消灭两个 bug

**Bug 1（toenails 浮动）**：toenails 是旧装备，会被 `oldArmors` 包含，遍历到它的 `partClone` 并调 `ApplyVertexDiff(false)`。不经过错误的 `HasShapeData` 门禁，所以不会被跳过。sandals attach 后，toenails 在同一事件里被正确重应用 morph。

**Bug 2（IED 冲突）**：D3D 操作回到 `TESEquipEvent` 栈，不在 `AttachBipedObject` hook 栈里。v3.0.2 在同样的栈里做过 D3D 操作，没有 IED 冲突报告。

**首次同帧穿 toenails + hosiery**：`OnEquipEvent` 时两者都不在 `oldArmors` 里（旧值为空），不会被遍历。它们的初始化由各自的 `AttachListener::OnAttach` 兜底（`[ApplyCurrent]`）完成。v3.0.3 已验证这条路径正确。

---

## 6. 决策依据

### 6.1 为什么不在 `OnAttach` 栈里做“其他 mesh 的更新”

`OnAttach` 的本来职责是“当前 mesh 就绪信号”。SKEE 自己的 `BodyMorphInterface::OnAttach` 就在这个栈里为当前 mesh 创建 `SHAPEDATA`，引擎和 SKEE 对此有预期。

但“已存在 mesh 响应新 morph 值”的触发源是**morph 值变了**（`SetAbsolute` 之后），不是**某个 mesh attach 了**。把后者当成前者的信号，就把 D3D 操作塞进了引擎 attach mesh 的窗口里。这是 v3.0.3 两个 bug 的共同来源。

### 6.2 为什么 `TESEquipEvent` 栈是安全的

v3.0.2 的 `UpdateModelWeight` 就在 `TESEquipEvent` 栈里做同步 D3D 操作，没有 IED 冲突报告。区别只在于 v3.0.2 用 `bipedData` 遍历踩了“未就绪 mesh”的坑。换成 `GetWornArmor` 旧值遍历，避开未就绪 mesh，`TESEquipEvent` 栈的安全性由 v3.0.2 的实际使用历史背书。

### 6.3 为什么 `HasShapeData` 门禁可以完全去掉

v3.0.3 的 `HasShapeData` 门禁是在 `OnAttach` 栈里为了判断“这个 mesh 是否已就绪”而加的，因为那里无法区分当前 mesh 和旧 mesh。

换到 `OnEquipEvent` + `GetWornArmor` 后，“就绪”由集合成员资格保证。`oldArmors` 里的 armor 一定是事件前就存在的，其 mesh 早已 attach。`SHAPEDATA` 存在则用正确 base 重置，不存在则当前顶点就是 base——两种情况都安全。门禁不再需要。

### 6.4 为什么遍历源用 `bipedData` 而不是直接遍历 `oldArmors`

`oldArmors` 是 `TESObjectARMO*` 集合，需要映射到 mesh。`bipedData` 已经给出了 `(armor, addon, object)` 三元组，只需检查 `armor` 是否在集合里。这比“对每个 armor 调 `VisitArmorAddon`”更直接，也比“遍历所有 armor 找 mesh”更省。

### 6.5 与 v3.0.2 的精确对比

| 维度                | v3.0.2                                 | 本方案                            |
| ------------------- | -------------------------------------- | --------------------------------- |
| 遍历源              | `bipedData` 全部槽                     | `GetWornArmor` 旧值 ∩ `bipedData` |
| 当前装备            | 被遍历，未就绪顶点创建错误 `SHAPEDATA` | 不在旧值集合里，不遍历            |
| 旧装备              | 被遍历，正确更新                       | 被遍历，正确更新                  |
| D3D 栈              | `TESEquipEvent`                        | `TESEquipEvent`                   |
| `HasShapeData` 门禁 | 无                                     | 无（旧装备天然就绪）              |

本方案 = v3.0.2 的正确部分（旧装备在 `TESEquipEvent` 栈里更新）+ 精确排除当前装备（用 `GetWornArmor` 旧值，而不是靠 `HasShapeData` 猜测就绪状态）。

---

## 7. 待确认事项

### 7.1 `GetWornArmor` 返回的 armor 与 `data.item` 的指针一致性

用 FormID 比较而非指针比较。同一 armor 的 `As<>` 通常返回同一指针，但跨 DLL 的指针比较不如 FormID 可靠。`data.item` 是 `TESForm*`，`GetWornArmor` 返回 `TESObjectARMO*`，`As<TESObjectARMO>()` 后比较 `GetFormID()`。

### 7.2 attachment holder（`NiAttachments [NiOA]`）里的动态 mesh

`UpdateModelWeight` 会额外遍历它。如果用户的场景涉及 SKEE `AttachmentInterface` 挂上去的动态 mesh（不是常规 armor addon），只遍历 `bipedData` 会漏掉它们。

初期版本可以只处理 `bipedData`，如果 rudeboybox 或其他用户报告“动态附件不响应 morph”，再补 attachment holder 的遍历。`GetWornArmor` 不覆盖这些动态 mesh，所以补的时候需要单独判断——但这是边缘场景，不阻塞当前修复。

### 7.3 IED 的 hook 位置

用 HookMap 或类似工具确认 IED / IED Conditions 是否 hook 了 `AttachBipedObject` 同一地址。即使本方案把 D3D 操作移回 `TESEquipEvent` 栈，如果 IED 也在 `TESEquipEvent` 栈里做 D3D 操作，仍可能有竞争。但从 v3.0.2 的实际使用历史看，没有 IED 冲突报告，说明 `TESEquipEvent` 栈对 IED 是安全的。

### 7.4 OAR IED Conditions 的注册顺序

`AttachListener` 在 `kPostPostLoad` 注册到 `IActorUpdateManager`。虽然它不直接注册 OAR 条件，但它在 `kPostPostLoad` 的 handler 链里增加了一个新的消费者。如果 OAR IED Conditions 的初始化存在 race（已知问题：某些加载顺序下 7 个条件只注册上 2 个），新增的消费者可能把原本脆弱的 race 推向失败的一侧。

需要用户提供 `SKSE.log` 确认 OAR 是否报告条件缺失。如果有，可能需要把 `AttachListener` 的注册挪到 `kPostLoad` 或延迟到 `kPostPostLoad` 末尾。但这是次要怀疑对象，主要根因仍是 7.3 的 D3D 栈问题。

---

## 8. 验证清单

实现后让 rudeboybox 测：

| 场景                                           | 期望                                |
| ---------------------------------------------- | ----------------------------------- |
| 新 NPC 首次同帧穿 toenails + hosiery + sandals | 正常（保持 v3.0.3 的修复）          |
| 新 NPC 平底足单独穿 toenails，再穿 sandals     | 正常（修复 v3.0.3 的回归）          |
| 已初始化 NPC 重复上述操作                      | 稳定                                |
| 已穿 toenails + hosiery，再穿高跟鞋            | toenails 和 hosiery 响应 `NoHeel=0` |
| 玩家读档，已穿 toenails + hosiery              | 正常（`OnActorLoaded` 兜底保持）    |
| 同时安装 OAR IED Conditions 和本插件           | 依赖 IED 条件的 OAR 动画正常        |

**日志验证**：`[ApplyOther]` 应该出现在 `[Equip]` 之后、`[Attach]` 之前（`TESEquipEvent` 栈里），而不是 `[Attach]` 之后。`hasShapeData` 门禁日志应该消失。`[ApplyOther]` 只列出 `oldArmors` 里的 armor，当前装备不在其中。

---

## 9. 变更记录

### v3.0.4（待发布）

**修复**：

- `OnEquipEvent` 恢复对旧装备的 `ApplyVertexDiff(false)`，用 `GetWornArmor` 旧值集合过滤，精确排除当前正在 attach 的装备。
- `AttachListener::OnAttach` 只保留当前 mesh 的兜底（`isAttaching=true`），删除 `ApplyVertexDiffToWornMeshes` 调用。
- 删除 `ApplyVertexDiffToWornMeshes` 中的 `HasShapeData` 门禁。

**根因**：v3.0.3 把“其他 mesh 的更新”塞进了 `AttachBipedObject` hook 栈，导致（a）`HasShapeData` 检查根节点误判漏更新（Bug 1），（b）D3D 操作与 IED 的 hook 交错（Bug 2）。

**验证**：见 §8 验证清单。

---

**报告结束。**
