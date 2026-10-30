# OnAttach 线程亲和性调查报告

**版本**：v3.0.5 设计期
**日期**：2026-10-30
**报告人**：Charlene Hoo
**状态**：根因已定位，修复方向已确定，待实现与验证
**关联**：

- [`archived/Architecture.md`](archived/Architecture.md) — v3.0.2 快照（已冻结）
- [`Architecture.md`](Architecture.md) — v3.0.4 当前架构
- [`onattach-d3d-regression-report-2026-10-29.md`](onattach-d3d-regression-report-2026-10-29.md) — v3.0.3 回归调查
- [`v3.0.4-ssot-refactor.md`](v3.0.4-ssot-refactor.md) — v3.0.4 演进细节
- [`v3.0.4-pitfalls-and-regression-guards.md`](v3.0.4-pitfalls-and-regression-guards.md) — v3.0.4 踩坑与防回归

---

## 1. 问题描述

v3.0.4 的 `AttachListener::OnAttach` 在 `IAddonAttachmentInterface::OnAttach` 回调中执行 `EvaluateRules`（读 `GetWornArmor`、写 SKEE `actorMorphs`）和 `ApplyVertexDiff`（`DeepCopy NiSkinPartition`、创建 `SHAPEDATA`、D3D 上传）。

**`OnAttach` 不是主线程信号。** NPC 批量加载（cell 加载、NPC 进入视野）时，引擎在工作线程上派发 `AttachBipedObject`，`OnAttach` 随之在工作线程执行。

**日志实锤**（v3.0.4 Debug，2026-10-30）：

```

[12:59:39.958] [3296 ] [I] SKSEPlugin_Load: loaded ← 主线程 3296
[12:59:45.489] [34160] [D] OnMessage: DataLoaded ← 工作线程 34160（启动期）
[13:00:02.347] [23620] [D] [Attach] refr=0009B0DB armor=000C02FB ... ← 工作线程 23620（NPC 批量加载）
[13:00:02.347] [23620] [D] [SKEE] SetAbsolute actor=0009B0DB morph='NoHeel' value=1
[13:00:02.347] [23620] [D] [ApplyOne] refr=0009B0DB mesh='' isAttaching=false
[13:00:02.521] [3296 ] [D] [Attach] refr=00000014 armor=FE004B06 ... ← 主线程 3296（玩家 mesh）
[13:00:07.778] [3296 ] [D] [Equip] actor=''(00000014) armor=... ← 主线程 3296（玩家操作）

```

- **玩家操作**（`TESEquipEvent`、玩家 mesh 的 `OnAttach`）在 3296 主线程，与 v3.0.2 的 `TESEquipEvent` 栈行为一致。
- **NPC 批量加载的 `OnAttach`** 在 23620 工作线程，当前实现在该线程执行了 `SetAbsolute` 和 `ApplyVertexDiff`。

## 2. 根因

v3.0.3 引入 `AttachListener` 时，假设 `OnAttach` 在主线程（与 SKEE 自身的 `BodyMorphInterface::OnAttach` 相同）。SKEE 自己的 `OnAttach` 也在工作线程做 D3D 操作，但 SKEE 的 `MorphFileCache::ApplyMorphs` 里 `defer` 参数被覆盖为 `(*g_main)->threadId != GetCurrentThreadId()`，**只保护了 `NIOVTaskUpdateSkinPartition::Run`（D3D 上传）**，没有保护 `DeepCopy` 和 `SHAPEDATA` 创建。SKEE 长期稳定说明要么实际很少跨线程，要么竞争窗口极窄没触发。

v3.0.4 的 `AttachListener` 继承了同样的假设，且额外在工作线程修改了 SKEE 的 `actorMorphs`（`unordered_map`，非线程安全）。

**三个操作在工作线程的风险**：

| 操作                                         | 风险                                                                                                    | 严重性                                                                      |
| -------------------------------------------- | ------------------------------------------------------------------------------------------------------- | --------------------------------------------------------------------------- |
| `EvaluateRules` → `GetWornArmor`             | 读 actor 装备状态，可能与主线程的装备修改竞争                                                           | 中                                                                          |
| `SetAbsolute` → `ClearMorph` / `SetMorph`    | 修改 SKEE `actorMorphs`（`unordered_map`），主线程的 `UpdateModelWeight` / `ApplyBodyMorphs` 同时遍历它 | **高**                                                                      |
| `ApplyVertexDiff` → `DeepCopy` / `SHAPEDATA` | 修改 `NiSkinPartition` 和 `NiBinaryExtraData`，非线程安全                                               | **高**（D3D 上传被 SKEE 的 `defer` 保护，但 `DeepCopy` / `SHAPEDATA` 没有） |

