# 脚趾甲 morph 异常调查报告

**版本**：v3.0.3 未发布
**日期**：2026-10-27
**报告人**：Charlene Hoo
**状态**：根因已定位，修复方案待验证

---

## 1. 问题描述

### 1.1 报告来源

Nexus 用户 **rudeboybox** 在 v3.0.2 发布后报告：

> A very minor glitch emerged playing around with toenails. Similarly to what happened before for hosiery, they also seem to need an initialization round, before working correctly with hosiery/footwear. It works like this: on any character (NPC), the first time you wear a combination of toenails and hosiery/footwear, for the toenails to show you need to equip them by themselves first (on a naked foot), and add hosiery and/or footwear, afterwards. If instead of following this sequence you add toenails and hosiery / footwear at the same time, the toenails don't appear, or flicker for an instant and disappear. This does not happen with footwear not touched by your mod. After an NPC has been 'initialized' in this way, you can add toenails and hosiery/footwear together and they will show correctly.

### 1.2 关键现象

| 场景                                 | 结果                 |
| ------------------------------------ | -------------------- |
| 单独穿脚趾甲                         | 正常                 |
| 先穿脚趾甲，再穿裤袜/鞋              | 正常                 |
| 同帧同时穿脚趾甲 + 裤袜/鞋（首次）   | 闪烁一瞬后消失       |
| 上述场景执行过一次后，再次同帧同时穿 | 正常                 |
| 穿 vanilla 鞋（无 morph target）     | 正常，脚趾甲不受影响 |

**“初始化后永久正常”** 是本次调查最关键的线索，它排除了“每次处理都出错”的假设，指向“首次时机问题”。

### 1.3 v3.0.3 的盲修

v3.0.3（未发布）引入了异步防抖队列：

```cpp
void SKEEIntegration::UpdateModelWeight(RE::Actor& a_actor) {
    m_pendingUpdates.insert(a_actor.GetFormID());
    if (m_updateScheduled) return;
    m_updateScheduled = true;
    SKSE::GetTaskInterface()->AddTask([this]() { FlushPendingUpdates(); });
}
```

假设根因是“同帧多次 `UpdateModelWeight` 导致 D3D buffer 竞态”。本次调查验证了这个假设**不完整**。

---

## 2. SKEE 代码分析

### 2.1 调用链

```
插件: SetAbsolute → ClearMorph + SetMorph
  └─ 纯数据写入 actorMorphs[formID][morphName][morphKey]，不碰 mesh

插件: UpdateModelWeight(actor)  // immediate=false
  └─ Impl_UpdateModelWeight
       └─ new NIOVTaskUpdateModelWeight(actor)
       └─ g_task->AddTask(updateTask)          ← 排到主线程任务队列
            └─ NIOVTaskUpdateModelWeight::Run()
                 └─ ApplyBodyMorphs(actor, false)
                      └─ Impl_ApplyBodyMorphs → morphCache.UpdateMorphs(refr, false)
                           └─ 对每个 biped node / attachment node:
                                MorphCache::ApplyMorphs(refr, object, false, defer)
                                  └─ 找 BODYTRI → fileCache->ApplyMorphs(...)
                                       └─ 对每个 shape: MorphFileCache::ApplyMorph(...)
                                            └─ DeepCopy NiSkinPartition
                                            └─ 若 bodyData 存在且 !isAttaching: memcpy 重置 vertex data
                                            └─ 应用 morph 到 partition[0] 的 raw vertex data
                                            └─ 传播到 partition[1..N]
                                            └─ new NIOVTaskUpdateSkinPartition(...)
                                                 └─ 在 ApplyMorphs 末尾:
                                                      defer = (*g_main)->threadId != GetCurrentThreadId()
                                                      if (!defer) update->Run()  ← 主线程同步执行
```

**关键事实**：`MorphFileCache::ApplyMorphs` 里的 `defer` 被**强制覆盖**为 `(*g_main)->threadId != GetCurrentThreadId()`。因为 `NIOVTaskUpdateModelWeight::Run()` 本身就在主线程的 Task 里执行，所以 `defer = false`，每个 shape 的 `NIOVTaskUpdateSkinPartition::Run()` 是**同步执行**的。

一次 `UpdateModelWeight(false)` = 一个主线程 Task = 同步完成所有匹配 geometry 的 `NiSkinPartition` DeepCopy + D3D buffer 更新 + `m_skinInstance->m_spSkinPartition` 替换。

### 2.2 SHAPEDATA 机制

`MorphFileCache::ApplyMorph` 的核心逻辑：

