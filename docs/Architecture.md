# AdeptivePantyhose 架构

**版本**：v3.0.4
**最后更新**：2026-10-30
**状态**：当前架构文档，取代 `Architecture.md`（v3.0.2 快照，已冻结）

本文档描述 AdeptivePantyhose 的当前架构与设计决策。目标读者为维护者与想要扩展规则配置的高级用户。v3.0.2 → v3.0.4 的演进细节见 `docs/v3.0.4-ssot-refactor.md`，WIP 期间的踩坑见 `docs/v3.0.4-pitfalls-and-regression-guards.md`。

---

## 1. 目标与范围

AdeptivePantyhose 是一个 SKSE 插件，根据角色当前穿戴的装备动态调整 BodyMorph，从而在不同鞋型（高跟鞋 / 平底鞋 / 赤脚）与裤袜组合下消除脚部与衣物之间的接缝。

**核心设计原则**：

1. **数据驱动**：装备识别与 morph 动作全部由外部 JSON 定义，代码只提供机制。
2. **无状态求值**：每次事件独立求值，不维护跨帧状态，避免存档兼容性与状态漂移问题。
3. **分层解耦**：配置解析与游戏领域知识严格分离，语法层可脱离 Skyrim 环境单测。
4. **Fail fast**：任何配置错误在加载期暴露并终止，不做部分恢复。
5. **减法优先**：新增路径优先在同一栈内用同一操作表达，通过门禁排除不适用对象，而不是引入新操作或新栈。v3.0.4 相对 v3.0.2 的演进遵循这条原则。

---

## 2. 分层架构

```
┌─────────────────────────────────────────────────────────┐
│ assets/SKSE/AdeptivePantyhose/MorphRules.json           │ 数据
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Config 层（语法层）                                      │ 纯 C++23 + nlohmann::json
│ - Types.h        ParsedConfig 等纯数据类型              │ 零游戏依赖
│ - SyntaxParser   解析 JSON 结构，验证引用完整性         │
└────────────────────────┬────────────────────────────────┘
                         │ ParsedConfig
                         ▼
┌─────────────────────────────────────────────────────────┐
│ MorphRuleEngine 层（领域层）                             │ 引入游戏概念
│ - Predicate 抽象基类 + 内置组合子                       │ RE::Actor / RE::TESObjectARMO
│ - Leaves/ KeywordPredicate 等叶子工厂                   │ FormID / Keyword
│ - PredicateCompiler 编译 + 环检测                       │
│ - MorphRuleEngine 求值 + apply                          │
└────────────────────────┬────────────────────────────────┘
                         │ CompiledRules
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Event / Attach 层（触发）                                │
│ - EventProcessor      监听 TESEquipEvent                │
│ - AttachListener      监听 IAddonAttachmentInterface    │
│ - main.cpp            kPostLoadGame 对玩家兜底          │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│ Integration 层（外部接口）                               │
│ - SKEEIntegration 请求 SKEE BodyMorph 接口              │
└─────────────────────────────────────────────────────────┘

横切：src/Support/FormName.h 提供 SafeName()，供各层日志安全打印
匿名的 RE::TESForm（GetName() 可能返回 nullptr）。
```

**层次约束**：

- **Config 层不依赖任何游戏头文件**。可直接拷贝到独立工程编译测试。
- **MorphRuleEngine 层依赖 `RE::` 但通过 `PredicateContext` 值语义接受输入**，不感知事件来源。
- **Event / Attach 层只负责把游戏信号翻译成引擎的调用**，不含判定逻辑。
- **Integration 层封装 SKEE 的接口交换协议**，对外暴露类型化的接口指针。

---

## 3. 配置格式

### 3.1 顶层结构

```json
{
  "$schema": "./MorphRules.schema.json",
  "Predicates": { ... },
  "Morphs": { ... }
}
```

- `Predicates`：命名谓词表，键为谓词名，值为谓词表达式。
- `Morphs`：morph 规则表，键为 SKEE BodyMorph 系统中的 morph 名。
- `$schema`：仅供编辑器校验，运行时忽略。

### 3.2 谓词表达式

四种形态：

| 形态  | 语法                      | 语义                   |
| ----- | ------------------------- | ---------------------- |
| 叶子  | `{ "ByKeywords": [...] }` | 调用注册的叶子工厂     |
| AnyOf | `{ "AnyOf": ["A", "B"] }` | 任一子谓词为真（短路） |
| AllOf | `{ "AllOf": ["A", "B"] }` | 所有子谓词为真（短路） |
| Not   | `{ "Not": "A" }`          | 子谓词取反             |

**叶子类型白名单**由 `PredicateCompiler` 的注册表决定。目前支持 `ByKeywords`（关键字匹配）与 `ByFormIDRange`（来源插件 + local FormID 范围）。

**内置保留谓词**：`Default` 恒为真，用于规则列表的兜底分支。`Predicates` 段中不允许定义同名谓词。

**slot 过滤不在谓词层**：`TriggeredBy` 是 morph 级元数据，armor 是否占据某槽由引擎在进入谓词求值前判断。因此没有 `HasBipedSlot` 之类的叶子谓词——它是冗余的。

### 3.3 Morph 规则

```json
"Morphs": {
  "NoHeel": {
    "TriggeredBy": "kFeet",
    "Rules": [
      { "Value": 0.0, "When": "HighHeel" },
      { "Value": 1.0, "When": "Default" }
    ]
  }
}
```

- **`TriggeredBy`**：该 morph 关心的 biped 部位槽。注意这是"morph 关心哪个槽"，不是"事件 armor 必须占该槽"——见 §5.2。
- **`Rules`**：有序规则列表。**第一个命中的 `When` 生效**（短路）。
- **`Value`**：morph 的**目标绝对值**。`0.0` 表示清除。缺省为 `1.0`。