## 3. 为什么 6000 用户没崩

v3.0.2 → v3.0.3 → v3.0.4 累积的用户量没有报告线程崩溃。日志给出了三个可能原因：

1. **NPC 批量加载串行在同一工作线程（23620）**。不是"多个工作线程并发写"，是"一个工作线程写 + 主线程读/写"。`unordered_map` 的 rehash 可能没撞上主线程读。
2. **多数 NPC mesh 的 TRI 没有目标 morph**（`NoHeel` 只在裤袜 TRI 里）。`ApplyVertexDiff` 内部 `HasMorphs=false` 时 `MorphFileCache::ApplyMorph` 的 `if (HasMorphs || existingMorphs)` 为 false，整块跳过，不碰 D3D。
3. **`SetAbsolute` 对 NPC 写的值大多是 0**（`NoHeel=1`、`Pantyhose=0`、`Pressed=0`），`ClearMorph` 后 `SetMorph` 被跳过（`a_value != 0.0F` 为假），实际只调了 `ClearMorph`。

**这是运气，不是设计对。** 竞争窗口存在，只是没触发崩溃。随着用户量增长、MOD 组合更复杂，触发的概率会上升。

## 4. 顺带发现：非人形 actor 也在被处理

日志里：

```

[13:00:02.357] [23620] [D] [Attach] refr=001066DF armor=000A919C object='chicken' isFP=false
[13:00:02.357] [23620] [D] morph 'NoHeel' triggeredBy=00000080 -> default-init
[13:00:02.357] [23620] [D] rule[1] value=1 equipped=false -> MATCH
[13:00:02.357] [23620] [D] [SKEE] SetAbsolute actor=001066DF morph='NoHeel' value=1
[13:00:02.357] [23620] [D] [ApplyOne] refr=001066DF mesh='chicken' isAttaching=false

```

`chicken`、`Crab2`、`Farm02FHat` 的 NPC 都在被处理。给鸡设 `NoHeel=1` 完全无意义，`ApplyVertexDiff` 对鸡的 mesh 也是浪费。

**性能浪费，不是正确性问题**（`HasMorphs=false` 提前返回）。但与线程问题叠加：工作线程上做的无用功越多，竞争窗口越长。

## 5. 修复方向

### 5.1 核心：非主线程延迟到主线程，执行时重新求值

```cpp
void AttachListener::OnAttach(...) {
    if (a_refr == nullptr || a_armor == nullptr || a_object == nullptr) return;
    auto* actor = a_refr->As<RE::Actor>();
    if (actor == nullptr) return;
    auto& skee = SKEEIntegration::GetSingleton();
    if (!skee.IsAvailable()) return;

    if (GetCurrentThreadId() != (*g_main)->threadId) {
        // 非主线程（NPC 批量加载）：延迟到主线程，执行时重新读当前状态
        const auto refrID = a_refr->GetFormID();
        const auto armorID = a_armor->GetFormID();
        SKSE::GetTaskInterface()->AddTask([refrID, armorID]() {
            auto* refr = RE::TESForm::LookupByID<RE::TESObjectREFR>(refrID);
            auto* armor = RE::TESForm::LookupByID<RE::TESObjectARMO>(armorID);
            if (!refr || !armor) return;
            auto* actor = refr->As<RE::Actor>();
            if (!actor) return;
            auto& skee = SKEEIntegration::GetSingleton();
            if (!skee.IsAvailable()) return;
            MorphRules::Engine::MorphRuleEngine::GetSingleton().EvaluateRules(actor, armor, true);
            skee.ApplyVertexDiffToWornMeshes(*actor);
        });
        return;
    }

    // 主线程：当前逻辑不变
    MorphRules::Engine::MorphRuleEngine::GetSingleton().EvaluateRules(actor, a_armor, true);
    skee.ApplyVertexDiff(*a_refr, *a_object, false);
}
```

**关键点**：

- **延迟分支不用 `a_object`**。Task 执行时它可能已失效（mesh detach 或 `partClone` 被替换）。用 `LookupByID` 重新拿 refr/armor，用 `ApplyVertexDiffToWornMeshes` 对 actor 当前所有已 attach mesh 重应用。
- **延迟分支在 SKSE Task 里执行，不在 `AttachBipedObject` hook 栈里**。IED 冲突风险与 v3.0.3 不同（v3.0.3 的问题是在 hook 栈里对其他 mesh 做 D3D，Task 是下一 tick 的主线程）。
- **主线程分支零开销**。玩家操作、标准 `TESEquipEvent` 路径完全不变，与 v3.0.2 的安全模型一致。
- **延迟执行 + 重新求值不违反无状态**。执行时重新读 `GetWornArmor`、重新求值，不依赖 `OnAttach` 时的快照。