```cpp
bool existingMorphs = !isAttaching && bodyData;

if (bodyMorph.second.HasMorphs(refr) || existingMorphs) {
    NiSkinPartition* newSkinPartition = nullptr;
    CALL_MEMBER_FN(skinPartition, DeepCopy)((NiObject**)&newSkinPartition);

    // 重置 vertex data（仅 !isAttaching 时）
    if (bodyData && newSkinPartition && !isAttaching) {
        for (UInt32 p = 0; p < newSkinPartition->m_uiPartitions; ++p) {
            auto& partition = newSkinPartition->m_pkPartitions[p];
            UInt32 vertexSize = newSkinPartition->GetVertexSize(partition.vertexDesc);
            memcpy(partition.shapeData->m_RawVertexData, bodyData->m_data,
                   newSkinPartition->vertexCount * vertexSize);
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

**关键设计**：`SHAPEDATA` 是“未经任何 morph 修改的原始顶点”的快照。每次 `ApplyMorph` 都先 `DeepCopy` partition，用 `SHAPEDATA` 重置 vertex data，再应用 morph delta。

**关键缺陷**：`if (!bodyData)` 创建 `SHAPEDATA` 的分支**不受 `isAttaching` 保护**。无论走 `OnAttach` 的 `isAttaching=true` 路径，还是 `UpdateModelWeight` 的 `isAttaching=false` 路径，只要 geometry 上没有 `SHAPEDATA`，就会**立即**用当前 `m_RawVertexData` 创建一份 base 快照。如果此时 mesh 未完全就绪，快照就是错误的，且**永久固化**——后续每次 `ApplyMorph` 都用这份错误的 base 重置。

### 2.3 脚趾甲的处理路径

经用户确认：**脚趾甲有自己的形态键（morph target）**，`UpdateModelWeight` 遍历时会处理它。

这意味着：

- 脚趾甲有自己的 `BODYTRI`（或其 mesh 被包含在裤袜的 TRI 文件中）
- `UpdateModelWeight` 对脚趾甲的 geometry 执行 `ApplyMorph`
- 首次执行时，如果脚趾甲 mesh 未就绪，`SHAPEDATA` 捕获错误的 base

### 2.4 其他潜在问题（次要）

**2.4.1 `bodyData` 大小计算越界风险**

```cpp
bodyData = NiBinaryExtraData::Create("SHAPEDATA",
    reinterpret_cast<char*>(partition.shapeData->m_RawVertexData),
    newSkinPartition->vertexCount * vertexSize);   // ← 用总 vertexCount

// 重置时
for (UInt32 p = 0; p < newSkinPartition->m_uiPartitions; ++p) {
    auto& partition = newSkinPartition->m_pkPartitions[p];
    memcpy(partition.shapeData->m_RawVertexData, bodyData->m_data,
           newSkinPartition->vertexCount * vertexSize);   // ← 用总 vertexCount
}
```

`NiSkinPartition::vertexCount` 是总 vertex count，而 `partition.shapeData->m_RawVertexData` 是**该 partition 自己的** buffer。多 partition 场景下有越界风险。脚趾甲通常单 partition，影响较小，但值得在 SKEE 仓库提 issue。

**2.4.2 `g_bodyMorphRebind` 的 buffer 替换**

如果开启，`NIOVTaskUpdateSkinPartition::Run` 会创建新 D3D buffer 并替换所有 partition 的 buffer，旧 buffer 被 `Release`。对独立 armor 影响较小，但如果 mesh 共享 buffer，可能出问题。

---

## 3. 根因分析

### 3.1 排除的假设

| 假设                                                                                    | 排除理由                                                                                                       |
| --------------------------------------------------------------------------------------- | -------------------------------------------------------------------------------------------------------------- |
| 名字匹配误伤（脚趾甲 geometry 名和裤袜 TRI shape 名相同，被裤袜的 `ApplyMorph` 误处理） | 误伤应该是**每次**发生的，不是只在首次。“初始化后正常”说明错误被永久固化，而非持续误伤                         |
| 同帧多次 `UpdateModelWeight` 的 D3D buffer 竞态                                         | 合理但**不完整**。竞态能解释 flicker，但不能解释“初始化后永久正常”——竞态是每次都可能发生，不会因“初始化”而消失 |
| 共享 `NiSkinInstance` 被直接替换                                                        | 脚趾甲是独立 `TESObjectARMO`，有自己的 `NiSkinInstance`，不共享                                                |

### 3.2 锁定的根因

**脚趾甲有自己的 morph target，`UpdateModelWeight` 在 mesh 未就绪时处理它，`SHAPEDATA` 捕获了不完整的 base vertex data，之后每次 `ApplyMorph` 都用这份错误的 base 重置，脚趾甲永久变形/消失。**

具体时序：

```
同帧同时穿脚趾甲 + 裤袜：
  1. TESEquipEvent(脚趾甲) 到达 → 插件 SetAbsolute → UpdateModelWeight(actor)
  2. TESEquipEvent(裤袜) 到达 → 插件 SetAbsolute → UpdateModelWeight(actor)
  3. 引擎开始 attach 脚趾甲和裤袜的 mesh（异步）
  4. 插件的 UpdateModelWeight Task 执行（此时 mesh 可能未就绪）
     └─ 遍历到脚趾甲的 geometry
     └─ if (!bodyData) → 用当前 m_RawVertexData 创建 SHAPEDATA
     └─ 此时 m_RawVertexData 是不完整的/临时的
  5. 脚趾甲 mesh attach 完成，渲染第一帧（可见）
  6. 下一次 ApplyMorph → 用错误的 SHAPEDATA 重置 → 脚趾甲消失