### 3.4 完整示例

```json
{
  "$schema": "./MorphRules.schema.json",
  "Predicates": {
    "KillerHeels": { "ByKeywords": ["SLA_KillerHeels"] },
    "CosplayPackHeels": {
      "ByFormIDRange": [
        {
          "Plugin": "[Caenarvon] Cosplay Pack.esp",
          "Min": "0xDDF",
          "Max": "0xDDF"
        }
      ]
    },
    "HighHeel": { "AnyOf": ["KillerHeels", "CosplayPackHeels"] }
  },
  "Morphs": {
    "NoHeel": {
      "TriggeredBy": "kFeet",
      "Rules": [
        { "Value": 0.0, "When": "HighHeel" },
        { "Value": 1.0, "When": "Default" }
      ]
    }
  }
}
```

---

## 4. 数据流

### 4.1 启动阶段

```
main.cpp: LoadMorphRules()
    │
    ├──► Syntax::ParseFile(path) → ParsedConfig（失败 → REX::FAIL）
    ├──► DumpParsedConfig(ParsedConfig)        [详细日志版]
    └──► PredicateCompiler::Compile(ParsedConfig) → CompiledRules（失败 → REX::FAIL）
            │
            ▼
MorphRuleEngine::SetRules(CompiledRules)
```

`SKEEIntegration::Initialize()` 在 `kPostPostLoad` 调用，向 SKEE 请求 `BodyMorph` 接口并注册 `AttachListener`。SKEE 未安装或版本过旧时打 `REX::WARN`，所有 morph 操作静默 no-op。

**Fail fast**：配置错误在加载期以 `REX::FAIL` 终止（日志 + 弹窗），不做部分恢复。

### 4.2 运行时：三条调用路径

插件在运行时响应**三个**触发源。三者都调用 `EvaluateRules`（幂等），mesh 更新范围不同：

|  #  | 触发源                                | 入口                             | Set                                 | Apply 范围                                                                                                             | 覆盖                       |
| :-: | ------------------------------------- | -------------------------------- | ----------------------------------- | ---------------------------------------------------------------------------------------------------------------------- | -------------------------- |
|  1  | `TESEquipEvent`                       | `MorphRuleEngine::OnEquipEvent`  | `EvaluateRules`                     | `ApplyVertexDiffToWornMeshes`：遍历 `bipedData`，对 `partClone->parent != nullptr` 的 mesh 调 `ApplyVertexDiff(false)` | 穿 + 脱                    |
|  2  | `kPostLoadGame` 消息                  | `MorphRuleEngine::OnActorLoaded` | 对每个 morph 读 `GetWornArmor` 求值 | `UpdateModelWeight`（全量，含 attachment holder）                                                                      | 读档                       |
|  3  | `IAddonAttachmentInterface::OnAttach` | `AttachListener::OnAttach`       | `EvaluateRules(actor, armor, true)` | `ApplyVertexDiff(当前 mesh, false)`：仅对刚 attach 的 mesh                                                             | 仅穿（mesh attach 才触发） |

**路径 1：标准装备/卸下事件**

```
TESEquipEvent → OnEquipEvent:
    - EvaluateRules：对每条 morph 按 §5.2 合成状态、按 §5.3 首条命中求值、SetAbsolute
    - ApplyVertexDiffToWornMeshes：对旧 mesh（parent != nullptr）以 isAttaching=false 重应用
```

正在 attach 的新 mesh 此时 `parent == nullptr`，被门禁排除。它的更新由路径 3 完成。

**路径 2：玩家读档**

`kPostLoadGame` 在存档完全恢复后触发，此时玩家 mesh 已 attach。对每个 morph 直接 `GetWornArmor(triggeredBy)` 取当前真实状态求值，然后 `UpdateModelWeight` 全量重应用。

**为什么不用 `TESEquipEvent` 覆盖读档**：读档不是"装备状态变化"，而是状态被引擎从存档恢复。引擎是否为恢复出来的装备触发 equip 事件是未定义行为。`kPostLoadGame` 是确定性兜底。

**为什么 `UpdateModelWeight` 只在这里保留**：`UpdateModelWeight` 比手写遍历多处理 attachment holder（SKEE `AttachmentInterface` 挂的 `NiAttachments [NiOA]` 动态 mesh）。读档时 mesh 已就绪，`UpdateModelWeight` 安全，且能覆盖动态 mesh。路径 1 不能用它——`TESEquipEvent` 期间 `bipedData` 可能已包含未 attach 的新 mesh，`UpdateModelWeight` 会为它创建错误 `SHAPEDATA`（v3.0.2 的 bug）。路径 1 的 `ApplyVertexDiffToWornMeshes` 用 `parent` 门禁精确排除未就绪 mesh，是这个场景的正确操作。

**路径 3：mesh attach**

`OnAttach` 是每个 armor addon mesh 挂到场景图后 SKEE 的回调。它覆盖两件事：

1. **Modex 的 `Actor::AddWornItem` 不触发 `TESEquipEvent`**。穿装备路径完全依赖 `OnAttach` 求值。
2. **标准路径下当前 mesh 的更新**。SKEE 的 `BodyMorphInterface::OnAttach` 在 observer 顺序中先于我们（SKEE 在 `Load` 注册，我们在 `kPostPostLoad`，追加到末尾），用**旧 morph 值**应用过当前 mesh。`SetAbsolute` 写入新值后，标准路径下路径 1 已在 `TESEquipEvent` 时对旧 mesh 处理过，但**当前 mesh 在 Set 之后没有任何人会再调 Apply**——只有我们能补。