### 5.2 可选：非人形 actor 过滤

在 `OnAttach` 开头加一层过滤，跳过非人形 actor：

```cpp
// 只处理人形 actor。鸡、螃蟹、马等动物没有 morph target，
// 处理它们纯属浪费。
if (!actor->IsPlayerRef() && !actor->IsHumanoid()) {
    return;
}
```

`IsHumanoid()` 的判据需要验证（CommonLibSSE 是否有这个方法，或需要检查 race 的 keyword）。如果不可靠，可以退化为检查 `actor->GetRace()->HasKeyword("ActorTypeNPC")` 或类似。

**优先级低**。不是正确性问题，但与线程修复叠加能减少工作线程上的无用功。

### 5.3 顺带清理（不阻塞）

- `SKEEIntegration.h` 的 `HasShapeDataRecursive` 死声明（实现已在 `6da43be` 删除）。
- `ApplyVertexDiffToWornMeshes` 注释里的 `a_oldArmors`（v3.0.4 WIP 失败尝试的遗留）。
- `AttachListener::OnAttach` 里补注释，说明"只对当前 mesh 调 `ApplyVertexDiff`，不对其他 mesh"（v3.0.3 教训）。

## 6. 设计反思：过度坚持"不跨帧"

这次调查印证了 v3.0.4 设计讨论中的一个问题：**"无状态"和"不跨帧"被混为一谈**。

| 类型                | 例子                                           | 是否该防                                         |
| ------------------- | ---------------------------------------------- | ------------------------------------------------ |
| 缓存跨帧求值结果    | `m_lastValue` 记录上帧 morph 值，值没变就跳过  | **该防**，状态漂移的种子                         |
| 延迟执行 + 重新求值 | 下一帧 Task 里重新 `GetWornArmor` 求值 + Apply | **不该防**，执行时读的是当前状态，不依赖缓存结论 |

v3.0.2 移除 `m_lastValue` 跳变检测是对的（第 1 类）。但把"延迟执行"也归入"状态"一起排除，是过度泛化——v3.0.3 的防抖队列被回滚，真正原因是"延迟一帧不能保证 mesh 就绪"（`OnAttach` 是更精确的信号），不是"不能跨帧"。

**这次线程问题让结论更清晰**：如果 `OnAttach` 跨线程，延迟到主线程是**正确性要求**，不是可选优化。"不跨帧"的执念在这里会阻止正确修复。

## 7. 验证清单

实现后验证：

| 场景                  | 期望                                                                                                                             |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------------- |
| 进 cell，NPC 批量加载 | 日志里 `[Attach] thread=23620 main=3296` 后出现延迟分支的 `[Task]` 日志；主线程 `ApplyVertexDiffToWornMeshes` 对 NPC mesh 重应用 |
| NPC 穿裤袜 + 高跟     | morph 正确，与 v3.0.4 视觉一致                                                                                                   |
| 玩家穿脱高跟          | 主线程分支，行为不变，`[Equip]` + `[ApplyOther]` 与 v3.0.4 一致                                                                  |
| Modex 三件同穿        | 主线程（玩家操作），行为不变                                                                                                     |
| 鸡、螃蟹等动物加载    | 如果加了 5.2 过滤，不再出现 `[Attach] refr=... object='chicken'`                                                                 |

**日志验证点**：

- `[Attach] thread={} main={}` 能区分主线程/工作线程分支。
- 工作线程分支出现 `[Task] deferred ...` 标记。
- 主线程 Task 执行时 `[ApplyOther]` 对 NPC mesh 出现。
- 不再有工作线程上的 `[SKEE] SetAbsolute` 和 `[ApplyOne]`。

## 8. 变更记录

### v3.0.5（待实现）

**修复**：

- `AttachListener::OnAttach` 增加线程检查，非主线程分支延迟到 SKSE Task，执行时重新求值 + `ApplyVertexDiffToWornMeshes`。
- （可选）非人形 actor 过滤。
- （可选）清理 `HasShapeDataRecursive` 死声明、`ApplyVertexDiffToWornMeshes` 过时注释、`AttachListener::OnAttach` 意图注释。

**根因**：`OnAttach` 在 NPC 批量加载时跨线程（日志实锤线程 23620），当前实现在工作线程执行 `SetAbsolute`（改 SKEE `actorMorphs`）和 `ApplyVertexDiff`（`DeepCopy` / `SHAPEDATA`），存在数据竞争。6000 用户没崩是运气（串行工作线程 + 多数 mesh 提前返回），不是设计对。

**文档**：`Architecture.md` §4.2 路径 3、§11 已知限制同步更新。

---

**报告结束。**