```

**单独穿脚趾甲为什么正常**：只有一件装备，引擎处理快，`UpdateModelWeight` 执行时脚趾甲 mesh 大概率已就绪，`SHAPEDATA` 捕获正确 base。

**初始化后为什么正常**：单独穿一次后，`SHAPEDATA` 已用正确 base 创建；之后同时穿，`if (!bodyData)` 不成立，不会重新捕获，`ApplyMorph` 用正确 base 重置。

### 3.3 为什么合成逻辑仍然必需

v3.0.3 的防抖队列只推迟了 `UpdateModelWeight`。`EvaluateAndApply` 和 `SetAbsolute` 仍然在 `OnEquipEvent` 里同步执行。

```
OnEquipEvent（同步，事件时刻）
  ├─ 合成 wornArmor（slotMatch ? (equipped ? armor : nullptr) : GetWornArmor）
  ├─ EvaluateAndApply 求值（谓词读 wornArmor 的关键字 / FormID）
  └─ SetAbsolute 写入 actorMorphs          ← 同步，不碰 mesh

FlushPendingUpdates（延迟，下一帧 Task）
  └─ SKEE UpdateModelWeight → ApplyBodyMorphs
       └─ 读 actorMorphs 里已经写好的值 → 应用到 mesh
```

`ApplyBodyMorphs` 读的是 `actorMorphs` 里的 morph 值，不是 `GetWornArmor`。延迟它不会改变求值结果。`EvaluateAndApply` 在 `TESEquipEvent` 派发期间同步执行时，`GetWornArmor` 仍然返回事件前状态——这正是 v3.0.2 合成逻辑要解决的问题。

**结论**：即使采用延迟方案，合成逻辑也不能删。只有把**整个求值**搬到 Task 里（丢失事件语义和 `!slotMatch && equipped` 的跳过优化），才能去掉合成。这是一个架构降级，不推荐。

---

## 4. 修复方案评估

### 4.1 方案 A：v3.0.3 防抖队列（当前盲修）

**假设**：同帧多次 `UpdateModelWeight` 导致 D3D buffer 竞态。

**优点**：改动小，如果根因是竞态，一帧延迟可能刚好让 mesh 就绪。

**缺点**：

- 延迟一帧不优雅（用户评价）
- 一帧可能不够（如果 mesh attach 需要多帧）
- 不能保证 `SHAPEDATA` 在 mesh 就绪后创建——如果一帧后 mesh 仍未就绪，错误 base 仍会被捕获
- 根因是“首次时机”而非“同帧多次”，防抖只减少触发次数，不改变首次捕获的时机

**评估**：方向合理但不彻底。**建议回滚**，因为：

1. 未发布，回滚成本低
2. 根因已定位为“首次 `SHAPEDATA` 捕获时机”，防抖不能精确解决
3. “延迟一帧”引入的时序不确定性会给后续排查增加噪音

### 4.2 方案 B：OnAttach 驱动 mesh 更新（推荐）

**核心思路**：把 mesh 更新的驱动从“事件后全 actor 更新”改成“每个 mesh attach 时精确更新”。

**架构**：

```
TESEquipEvent（同步）
  ├─ 合成 wornArmor（v3.0.2 逻辑，保留）
  ├─ EvaluateAndApply 求值
  └─ SetAbsolute 写入 actorMorphs        ← 不调 UpdateModelWeight

OnAttach（每个 addon mesh attach 时）
  ├─ 判断 armor 是否匹配 CompiledRules
  └─ 对刚 attach 的 object 调 ApplyVertexDiff(refr, object, true)
       └─ 只处理这个 mesh，SHAPEDATA 在 mesh 就绪后创建