**对当前 mesh 调 `ApplyVertexDiff(false)`，对其他 mesh 不调**。这是 v3.0.3 的精确教训：

- **对其他 mesh 做顶点操作**是 v3.0.3 两个 bug 的共同来源——`HasShapeData` 检查根节点漏更新（Bug 1），在 `AttachBipedObject` 栈里对其他 mesh 做 D3D 与 IED 交错（Bug 2）。v3.0.4 删除。
- **对当前 mesh 做顶点操作**是 SKEE 自身一直在做的（`isAttaching=true`），v3.0.3 从未出过问题，v3.0.4 保留。

`isAttaching=false` 而非 `true`：`false` 是"重置到 `SHAPEDATA`（base）再应用"——幂等；`true` 是"在当前顶点上再应用"——double application。当前 mesh 刚 attach、顶点就绪，SKEE 已用 `true` 处理过，我们用 `false` 重应用，幂等。

**为什么不检查 `EvaluateRules` 返回值**：v3.0.4 是 v3.0.2 的减法——同一栈、同一操作，只排除物理上未就绪的 mesh。`ApplyVertexDiff(false)` 对已就绪 mesh 幂等（`HasMorphs=false` 时 SKEE 内部提前返回，零 D3D）。额外的 `if (anyApplied)` 判断是"加法"，与整体方向相反。无条件调用与 `OnEquipEvent` 里"遍历所有 `bipedData` mesh，只排除 `parent == nullptr`"是同一哲学。

### 4.3 穿脱对称性

| 场景     | Set        | Apply                             |   对称性   |
| -------- | ---------- | --------------------------------- | :--------: |
| 标准穿   | 路径 1 + 3 | 路径 1（旧 mesh）+ 3（当前 mesh） |     ✓      |
| 标准脱   | 路径 1     | 路径 1（全部 mesh）               |     ✓      |
| Modex 穿 | 路径 3     | 仅路径 3（当前 mesh）             | ✗ 已知缺口 |
| Modex 脱 | 路径 1     | 路径 1（全部 mesh）               |     ✓      |
| 读档     | 路径 2     | 路径 2（全量）                    |  独立覆盖  |

**标准路径对称**：穿 = 路径 1（旧）+ 路径 3（新），脱 = 路径 1（全部）。两条路径的 Apply 范围互补。

**唯一缺口在 Modex 穿**：`AddWornItem` 绕过 `TESEquipEvent`，只剩路径 3 处理当前 mesh，其他已 attach 的 mesh 不更新。这就是 §11.1 的"attach 顺序依赖"。

**脱时正在脱的 mesh 会被路径 1 碰一次**：`TESEquipEvent` 派发期间它 `parent` 还非 null，门禁不排除它，用 `Default` 重置一次后引擎 detach。多余但无害，不显式排除。

**为什么玩家读档不走 `TESEquipEvent`**：见 §4.2 路径 2。**为什么 NPC 加载不需要单独处理**：cell 加载时引擎会为每个 NPC 批量触发它们的装备 `TESEquipEvent`，这条路径已覆盖"NPC 加载后已穿高跟"。

---

## 5. 语义规范

### 5.1 equipped 门禁

**规则求值前先看 `equipped`**（求值时刻该槽是否有装备）：

```cpp
matched = equipped ? rule.when->Evaluate(ctx)      // 正常求值谓词
                   : rule.when->IsAlwaysTrue();    // 仅恒真谓词能命中
```

- `equipped == true`：正常求值，`HighHeel` 等有意义的条件可能命中。
- `equipped == false`：谓词一律不求值。只有 `Default` 分支可能命中。

**核心假设**：脱下鞋子 = 裸足。卸下高跟和卸下平底鞋的结果相同——都落到 `Default → NoHeel=1`。

`equipped == false` 的触发来源不止"刚卸下鞋子"，还包括"任何事件发生时该槽本来就空"（首次穿裤袜、脚上没鞋）。后者语义是"确保 morph 处于裸足对应的状态"，与"卸下鞋子"一致。这是 v3.0.2 修复的核心（§7.2）。

### 5.2 slot 过滤与状态合成

每个事件到达时，对每条 morph 做四选一判断：

| `slotMatch` | 事件后该槽有装备 | 求值输入           | 场景                                          |
| :---------: | :--------------: | ------------------ | --------------------------------------------- |
|    true     |       true       | `(armor, true)`    | 穿上/换上新装备                               |
|    true     |      false       | `(nullptr, false)` | 卸下该槽装备                                  |
|    false    |      false       | `(nullptr, false)` | 事件与该槽无关，且该槽为空 → 初始化 `Default` |
|    false    |       true       | 跳过               | 事件与该槽无关，该槽已有装备，morph 值不变    |

```cpp
const bool slotMatch = a_armor != nullptr && a_armor->HasPartOf(morph.triggeredBy);

RE::TESObjectARMO* wornArmor = nullptr;
if (slotMatch) {
    wornArmor = a_equipped ? a_armor : nullptr;   // 本事件改变了该槽，用事件合成
} else {
    wornArmor = a_actor->GetWornArmor(morph.triggeredBy);   // 该槽未被触及，读当前值
}
const bool equipped = wornArmor != nullptr;

if (!slotMatch && equipped) {
    continue;   // 唯一跳过分支
}
```

**第 3 行（`!slotMatch && !equipped`）是 v3.0.2 修复的关键路径**。裤袜占 `kCalves`，`NoHeel` 的 `TriggeredBy` 是 `kFeet`。首次给 NPC 穿裤袜时脚上是空的，`slotMatch == false`、`GetWornArmor(kFeet) == nullptr`，走求值命中 `Default → NoHeel=1`，morph 初始化。修复前在这里 `continue`，`NoHeel` 从未写入。

**`!slotMatch` 时读 `GetWornArmor` 为什么安全**：该槽没有被当前事件触及，`GetWornArmor` 返回的"旧值"就是该槽的真实当前值。`TESEquipEvent` 派发期间 API 返回旧值的行为，只在"事件正要改变的那个槽"上才造成问题。

**`!slotMatch && equipped` 跳过的含义**：事件 armor 不占这个槽，槽里已有装备——morph 关心的状态没有被本事件改变。跳过纯属性能优化，不跳过也正确（只是多一次 `SetAbsolute` + `ApplyVertexDiff`）。

**这解决了"穿头盔触发 morph 更新"**：穿头盔时脚上有高跟鞋，`!slotMatch && equipped` 成立，跳过。脚上没鞋时求值并幂等写入 `NoHeel=1`，无害。

### 5.3 首条命中胜出

`Rules` 是有序列表，第一个命中的规则生效，其余不求值：

```cpp
const auto iter = std::ranges::find_if(morph.rules, [&](const CompiledMorphEntry& a_rule) {
    return equipped ? a_rule.when->Evaluate(ctx) : a_rule.when->IsAlwaysTrue();
});
if (iter == morph.rules.end()) {
    return false;   // 全部不命中，不触碰该 morph
}
SetAbsolute(a_actor, morphName, iter->value);
```

用 `find_if` 而非 `any_of`：谓词求值本身无副作用，但命中后需要 `SetAbsolute` 写入——"找到后动手"的语义，不是"判断存在性"。

### 5.4 Value = 0.0 表示清除

SKEE 的 `SetMorph` 是**相对变化量**。要得到绝对值 `v`，必须 `ClearMorph` 之后再 `SetMorph`：

```cpp
m_bodyMorph->ClearMorph(a_actor, morphNameCStr, kNamespaceKey);
if (a_value != 0.0F) {
    m_bodyMorph->SetMorph(a_actor, morphNameCStr, kNamespaceKey, a_value);
}
```

`Value == 0.0` 时只 `ClearMorph`，跳过 `SetMorph`——效果等价且少一次引擎调用。JSON 里用户看到的是绝对值语义。

### 5.5 SKEE 命名空间 key

所有 morph 操作使用统一 key `AdeptivePantyhoseMorphKey`（常量名 `kNamespaceKey`），隔离本插件设置的 morph，避免与其他插件冲突。

每次 `SetAbsolute` 时顺便 `ClearMorph` 旧 key `CH_AdeptivePantyhoseMorphKey`（`kLegacyNamespaceKey`），兼容从旧版本升级的存档。一次性兼容代码。

### 5.6 mesh 就绪的判据

**`partClone->parent != nullptr` 是唯一的"mesh 就绪"信号**。它表示 mesh 已挂到场景图、顶点就绪，与 morph 状态正交。

**不能用 `HasShapeData`**：`SHAPEDATA` 是挂在子 geometry（`BSGeometry`）上的 `NiBinaryExtraData`，检查根节点永远 false。且 `SHAPEDATA` 是否存在依赖 SKEE 的 `HasMorphs`（"任一 morph 值非 0"）——`NoHeel=0` 时 `HasMorphs=false`，SKEE 不创建 `SHAPEDATA`，但 mesh 已就绪。用 `HasShapeData` 作门禁会误杀（v3.0.4 WIP 踩坑，见 pitfalls §8）。

**不能用 RTTI 精确匹配**：`GetRTTI() == NiNode::Ni_RTTI` 只匹配精确类型 `NiNode`，漏掉 `BSFadeNode` 等子类。类型判断用 `skyrim_cast` 走继承链。

---

## 6. 分层细节

### 6.1 Config 层（语法层）

**职责**：把 JSON 文件解析成纯数据的 `ParsedConfig`，不做任何游戏解释。

输出 `ParsedPredicate`（`kind` + `leafType` + `leafPayload` 原始 JSON + `refs` / `ref`）、`ParsedMorph`（`triggeredBy` 字符串 + 有序 `rules`）、`ParsedConfig`（两个 `unordered_map`）。

**错误处理**：所有错误以 `std::expected<ParsedConfig, ParseError>` 返回。`ParseError` 携带精确 JSON 路径（如 `Predicates.WearingHighHeels.AnyOf[1]`）和人类可读消息。

**关键设计**：**叶子载荷不解释**。`{ "ByKeywords": ["SLA_KillerHeels"] }` 只记录 `leafType = "ByKeywords"`、`leafPayload = ["SLA_KillerHeels"]`，匹配规则是领域层的事。

### 6.2 MorphRuleEngine 层（领域层）

**职责**：把 `ParsedConfig` 编译成可执行的 `CompiledRules`，并提供运行时求值。

**编译期**：

- `PredicateCompiler` 通过 `LeafFactory` 注册表分派叶子类型，递归编译每个谓词，`m_compiled` 缓存避免重复，`m_inProgress` 检测引用环。
- `TriggeredBy` 经手写映射表 `kSlotMap` 转为 `BipedObjectSlot`。位标志枚举不适合 magic_enum，映射表用 `constexpr std::to_array<std::pair<std::string_view, Slot>>`，32 项线性扫描，仅启动期调用一次。列表与 schema 的 `BipedSlotName` enum 一一对应。

**编译产物**：

```cpp
struct CompiledMorphEntry { float value; PredicatePtr when; };
struct CompiledMorph {
    RE::BGSBipedObjectForm::BipedObjectSlot triggeredBy;
    std::vector<CompiledMorphEntry> rules;
};
struct CompiledRules { std::unordered_map<std::string, CompiledMorph> morphs; };
```