```

**实现细节**：

1. **接口注册**：`Initialize` 里多查 `IActorUpdateManager`：

   ```cpp
   auto* updateManager = static_cast<IActorUpdateManager*>(
       exchange.interfaceMap->QueryInterface("ActorUpdateManager"));
   ```

2. **Listener 实现**（`IAddonAttachmentInterface` 只有一个纯虚函数，实现成本低）：

   ```cpp
   class AttachListener final : public IAddonAttachmentInterface {
   public:
       static auto GetSingleton() -> AttachListener& {
           static AttachListener s_singleton;
           return s_singleton;
       }

       void OnAttach(RE::TESObjectREFR* a_refr, RE::TESObjectARMO* a_armor,
                     RE::TESObjectARMA* a_addon, RE::NiAVObject* a_object,
                     bool a_isFirstPerson, RE::NiNode* a_skeleton,
                     RE::NiNode* a_root) override {
           if (a_refr == nullptr || a_object == nullptr || a_armor == nullptr) return;
           // 用 CompiledRules 判断 armor 是否匹配某条 morph 规则
           if (!MatchesAnyRule(a_armor)) return;
           SKEEIntegration::GetSingleton().ApplyVertexDiff(*a_refr, *a_object, true);
       }
   };
   ```

3. **`SKEEIntegration` 新增 `ApplyVertexDiff`**：

   ```cpp
   void SKEEIntegration::ApplyVertexDiff(RE::TESObjectREFR& a_refr,
                                          RE::NiAVObject& a_object, bool a_attach) {
       if (m_bodyMorph == nullptr) return;
       m_bodyMorph->ApplyVertexDiff(&a_refr, &a_object, a_attach);
   }
   ```

4. **生命周期**：listener 必须是单例或全局对象（`AddInterface` 传裸指针，SKEE 不持有所有权）。插件卸载前 `RemoveInterface`。

5. **第一/第三人称**：同一 armor 会触发两次 `OnAttach`。两套 mesh 独立，各自有自己的 `SHAPEDATA`，**两次都要处理**。如果只关心第三人称（NPC 通常只渲染第三人称），可加 `if (a_isFirstPerson) return;` 减少开销。

6. **读档路径**：`OnActorLoaded` 里也移除 `UpdateModelWeight`，只调 `SetAbsolute`。读档时引擎会为已穿装备触发 `OnAttach`，listener 处理。如果 rudeboybox 测试发现读档后脚趾甲没更新，再在 `OnActorLoaded` 里补一次 `UpdateModelWeight`（此时 mesh 已就绪，安全）。

**优点**：

- `SHAPEDATA` 在 mesh 就绪后创建，捕获正确 base
- 每个 mesh 独立处理，不互相干扰
- 时机精确，不依赖“延迟一帧”这种启发式
- 不修改 SKEE

**缺点**：

- 多查一个接口、多一个 listener 类
- 需要维护 listener 生命周期
- `OnAttach` 对所有 armor 触发，需要在 listener 里用 `CompiledRules` 过滤
- 如果 `OnAttach` 时机本身不可靠（SKEE 自己的 `OnAttach` 也调 `ApplyVertexDiff`，说明作者认为可靠，但未验证），方案失效

**验证方法**：改完后让 rudeboybox 测两个场景：

1. 单独穿脚趾甲（应正常）
2. 同帧同时穿脚趾甲 + 裤袜（应正常）

如果同时穿正常，说明 `OnAttach` 时机正确。如果仍异常，说明 `OnAttach` 时 mesh 仍未就绪，需要更深的方案（改 SKEE）。

### 4.3 方案 C：Is3DLoaded 过滤（不推荐）

**评估**：`Is3DLoaded` 是 actor 级信号，不告诉你“新装备 mesh 是否就绪”。rudeboybox 的场景是 NPC 已在场（3D 已 loaded），给 NPC 同时穿脚趾甲 + 裤袜，此时 `Is3DLoaded` 返回 true，但新穿的脚趾甲 mesh 可能还在 attach 中。**Is3DLoaded 挡不住这个场景**。

它真正能挡住的是“NPC 刚进 cell、3D 还没构建完”的极端情况，和 rudeboybox 的问题无关。

### 4.4 方案 D：修改 SKEE（不推荐，除非 A/B 都失败）

在 `MorphFileCache::ApplyMorph` 里：

- 创建 `SHAPEDATA` 前检查 `m_RawVertexData` 是否最终就绪
- 或 `isAttaching=true` 时不创建 `SHAPEDATA`，等第一次 `isAttaching=false` 的 `ApplyMorph` 再创建

**缺点**：需要 fork SKEE 并重新编译，超出插件范围，且 SKEE 是 GPL-3.0，分发修改版需要遵守协议。

---

## 5. 决策记录

### 5.1 回滚 v3.0.3 防抖改动

**决策**：回滚 v3.0.3 的防抖队列，回到 v3.0.2 的状态（保留合成逻辑）。

**理由**：

1. v3.0.3 未发布，回滚成本低
2. 根因已定位为“首次 `SHAPEDATA` 捕获时机”，防抖不能精确解决
3. “延迟一帧”引入的时序不确定性会给后续排查增加噪音
4. 用户明确表示“延迟一帧不优雅”

**保留**：v3.0.2 的合成逻辑（`slotMatch` 分支、`!slotMatch && equipped` 跳过）**必须保留**，和防抖无关。

### 5.2 下一步：实现 OnAttach 方案

**决策**：按方案 B 实现 `OnAttach` 驱动的 mesh 更新。

**前提**：先让 rudeboybox 确认脚趾甲确实有自己的 `BODYTRI`（NifSkope 检查），确保 `OnAttach` 的 `object` 包含脚趾甲自己的 morph target。

**验证顺序**：

1. 实现 `OnAttach` listener + `ApplyVertexDiff`
2. `TESEquipEvent` 移除 `UpdateModelWeight`，保留 `SetAbsolute`
3. `OnActorLoaded` 移除 `UpdateModelWeight`，保留 `SetAbsolute`
4. 让 rudeboybox 测：单独穿、同帧同时穿、读档后
5. 如果读档后异常，`OnActorLoaded` 补回 `UpdateModelWeight`

---

## 6. 附录

### 6.1 关键代码引用

**SKEE `MorphFileCache::ApplyMorph` 的 SHAPEDATA 创建**（`BodyMorphInterface.cpp`）：

```cpp
if (!bodyData) {
    auto& partition = newSkinPartition->m_pkPartitions[0];
    UInt32 vertexSize = newSkinPartition->GetVertexSize(partition.vertexDesc);
    bodyData = NiBinaryExtraData::Create("SHAPEDATA",
        reinterpret_cast<char*>(partition.shapeData->m_RawVertexData),
        newSkinPartition->vertexCount * vertexSize);
    bodyGeometry->AddExtraData(bodyData);
}
```

**SKEE `BodyMorphInterface::OnAttach`**：

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

**`IAddonAttachmentInterface`**（`IPluginInterface.h`）：

```cpp
class IAddonAttachmentInterface {
public:
    virtual void OnAttach(RE::TESObjectREFR* refr, RE::TESObjectARMO* armor, RE::TESObjectARMA* addon,
                          RE::NiAVObject* object, bool isFirstPerson,
                          RE::NiNode* skeleton, RE::NiNode* root) = 0;
};
```

**`IActorUpdateManager::AddInterface`**：

```cpp
virtual void AddInterface(IAddonAttachmentInterface* observer) = 0;
virtual void RemoveInterface(IAddonAttachmentInterface* observer) = 0;
```

### 6.2 时序对比

```
时间轴 →