**运行时**：`MorphRuleEngine` 是**无状态**的。没有 `m_lastValue` 之类的跨事件缓存。`OnEquipEvent` 内部调 `GetWornArmor` 读取 actor 当前装备，但这是"本次事件求值时的输入读取"，不是跨帧状态——下次事件重新读，不依赖上次结论。这消除了状态漂移、跨存档污染、跳变检测的边界问题。

**`OnEquipEvent` 的职责**（§4.2 路径 1）：

```cpp
void MorphRuleEngine::OnEquipEvent(RE::Actor* a_actor, RE::TESObjectARMO* a_armor, bool a_equipped) {
    if (EvaluateRules(a_actor, a_armor, a_equipped)) {
        SKEEIntegration::GetSingleton().ApplyVertexDiffToWornMeshes(*a_actor);
    }
}
```

`EvaluateRules` 只求值 + `SetAbsolute`，不碰 mesh。`ApplyVertexDiffToWornMeshes` 在 `TESEquipEvent` 栈里对旧 mesh 重应用。

**`OnActorLoaded` 的职责**（§4.2 路径 2）：对每个 morph 用 `GetWornArmor` 求值，然后 `UpdateModelWeight` 全量重应用。

### 6.3 Event / Attach 层

**`EventProcessor`**：监听 `TESEquipEvent`。入口过滤 `actor` 非空且是 `RE::Actor`、`baseObject` 是 `RE::TESObjectARMO`，然后调 `MorphRuleEngine::OnEquipEvent`。

**`AttachListener`**：实现 `IAddonAttachmentInterface`，在 `kPostPostLoad` 通过 `IActorUpdateManager::AddInterface` 注册（追加到 SKEE 自己的观察者之后）。`OnAttach` 收到每个 armor addon mesh 的 attach 回调：

```cpp
void AttachListener::OnAttach(...) {
    if (a_refr == nullptr || a_armor == nullptr || a_object == nullptr) return;
    auto* actor = a_refr->As<RE::Actor>();
    if (actor == nullptr) return;
    auto& skee = SKEEIntegration::GetSingleton();
    if (!skee.IsAvailable()) return;

    // SSOT 求值入口：Modex 的 AddWornItem 不触发 TESEquipEvent，
    // OnAttach 是唯一无法被绕过的信号。求值是纯数据操作。
    MorphRules::Engine::MorphRuleEngine::GetSingleton().EvaluateRules(actor, a_armor, /*a_equipped=*/true);

    // 只对当前 mesh 调 ApplyVertexDiff：时序上 Set 之后没有其他路径会 Apply 它
    // （Modex 路径没有 TESEquipEvent；标准路径的 OnEquipEvent 在事件时已处理完旧 mesh）。
    // 对其他 mesh 不调：v3.0.3 教训。
    skee.ApplyVertexDiff(*a_refr, *a_object, /*a_isAttaching=*/false);
}
```

**不依赖 `TESEquipEvent` 作为 SSOT 的原因**：Modex 等"同时装备套装"类 MOD 通过 `Actor::AddWornItem` 底层 API 修改装备，绕过 `ActorEquipManager` 的事件派发路径。`TESEquipEvent` 不触发，但 mesh 会 attach。`IAddonAttachmentInterface::OnAttach` 是唯一无法被绕过的信号。

**`main.cpp` 的 `kPostLoadGame`**：对玩家读档兜底。

### 6.4 Integration 层

**`SKEEIntegration`**：

- `Initialize()` 在 `kPostPostLoad` 调用，向 SKEE 请求 `BodyMorph` 接口和 `IActorUpdateManager`，注册 `AttachListener`。
- 持有类型化的 `SKEE::IBodyMorphInterface*`。
- 对外提供语义化方法：`SetAbsolute`（屏蔽 SKEE 的 key 概念和相对语义）、`ApplyVertexDiffToWornMeshes`（路径 1 的全量重应用，`parent` 门禁）、`ApplyVertexDiff`（单 mesh，供 `AttachListener` 使用）、`UpdateModelWeight`（读档兜底）。
- SKEE 未安装或接口版本过旧时打 `REX::WARN`，所有操作静默 no-op。

`ApplyVertexDiffToWornMeshes` 遍历 `GetBiped(false/true)` 的 `objects` / `bufferedObjects`，对每个 `partClone->parent != nullptr` 的 mesh 调 `ApplyVertexDiff(false)`：

```cpp
for (const bool isFirstPerson : {false, true}) {
    const auto& weightModel = a_actor.GetBiped(isFirstPerson);
    if (!weightModel) continue;
    auto processArray = [&](auto& a_array) {
        for (std::uint32_t i = 0; i < RE::BIPED_OBJECTS::kTotal; ++i) {
            auto& data = a_array[i];
            if (!data.partClone) continue;
            if (data.partClone->parent == nullptr) continue;   // 唯一门禁
            ApplyVertexDiff(a_actor, *data.partClone, /*a_isAttaching=*/false);
        }
    };
    processArray(weightModel->objects);
    processArray(weightModel->bufferedObjects);
}
```

**与 `UpdateModelWeight` 的精确等价**：对 `bipedData` 里的常规 armor addon，主线程下 `UpdateModelWeight` 精确等于"对每个 biped node 调 `ApplyVertexDiff(false)`"。两者最终都走 `MorphFileCache::ApplyMorphs`，路径相同。`UpdateModelWeight` 多的只有 attachment holder 和 `Shrink()`。

---

## 7. 关键决策记录

### 7.1 为什么 Config 层不依赖游戏头文件