TESEquipEvent         OnAttach（脚趾甲）    OnAttach（裤袜）    Is3DLoaded 稳定
     │                      │                    │                  │
     ▼                      ▼                    ▼                  ▼
 装备数据变化          mesh 挂到场景图       mesh 挂到场景图      整个 3D 树就绪
 GetWornArmor          vertex data 就绪      vertex data 就绪
 返回旧值
```

- `TESEquipEvent` 最早，但 `GetWornArmor` 返回旧值，且 mesh 未就绪。
- `OnAttach` 是每个 mesh 单独的信号，晚于 `TESEquipEvent`，此时该 mesh 的 vertex data 已就绪。
- `Is3DLoaded` 是 actor 级，对“新 mesh 是否就绪”没有区分能力。

### 6.3 相关文件

| 文件                                           | 作用                                    |
| ---------------------------------------------- | --------------------------------------- |
| `src/Event/EventProcessor.cpp`                 | `TESEquipEvent` 入口                    |
| `src/MorphRuleEngine/MorphRuleEngine.cpp`      | `OnEquipEvent` / `OnActorLoaded` 求值   |
| `src/Integration/SKEE/SKEEIntegration.{h,cpp}` | SKEE 接口封装，需新增 `ApplyVertexDiff` |
| `src/main.cpp`                                 | `Initialize` 里注册 `OnAttach` listener |
| `docs/Architecture.md`                         | 需同步更新 §4.2、§5.2、§6.3、§7.7       |

---

**报告结束。**