旧版把 JSON 解析与 `RE::FormID` 类型转换混在一起，改一个 formID 都需要进游戏验证。Config 层只认识 `nlohmann::json`，叶子载荷原样保留，解释权交给领域层。收益：可脱离 Skyrim 环境单测，加新叶子类型无需改动 Config 层。

### 7.2 为什么移除跨帧跳变检测

早期设计用 `m_lastValue` 记录每个 `(actor, morph)` 上帧状态，只有"值变化"时才 `SetMorph`。但加载存档、切换角色、actor 禁用后启用都会让缓存失效；玩家希望"卸鞋 → 光脚"立即 morph，而不是因缓存误判跳过。改为无状态求值。代价：每次事件都调一次 SKEE 接口（即使值没变），实测不是瓶颈。

### 7.3 为什么 `TriggeredBy` 是 morph 级元数据

如果 slot 判定作为谓词放在 `When` 里，用户可能忘记写，导致穿头盔也触发 morph。提升为 morph 级元数据后，用户不可能忘记，引擎的 slot 逻辑只有一处。v3.0.2 起 `TriggeredBy` 的语义是"该 morph 关心哪个槽、以及事件后该槽的状态如何合成"（§5.2），不是"事件 armor 必须占该槽"——morph 是 actor 级状态，由 actor 在 `TriggeredBy` 槽上的真实装备决定，不由恰好触发事件的那件 armor 决定。

### 7.4 为什么遍历规则集而非遍历装备

读档兜底需要检查玩家所有已装备 armor。遍历规则集（每个 morph 确定它关心的那一件）而非遍历所有 `BipedObjectSlot`：遍历次数 = morph 数量（通常 1~5），与装备数量无关；天然无重复；与 `OnEquipEvent` 结构一致。

### 7.5 为什么使用 `REX::FAIL` 而非 `return false`

配置错误在 `SKSEPlugin_Load` 里返回 false 只会在日志留一行，用户通常在游戏跑一半才发现插件没生效。`REX::FAIL` 输出日志 + 弹窗 + 终止进程，用户启动时立即看到精确 JSON 路径和原因。

### 7.6 为什么放弃 `TESObjectLoadedEvent`

实测证据：NPC 加载时引擎已触发全部装备的 `TESEquipEvent`（Whiterun Guard、Hilde 等在 cell 加载后 200ms 内集中触发），早于它们的 `TESObjectLoadedEvent` 数秒——"NPC 加载后已穿高跟"已被 equip 事件覆盖。`TESObjectLoadedEvent` 每秒数百条，绝大多数是容器/门/杂物，`GetName()` 为空。且需判断 `Is3DLoaded()` 才能安全调 `GetWornArmor`，引入额外时机依赖。

### 7.7 为什么 `OnEquipEvent` 要合成"事件后槽位状态"

`TESEquipEvent` 派发期间 `GetWornArmor` 返回**事件前**状态。穿鞋事件里它仍返回 `nullptr`（或旧的那双），脱鞋事件里它仍指向正要卸下的鞋。直接用它求值会把穿鞋求成 `Default`、脱鞋求成 `HighHeel`，行为反转。

按事件是否触及 `triggeredBy` 槽分两支合成（§5.2）：`slotMatch` 时事件参数是权威的（描述的就是这次变化），`!slotMatch` 时 `GetWornArmor` 是权威的（该槽没被动过）。两支各自有可靠信息源，不存在冲突。

合成逻辑放在各入口（`OnEquipEvent` 有 `slotMatch` 上下文，`OnActorLoaded` 没有——它不是装备事件，`GetWornArmor` 已是当前真实值），`EvaluateAndApply` 只接受自洽的 `(armor, equipped)`。

### 7.8 swap 场景的瞬态可接受

同槽从 A 换到 B。两个事件时先写一次（`A` 卸下走 `Default`）、再写一次（`B` 穿戴走求值），中间有瞬态但两次各自 `ApplyVertexDiff`，最终收敛。一个事件时直接用 `B` 求值，一次到位。不特殊处理——瞬态在两次 `ApplyVertexDiff` 之间，玩家不可见。为消除瞬态引入"延迟到下一帧"会破坏无状态求值的简洁性。

### 7.9 为什么 `OnAttach` 是 SSOT 求值入口

`TESEquipEvent` 不是装备变化的 SSOT——Modex 的 `Actor::AddWornItem` 路径不触发它。**唯一绕不过的信号是 `IAddonAttachmentInterface::OnAttach`（mesh attach）**。因此求值必须在 `OnAttach` 里完成，覆盖 Modex 路径。求值是纯数据操作（写 `actorMorphs`），不需要碰 mesh。

### 7.10 为什么 `OnAttach` 只对当前 mesh 做顶点操作

v3.0.3 的精确教训：**对其他 mesh 做顶点操作**是 Bug 1（`HasShapeData` 检查根节点漏更新）和 Bug 2（`AttachBipedObject` 栈里对其他 mesh 做 D3D 与 IED 交错）的共同来源。**对当前 mesh 做顶点操作**是 SKEE 自身一直在做的（`BodyMorphInterface::OnAttach` 以 `isAttaching=true` 调 `ApplyVertexDiff`），从未出过问题。

v3.0.4 删除"对其他 mesh 的操作"，保留"对当前 mesh 的操作"。这是 v3.0.3 → v3.0.4 的减法。

### 7.11 为什么 `OnAttach` 必须对当前 mesh 调 `ApplyVertexDiff`

**时序决定**：`OnAttach` 里 `SetAbsolute` 写入新值后，没有其他路径会对当前 mesh 调 Apply：

- Modex 路径：`AddWornItem` 不触发 `TESEquipEvent`，没有 `OnEquipEvent`，没有 `ApplyVertexDiffToWornMeshes`。当前 mesh 只有 `OnAttach` 能救。
- 标准路径：`OnEquipEvent` 在 `TESEquipEvent` 时已处理旧 mesh，当前新 mesh 此时 `parent == nullptr` 被门禁排除。SKEE 的 `BodyMorphInterface::OnAttach` 在 observer 顺序中先于我们，用**旧 morph 值**应用过当前 mesh。我们 Set 新值后，只有我们能补。

用 `isAttaching=false` 而非 `true`：`false` 重置到 `SHAPEDATA`（base）再应用，幂等；`true` 在当前顶点上再应用，double application。

### 7.12 为什么 `OnAttach` 不检查 `EvaluateRules` 返回值

v3.0.4 的减法哲学：同一栈、同一操作，只排除物理上未就绪的对象。`ApplyVertexDiff(false)` 对已就绪 mesh 幂等（`HasMorphs=false` 时 SKEE 内部提前返回，零 D3D）。额外的 `if (anyApplied)` 是"加法"，与整体方向相反。无条件调用与 `OnEquipEvent` 里"遍历所有 `bipedData` mesh，只排除 `parent == nullptr`"是同一哲学。

### 7.13 为什么 `parent != nullptr` 是唯一的 mesh 就绪门禁

见 §5.6。`HasShapeData` 依赖 `HasMorphs`（`NoHeel=0` 时误判），RTTI 精确匹配漏子类（`BSFadeNode`）。`parent` 表示"mesh 已挂到场景图 = 顶点就绪"，与 morph 状态正交。v3.0.3 和 v3.0.4 WIP 三次踩"body mesh 被错误跳过"的坑（`HasShapeData` 根节点、RTTI 精确匹配、`HasShapeData` 依赖 `HasMorphs`），都是门禁选错。

### 7.14 `NoHeel=0` 是 base shape，不是"没有应用"

`NoHeel` 的 morph target 语义：base = 高跟形状，应用 delta = 平脚形状。`0` 不应用 delta → base（高跟形状）；`1` 应用 delta → base + delta（平脚形状）。穿高跟时看不到变化是正常的——裤袜就是 base shape。只有 `0→1`/`1→0` 切换才能看到变化。

`NoHeel` 的唯一视觉载体是裤袜。裸足、鞋、toenails 的 TRI 里都没有 `NoHeel`。脚部变形由鞋的 `Heel` morph 控制（RaceMenu High Heel 系统），不是 `NoHeel`。

---

## 8. 扩展点

### 8.1 新增叶子谓词类型

在 `src/MorphRuleEngine/Leaves/` 下新建 `MakeXPredicate(const nlohmann::json&) -> std::expected<PredicatePtr, LeafError>`，在 `main.cpp` 的 `LoadMorphRules` 里 `compiler.RegisterLeaf("X", &Engine::MakeXPredicate)`，在 `MorphRules.schema.json` 的 `PredicateLeaf` 里补一条分支。无需修改 Config 层、`PredicateCompiler`、`MorphRuleEngine`。

**注意**：如果它表达"armor 属于哪一类"，应该是谓词；如果表达"哪个槽位触发"，应该是 `TriggeredBy` 的职责。

### 8.2 新增组合子

在 `Predicate.h` 增加基类实现，在 `PredicateCompiler::BuildFromParsed` 的 `switch` 里加分支。

### 8.3 新增 morph 级元数据

改 `ParsedMorph` / `CompiledMorph` 结构，两处解析器各加一行。

### 8.4 新增触发源

如果未来发现某类 MOD 既不用 `TESEquipEvent` 也不用 `IAddonAttachmentInterface`（例如纯脚本改 actor 的 morph 值），需要新增触发源。先评估是否能归入现有两条路径——v3.0.3 的教训是不要轻易引入新栈做 D3D。

---

## 9. 目录结构

```
src/
├── Config/
│   ├── Types.h                   语法层数据类型
│   └── SyntaxParser.{h,cpp}      JSON 解析
├── MorphRuleEngine/
│   ├── Predicate.h               抽象基类 + 内置组合子
│   ├── CompiledRules.h           编译产物
│   ├── CompileError.h            编译错误类型
│   ├── PredicateCompiler.{h,cpp} 编译器
│   ├── MorphRuleEngine.{h,cpp}   运行时求值
│   └── Leaves/
│       ├── KeywordPredicate.{h,cpp}
│       └── FormIDRangePredicate.{h,cpp}
├── Event/
│   └── EventProcessor.{h,cpp}    监听 TESEquipEvent
├── Integration/SKEE/
│   ├── AttachListener.{h,cpp}    IAddonAttachmentInterface 实现
│   └── SKEEIntegration.{h,cpp}   SKEE 接口封装
├── Support/
│   └── FormName.h                SafeName() 日志兜底
├── main.cpp                      装配、生命周期、kPostLoadGame 兜底
└── pch.h
```

---

## 10. 调试与日志

日志宏来自 `REX`（`commonlib-shared` 提供）。`REX::FAIL` = 日志 + 弹窗 + 终止进程；`REX::CRITICAL` 只写日志。插件初始化阶段的不可恢复错误用 `REX::FAIL`，运行时的严重异常（不影响继续运行）用 `CRITICAL`。

**DEBUG 级在正式版由编译期剔除**：release 配置默认 `SPDLOG_ACTIVE_LEVEL = SPDLOG_LEVEL_INFO`，所有 `REX::DEBUG` 调用在编译期消失——参数不求值，字符串不进二进制。详细日志版通过构建参数提到 `SPDLOG_LEVEL_TRACE`，同一份源码原样编出全量日志，不需要运行时开关或 `#ifdef`。

**日志文件**：`Documents/My Games/Skyrim Special Edition/SKSE/AdeptivePantyhose.log`。`CMakeLists.txt` 在 configure 阶段硬链接到项目根。

**关键日志点**：

- 启动期（INFO）：`Plugin version` / `SKSE version` / `Game version` / `MorphRules loaded: N morph(s)` / `SKEEIntegration: initialized (bodyMorphVersion=N)` / `AttachListener installed`。
- 运行期（DEBUG）：`[Equip]` 事件入口 + `morph '...' -> slot-match / default-init` + `rule[i] ... -> MATCH` + `[SKEE] SetAbsolute`；`[Attach]` + `EvaluateRules` + `[ApplyOne]`；`[ApplyOther]`（路径 1 的每个旧 mesh）；`[PostLoad]` + `UpdateModelWeight`。

**排查顺序**：日志里有没有 `[Equip]`（`TESEquipEvent` 是否触发）→ 有没有 `[Attach]`（`OnAttach` 是否触发）→ `rule[i] -> MATCH` 是否出现（morph 值是否写入）→ `[ApplyOne]` / `[ApplyOther]` 是否对目标 mesh 出现（mesh 是否被处理）→ `skee.log` 里该 mesh 的 TRI 是否含目标 morph target。

---

## 11. 已知限制

1. **Modex 穿的 attach 顺序依赖**：`OnAttach` 只处理当前 mesh。如果新装备影响已有 mesh 的 morph 值，已有 mesh 不更新。示例：裤袜先 attach、高跟后 attach 时，裤袜不响应高跟的 `NoHeel` 变化。标准 `TESEquipEvent` 路径没有此问题（第二个事件的 `OnEquipEvent` 会处理已 attach 的裤袜）。当前策略：接受限制，文档说明；如反馈集中，备选方案是延迟一帧重应用（引入瞬态）。
2. **单 slot 触发**：`TriggeredBy` 是单个槽，无法表达"kFeet 或 kCalves 任一触发"。当前无此需求。
3. **SKEE 接口版本依赖**：BodyMorph 接口要求 >= v4。SKEE 未安装或版本过旧时整个 morph 功能静默失效。
4. **配置文件的编辑体验**：依赖用户在 MO2 目录中手动编辑 JSON。无 GUI。
5. **NPC 加载依赖引擎的 equip 事件**：如果某 mod 通过脚本绕过装备系统直接修改 NPC 装备，且引擎不为其触发 `TESEquipEvent`，`OnAttach` 仍能覆盖 mesh attach——除非该 mod 同时绕过 mesh attach（当前无证据）。见 §7.6。
6. **morph 是 actor 级状态**：插件设置的是 actor 的 morph 值，SKEE 将其应用到该 actor 上所有带对应 morph target 的 mesh。裤袜是否视觉上贴合某双鞋，取决于 mesh 作者是否在 BodySlide 中构建了兼容形变。插件无法修复 mesh 层面不匹配。
7. **swap 瞬态**：同槽直接换装时若引擎派发两个事件，morph 值会经历一次中间态再收敛。见 §7.8。
8. **`TESEquipEvent` 期间 `GetWornArmor` 返回旧值**：引擎行为，不可绕过。任何需要"事件后状态"的逻辑必须用事件参数合成。见 §7.7。
9. **SE / AE 双版本**：分别产出独立 DLL（`_SE` / `_AE` 后缀），部署到不同 MO2 目录。装错版本的现象是"插件加载成功但事件回调不触发"。不要同时启用两个版本，否则日志互相覆盖。
10. **`AttachBipedObject` 栈里的 D3D 范围**：v3.0.4 只对当前 mesh 调一次 `ApplyVertexDiff(false)`，`HasMorphs=false` 时 SKEE 内部提前返回（零 D3D）。IED 冲突（v3.0.3 Bug 2）在 v3.0.4 已由用户确认解决。若未来 IED 更新 hook 位置，需重新评估。机制层面确认（HookMap）未做，行为层面已确认。

---

## 12. 历史文档

| 文档                                             | 版本            | 状态                                                       |
| ------------------------------------------------ | --------------- | ---------------------------------------------------------- |
| `archived/Architecture.md`                       | v3.0.2          | 已冻结，本文档取代                                         |
| `archived/skee-behavior-notes.md`                | v3.0.2 / v3.0.3 | 已冻结，`skee-behavior-notes.md` 取代                      |
| `archived/skee-behavior-notes-2.md`              | v3.0.4 修正版   | 已冻结，内容已合并到 `skee-behavior-notes.md`              |
| `archived/toenail-report-2026-10-27.md`          | v3.0.3 设计期   | 历史，根因推理链，结论已吸收到 `skee-behavior-notes.md` §4 |
| `archived/toenail-onattach-design-2026-10-27.md` | v3.0.3 设计期   | 历史，设计已被 v3.0.4 明确否定                             |
| `archived/v3.0.3-onattach-fix.md`                | v3.0.3          | 历史，v3.0.3 有回归，设计已被 v3.0.4 取代                  |
| `skee-behavior-notes.md`                         | v3.0.2 → v3.0.5 | 当前，合并版                                               |
| `onattach-d3d-regression-report-2026-10-29.md`   | v3.0.3 回归调查 | 当前证据链，§5 修复方向已过时，§3/§6 仍有效                |
| `v3.0.4-ssot-refactor.md`                        | v3.0.4          | 当前，演进细节                                             |
| `v3.0.4-pitfalls-and-regression-guards.md`       | v3.0.4          | 当前，踩坑与防回归                                         |
| `onattach-thread-affinity-report-2026-10-30.md`  | v3.0.5 调查     | 当前，OnAttach 跨线程                                      |
| **`Architecture.md`**                            | **v3.0.4**      | **当前主文档**                                             |
