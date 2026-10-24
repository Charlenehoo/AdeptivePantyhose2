# AdeptivePantyhose 架构

**版本**：v3.0.2
**最后更新**：2026-10-24

本文档描述 AdeptivePantyhose 的内部架构与设计决策。目标读者为维护者与想要扩展规则配置的高级用户。

---

## 1. 目标与范围

AdeptivePantyhose 是一个 SKSE 插件，根据角色当前穿戴的装备动态调整 BodyMorph，从而在不同鞋型（高跟鞋 / 平底鞋 / 赤脚）与裤袜组合下消除脚部与衣物之间的接缝。

**核心设计原则**：

1. **数据驱动**：装备识别与 morph 动作全部由外部 JSON 定义，代码只提供机制。
2. **无状态求值**：每次事件独立求值，不维护跨帧状态，避免存档兼容性与状态漂移问题。
3. **分层解耦**：配置解析与游戏领域知识严格分离，语法层可脱离 Skyrim 环境单测。
4. **Fail fast**：任何配置错误在加载期暴露并终止，不做部分恢复。

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
│ Event 层（触发）                                         │
│ - EventProcessor 监听 TESEquipEvent，喂给引擎           │
│ - main.cpp 消息 kPostLoadGame 对玩家兜底一次            │
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
- **Event 层只负责把游戏事件翻译成引擎的调用**，不含判定逻辑。
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

**叶子类型白名单**由 `PredicateCompiler` 的注册表决定。目前支持：

| 叶子名          | 载荷                      | 语义                                         |
| --------------- | ------------------------- | -------------------------------------------- |
| `ByKeywords`    | 字符串数组                | armor 拥有任意一个关键字                     |
| `ByFormIDRange` | `{Plugin, Min, Max}` 数组 | armor 来源插件与 local FormID 落在任一范围内 |

**内置保留谓词**：`Default` 恒为真，用于规则列表的兜底分支。`Predicates` 段中不允许定义同名谓词。

**slot 过滤不在谓词层**：`TriggeredBy` 是 morph 级元数据，armor 是否占据某槽由引擎在进入谓词求值前判断。因此**没有** `HasBipedSlot` 之类的叶子谓词——它是冗余的。

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

- **`TriggeredBy`**：该 morph 关心的 biped 部位槽。引擎按 §5.2 的规则决定事件是否触发对该 morph 的求值。
- **`Rules`**：有序规则列表。**第一个命中的 `When` 生效**（短路）。
- **`Value`**：morph 的**目标绝对值**。`0.0` 表示清除。缺省为 `1.0`。

### 3.4 一个完整示例

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
    ├──► Syntax::ParseFile(path)
    │       │
    │       ├── 读取文件
    │       ├── JSON 解析
    │       ├── 遍历 Predicates 段 → ParsedPredicate
    │       ├── 遍历 Morphs 段 → ParsedMorph
    │       └── ValidateReferences：检查所有引用名有定义
    │
    │   成功 → ParsedConfig
    │   失败 → ParseError{ location, message } → REX::FAIL
    │
    ├──► DumpParsedConfig(ParsedConfig)        [详细日志版]
    │
    └──► PredicateCompiler::Compile(ParsedConfig)
            │
            ├── 注册叶子工厂（main.cpp 提供）
            ├── 递归编译每个谓词，缓存 PredicatePtr
            ├── 用 m_inProgress 检测引用环
            ├── 解析每个 morph 的 TriggeredBy 为 BipedObjectSlot
            └── 编译每个 morph 的 Rules 为 PredicatePtr 列表
    │
    │   成功 → CompiledRules
    │   失败 → CompileError{ location, message } → REX::FAIL
    │
    ▼
MorphRuleEngine::SetRules(CompiledRules)
```

**Fail fast**：任何一步失败都在 `main.cpp` 里以 `REX::FAIL` 终止。`REX::FAIL` 会输出日志并在游戏启动前弹出错误框，用户立刻知晓，而不是等到进游戏后才发现插件静默失效。

### 4.2 运行时阶段

插件在运行时只响应**两个**触发源：

| 触发源               | 覆盖场景                                          |
| -------------------- | ------------------------------------------------- |
| `TESEquipEvent`      | 装备/卸下、NPC 加载时恢复装备、玩家新游戏初始装备 |
| `kPostLoadGame` 消息 | 玩家读档                                          |

**场景 A：装备/卸下事件（`TESEquipEvent`）**

```
EventProcessor::ProcessEvent(TESEquipEvent)
    │
    ├── 过滤：a_event != null
    ├── 过滤：actor 是 RE::Actor
    ├── 过滤：baseObject 是 RE::TESObjectARMO
    │
    ▼
MorphRuleEngine::OnEquipEvent(actor, armor, equipped)
    │
    ├── 对每条 morph：
    │     ├── slotMatch = armor->HasPartOf(morph.TriggeredBy)
    │     │
    │     ├── 合成"事件后该槽的真实状态"：
    │     │     ├── slotMatch  → wornArmor = equipped ? armor : nullptr
    │     │     └── !slotMatch → wornArmor = actor->GetWornArmor(TriggeredBy)
    │     │                       （该槽未被事件触及，旧值即当前值）
    │     │
    │     ├── 跳过条件：!slotMatch && wornArmor != nullptr
    │     │     （事件与本 morph 无关，且该槽已有装备，morph 值不需要变）
    │     │
    │     ├── 遍历 rules
    │     │     ├── matched = wornArmor != nullptr
    │     │     │              ? rule.when->Evaluate(ctx)
    │     │     │              : rule.when->IsAlwaysTrue()
    │     │     └── 首个 matched → SetAbsolute(morphName, rule.value)
    │
    └── 若有任何 morph 被 apply → UpdateModelWeight(actor)
```

**关键点：不能在回调里直接用 `GetWornArmor` 作为求值输入。** `TESEquipEvent` 派发期间该 API 返回的是**事件前**状态——穿鞋事件里它还是 `nullptr`，脱鞋事件里它还指向那双鞋。直接用它会把穿鞋求成 `Default`、脱鞋求成 `HighHeel`，行为完全反转。因此 `slotMatch` 分支必须用 `(armor, equipped)` 合成新状态；只有 `!slotMatch` 分支（该槽未被本事件触及）才能安全读 `GetWornArmor`。详见 §7.7。

**场景 B：玩家读档（`kPostLoadGame` 消息）**

```
main.cpp: OnMessage(kPostLoadGame)
    │
    ▼
MorphRuleEngine::OnActorLoaded(RE::PlayerCharacter::GetSingleton())
    │
    ├── 对每条 morph：
    │     ├── wornArmor = actor->GetWornArmor(morph.TriggeredBy)
    │     ├── equipped = (wornArmor != nullptr)
    │     └── 与 OnEquipEvent 相同的规则求值路径
    │
    └── 若有任何 morph 被 apply → UpdateModelWeight(actor)
```

`kPostLoadGame` 不是装备事件，是读档完成消息，此时装备状态已从存档完整恢复。`OnActorLoaded` 里的 `GetWornArmor` 拿到的是**当前真实状态**，可以直接用，不需要 §7.7 那套合成逻辑。

**两个入口共用 `EvaluateAndApply` 内部函数**——保证 equip 事件与读档事件的行为完全一致。该函数的契约是：传入的 `(armor, equipped)` 必须自洽地描述"求值时刻该槽的真实状态"。两个入口各自负责构造这个状态，`EvaluateAndApply` 不关心它是怎么来的。

**遍历方向**：引擎遍历**规则集**，而不是遍历 actor 的装备。每个 morph 自己知道关心哪个 slot，通过事件合成或 `GetWornArmor` 取那一件。遍历成本 = morph 数量（O(1~5)），与装备数量无关。

**为什么玩家的读档不走 `TESEquipEvent`**：读档不是"装备状态发生变化"，而是状态被引擎从存档恢复。引擎是否为恢复出来的每一件装备触发 equip 事件是未定义行为，不能依赖。`kPostLoadGame` 消息在存档完全恢复后触发，此时玩家的装备状态已就绪，主动跑一遍规则最可靠。

**为什么 NPC 加载不需要单独处理**：cell 加载时引擎会为每个 NPC 批量触发它们的装备 `TESEquipEvent`（日志里可见 Whiterun Guard、Hilde、Sven、Faendal 等 NPC 在 200ms 内集中触发全部装备的 equip 事件）。这条路径已经覆盖"NPC 加载后已穿高跟"的场景，无需额外挂钩。

---

## 5. 语义规范

### 5.1 equipped 门禁

**规则求值前先看 `equipped`**：

```cpp
matched = equipped ? rule.when->Evaluate(ctx)      // 正常求值谓词
                   : rule.when->IsAlwaysTrue();    // 仅恒真谓词能命中
```

这里的 `equipped` 是**求值时刻该槽是否有装备**，不是事件的 `a_equipped` 参数。两个入口的构造方式不同：

- `OnEquipEvent`：按 §4.2 / §5.2 的规则合成（`slotMatch` 时来自事件参数，`!slotMatch` 时来自 `GetWornArmor`）。
- `OnActorLoaded`：直接 `GetWornArmor(triggeredBy) != nullptr`。

- `equipped == true`：正常求值谓词，`HighHeel` 等有意义的条件都可能命中。
- `equipped == false`：谓词一律不求值。只有 `IsAlwaysTrue()` 为 true 的规则可能命中——这正是 `Default` 分支。有意义的谓词（`HighHeel` 之类）此时一律视为不命中。

**这是本插件的核心假设**：脱下鞋子 = 裸足。因此卸下高跟和卸下平底鞋的结果相同——都落到 `Default` 的 `Value: 1.0`，即 `SetAbsolute("NoHeel", 1.0)`。

`equipped == false` 的触发来源不止"刚卸下鞋子"，还包括"任何事件发生时该槽本来就空"（例如首次穿裤袜、脚上没穿鞋）。后者的语义是"确保 morph 处于裸足对应的状态"，与"卸下鞋子"完全一致。这是 v3.0.2 修复的核心——详见 §5.2 与 §7.7。

**为什么硬编码而不做成 JSON 可配置**：这是插件对游戏玩法的基本假设，不是每个用户需要表达的自由度。如果某个用户想要"卸鞋后 morph 不变"，他应该改写 JSON，而不是要求插件提供一个"忽略 unequip"的开关。

### 5.2 slot 过滤与状态合成

**`TriggeredBy` 决定 morph 关心哪个槽，但不再是"事件 armor 必须占该槽"的硬性前置。** 每个事件到达时，引擎对每条 morph 做四选一的判断：

| `slotMatch` | 事件后该槽有装备 | 求值输入           | 场景                                              |
| :---------: | :--------------: | ------------------ | ------------------------------------------------- |
|    true     |       true       | `(armor, true)`    | 穿上/换上新装备                                   |
|    true     |      false       | `(nullptr, false)` | 卸下该槽装备                                      |
|    false    |      false       | `(nullptr, false)` | **事件与该槽无关，且该槽为空 → 初始化 `Default`** |
|    false    |       true       | 跳过               | 事件与该槽无关，该槽已有装备，morph 值不变        |

判定与求值代码：

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

**第 3 行（`!slotMatch && !equipped`）是 v3.0.2 修复的关键路径**。裤袜通常占据 `kCalves`，而 `NoHeel` 的 `TriggeredBy` 是 `kFeet`。玩家给 NPC 第一次穿裤袜时，脚上是空的：`slotMatch == false`、`GetWornArmor(kFeet) == nullptr`，于是走求值路径，命中 `Default → NoHeel = 1.0`，morph 被立即初始化。修复前的代码在这里 `continue`，`NoHeel` 从未被写入，必须"先穿高跟鞋（触发 `HighHeel → NoHeel=0`）再脱掉（`equipped=false` 命中 `Default`）"才能 priming。

**`!slotMatch` 时读 `GetWornArmor` 为什么安全**：该槽没有被当前事件触及，`GetWornArmor` 返回的"旧值"就是该槽的真实当前值。`TESEquipEvent` 派发期间 API 返回旧值这一行为，只在"事件正要改变的那个槽"上才造成问题。

**`!slotMatch && equipped` 跳过的含义**：事件 armor 不占这个槽，而且槽里已经有装备——说明这个 morph 关心的状态没有被本事件改变，重新求值只会得到和上次相同的结果。跳过纯属性能优化，不跳过也正确（只是多一次 `SetAbsolute` + 一次 `UpdateModelWeight`）。

**这解决了"穿头盔触发 morph 更新"的问题**：穿头盔时，若脚上有高跟鞋，`!slotMatch && equipped` 成立，整个 morph 跳过。若脚上没鞋，则会求值并（幂等地）再次写入 `NoHeel = 1.0`，无害但日志会多几条 `default-init`。

### 5.3 首条命中胜出

`Rules` 是有序列表，**第一个命中的规则生效，其余不求值**：

```cpp
const auto iter = std::ranges::find_if(morph.rules, [&](const CompiledMorphEntry& a_rule) {
    return equipped ? a_rule.when->Evaluate(ctx) : a_rule.when->IsAlwaysTrue();
});
if (iter == morph.rules.end()) {
    return false;   // 全部不命中，不触碰该 morph
}
SetAbsolute(a_actor, morphName, iter->value);
```

用 `find_if` 而非 `any_of`：谓词求值本身无副作用，但命中后需要 `SetAbsolute` 写入——这是"找到后动手"的语义，不是"判断存在性"。把副作用塞进 `any_of` 的谓词里违反纯函数契约。

**全部不命中时**：不触碰该 morph。调用方通过是否触发过 apply 决定是否调 `UpdateModelWeight`。

### 5.4 Value = 0.0 表示清除

SKEE 的 `SetMorph(actor, name, key, value)` 语义是**相对变化量**，不是绝对赋值。要得到绝对值 `v`，必须 `ClearMorph` 之后再 `SetMorph`：

```cpp
// SKEEIntegration::SetAbsolute 内部
m_bodyMorph->ClearMorph(a_actor, morphNameCStr, kNamespaceKey);
if (a_value != 0.0F) {
    m_bodyMorph->SetMorph(a_actor, morphNameCStr, kNamespaceKey, a_value);
}
```

`Value == 0.0` 时只 `ClearMorph`，跳过 `SetMorph`——效果等价且少一次引擎调用。

**JSON 里用户看到的是绝对值语义**，不需要理解 SKEE 的接口细节。

### 5.5 SKEE 命名空间 key

所有 morph 操作使用统一 key `AdeptivePantyhoseMorphKey`（常量名 `kNamespaceKey`）。这个 key 是插件在 SKEE 系统中的命名空间，隔离本插件设置的 morph，避免与其他插件冲突。

**旧 key `CH_AdeptivePantyhoseMorphKey` 的清理**：每次 `SetAbsolute` 时顺便 `ClearMorph` 旧 key（常量名 `kLegacyNamespaceKey`），兼容从旧版本升级的存档。这是一次性兼容代码，等足够长时间后可以移除。

---

## 6. 分层细节

### 6.1 Config 层（语法层）

**职责**：把 JSON 文件解析成纯数据的 `ParsedConfig`，不做任何游戏解释。

**输出类型**：

```cpp
struct ParsedPredicate {
    PredicateKind kind;              // 叶子 / AnyOf / AllOf / Not
    std::string leafType;            // 仅叶子：matcher 名
    nlohmann::json leafPayload;      // 仅叶子：原始载荷
    std::vector<std::string> refs;   // 仅 AnyOf/AllOf
    std::string ref;                 // 仅 Not
};

struct ParsedMorph {
    std::string triggeredBy;                 // 槽名，如 "kFeet"
    std::vector<ParsedMorphEntry> rules;
};

struct ParsedConfig {
    std::unordered_map<std::string, ParsedPredicate> predicates;
    std::unordered_map<std::string, ParsedMorph>     morphs;
};
```

**错误处理**：所有错误以 `std::expected<ParsedConfig, ParseError>` 返回。`ParseError` 携带精确的 JSON 路径（如 `Predicates.WearingHighHeels.AnyOf[1]`）和人类可读的消息。

**关键设计**：**叶子载荷不解释**。`{ "ByKeywords": ["SLA_KillerHeels"] }` 只记录 `leafType = "ByKeywords"`、`leafPayload = ["SLA_KillerHeels"]`，至于"ByKeywords 该怎么匹配"是领域层的事。

### 6.2 MorphRuleEngine 层（领域层）

**职责**：把 `ParsedConfig` 编译成可执行的 `CompiledRules`，并提供运行时求值。

**编译期**：

- `PredicateCompiler` 通过 `LeafFactory` 注册表分派叶子类型
- 递归编译每个谓词，用 `m_compiled` 缓存避免重复
- 用 `m_inProgress` 检测引用环（语法层只验证"名字有定义"，不检查循环）
- `TriggeredBy` 字符串经 `ParseBipedSlot` 手写映射表转为
  `RE::BGSBipedObjectForm::BipedObjectSlot`。位标志枚举不适合
  magic_enum，且映射表与 schema 的 BipedSlotName enum 一一对应。
  映射表用 `constexpr auto kSlotMap = std::to_array<std::pair<std::string_view, Slot>>({...})` 而非
  `static const unordered_map`——后者无法 `constexpr`，只能退化为运行期常量，与项目 `kCamelCase` 约定冲突。32 项线性扫描，仅启动期调用一次。

**编译产物**：

```cpp
struct CompiledMorphEntry {
    float value;
    PredicatePtr when;
};

struct CompiledMorph {
    RE::BGSBipedObjectForm::BipedObjectSlot triggeredBy;
    std::vector<CompiledMorphEntry> rules;
};

struct CompiledRules {
    std::unordered_map<std::string, CompiledMorph> morphs;
};
```

**运行时**：`MorphRuleEngine` 是**无状态**的。没有 `m_lastValue` 之类的跨事件缓存。`OnEquipEvent` 内部会调用 `GetWornArmor` 读取 actor 的当前装备，但这是"本次事件求值时的输入读取"，不是跨帧状态——下次事件重新读，不依赖上次的结论。这消除了状态漂移、跨存档污染、跳变检测的边界问题。

### 6.3 Event 层

**职责**：监听游戏事件，翻译成对引擎的调用。**不含判定逻辑**。

只监听**一个事件** `TESEquipEvent`，并在 SKSE 的 `kPostLoadGame` 消息里对玩家做一次兜底：

| 触发源               | 用途                                            | 过滤条件                            |
| -------------------- | ----------------------------------------------- | ----------------------------------- |
| `TESEquipEvent`      | 装备/卸下、NPC 加载恢复装备、玩家新游戏初始装备 | actor 是 Actor，baseObject 是 armor |
| `kPostLoadGame` 消息 | 玩家读档                                        | 无（读档后直接对玩家跑一遍）        |

**`OnEquipEvent` 的两个前置过滤**（`a_actor == nullptr`、`a_armor == nullptr`）在 v3.0.2 之后语义有细微变化：

- `a_actor == nullptr`：仍直接返回。没有 actor 就无从求值。
- `a_armor == nullptr`：**不再提前返回**。morph 求值依赖的是"事件后该槽的状态"，而不是事件 armor 本身。`a_armor == nullptr` 时 `slotMatch` 恒为 false，走 `GetWornArmor` 分支——语义自洽（事件没触及任何槽，读当前值）。实际运行中 `EventProcessor` 已在入口过滤掉 `armor == nullptr`，这条分支是防御性代码。

**为什么 `TESEquipEvent` 就够了（对 NPC）**：日志证据显示，cell 加载时引擎会为每个 NPC 批量触发它们的装备 `TESEquipEvent`，即使玩家从未与该 NPC 交互。这覆盖了"NPC 加载后已穿高跟，但之后没有穿脱"的场景。

**为什么玩家读档需要单独兜底**：读档不是"装备状态变化"，而是状态被引擎从存档恢复。引擎是否为恢复出来的装备触发 equip 事件是未定义行为，不能依赖。`kPostLoadGame` 消息在存档完全恢复后触发，此时玩家的装备状态已就绪。

**为什么不用 `TESObjectLoadedEvent`**：见 §7.6。

### 6.4 Integration 层

**职责**：封装与外部插件的接口交换协议。

**`SKEEIntegration`**：

- `Initialize()` 在 `kPostPostLoad` 阶段调用，向 SKEE 发起接口交换请求
- 持有一个类型化的 `SKEE::IBodyMorphInterface*`（不再是 `void*`）
- 对外提供语义化方法 `SetAbsolute` / `UpdateModelWeight`，屏蔽
  SKEE 的 key 概念、SetMorph 的相对语义、接口为空的检查。命名
  空间 key 是集成层的私有常量，领域层不感知。
- SKEE 未安装或接口版本过旧时打 `REX::WARN`，返回 `nullptr`，调用方静默跳过

---

## 7. 关键决策记录

### 7.1 为什么 Config 层不依赖游戏头文件

**问题**：旧版 `HighHeelDetector` 把 JSON 解析与 `RE::FormID` 类型转换混在一起，改一个 formID 都需要进游戏才能验证。

**决策**：Config 层只认识 `nlohmann::json`，叶子载荷原样保留，解释权交给领域层。

**收益**：Config 层可以脱离 Skyrim 环境单测。加新的叶子类型（如 `ActorSex`、`ActorLevel`）无需改动 Config 层。

### 7.2 为什么移除了跨帧跳变检测

**问题**：早期设计用 `m_lastValue` 记录每个 `(actor, morph)` 的上帧状态，只有"值变化"时才 `SetMorph`。但：

- 加载存档、切换角色、actor 被禁用后启用，都会让缓存失效
- 玩家希望在"卸下鞋 → 光脚"时立即 morph，而不是因为缓存误判而跳过
- 逻辑复杂度显著上升

**决策**：**无状态求值**。每次 equip/unequip 事件独立求值，直接 `ClearMorph + SetMorph` 到目标值。

**代价**：每次事件都调用一次 SKEE 接口（即使值没变）。实测这不是性能瓶颈。如果未来有性能问题，可以再引入缓存，但缓存失效的复杂度要重新评估。

### 7.3 为什么 TriggeredBy 是 morph 级元数据而非谓词

**问题**：如果 slot 判定作为一个谓词放在 `When` 里，用户可能会忘记写，导致穿头盔也触发 morph。

**决策**：把 slot 归属提升为 morph 级元数据 `TriggeredBy`。

**收益**：

- 用户不可能忘记——`TriggeredBy` 是必填字段
- 引擎的 slot 逻辑只有一处
- 语义上更准确——"这个 morph 关心哪个槽位"是 morph 的属性，不是某条规则的条件
- **消除了 `HasBipedSlot` 之类的冗余谓词**：`TriggeredBy` 已经声明了该槽，谓词层不需要再重复判断

注意 v3.0.2 之后 `TriggeredBy` 的语义从"事件 armor 必须占该槽"放宽为"该 morph 关心哪个槽、以及事件后该槽的状态如何合成"。这不是语义漂移，而是修正：morph 是 actor 级状态，本来就该由 actor 在 `TriggeredBy` 槽上的真实装备决定，而不是由恰好触发了事件的那件 armor 决定。

### 7.4 为什么遍历规则集而非遍历装备

**问题**：读档兜底需要检查玩家所有已装备的 armor。

**方案 A**：遍历所有 `BipedObjectSlot`，逐个 `GetWornArmor`，然后对每件 armor 跑一遍规则。
**方案 B**：遍历规则集，对每个 morph 确定它关心的那一件 armor（事件合成或 `GetWornArmor`）。

**决策**：**B**。

- 遍历次数 = morph 数量（通常 1~5），与装备数量无关
- 天然无重复——`TriggeredBy` 决定槽位，不会同一件 armor 处理多次
- 与 `OnEquipEvent` 的结构一致——都是"对每条 morph 求值"

### 7.5 为什么使用 REX::FAIL 而非 return false

**问题**：配置错误在 `SKSEPlugin_Load` 里返回 false 只会在日志里留一行，用户通常在游戏跑了一半才发现插件没生效。

**决策**：`main.cpp` 的 `LoadMorphRules` 里遇到 `ParseError` / `CompileError` 一律 `REX::FAIL`。

`REX::FAIL` 是 `commonlib-shared` 提供的日志 + 终止原语，等价于 `REX::CRITICAL` 日志 + `SKSE::stl::report_and_fail`——输出错误信息到日志、弹出错误框、终止游戏进程。

**收益**：用户启动游戏时立即看到错误弹窗，包含精确的 JSON 路径与错误原因，能在 10 秒内定位并修复。

### 7.6 为什么放弃 TESObjectLoadedEvent

**候选方案**：用 `TESObjectLoadedEvent` 处理"NPC 加载后已穿高跟"的场景——当 NPC 被加载进内存时主动跑一遍规则。

**实测证据**（来自开发期日志）：

1. **NPC 加载时引擎已经触发全部装备的 `TESEquipEvent`**。Whiterun Guard、Hilde、Sven、Faendal 等 NPC 在 cell 加载后的 200ms 内集中触发装备事件，早于它们的 `TESObjectLoadedEvent` 数秒。也就是说"NPC 加载后已穿高跟"这个场景已经被 equip 事件覆盖。
2. **`TESObjectLoadedEvent` 极其嘈杂**。cell 加载期间每秒数百条，绝大多数 formID 对应的是容器、门、杂物、特效，`GetName()` 为空。
3. **引入额外的时机依赖**。需要判断 `actor->Is3DLoaded()` 才能安全调用 `GetWornArmor`，否则可能拿到不完整结果。这引入"漏掉某些边缘情况"的风险。

**决策**：**只监听 `TESEquipEvent`**，不对 NPC 加载单独挂钩。

**收益**：

- 事件量从"每秒几百条"降到"每次装备变化一条"
- 消除 `Is3DLoaded` 时机判断的复杂度
- 更少的代码路径——所有 NPC 的 morph 更新都走同一入口

**代价**：如果未来发现某种 edge case 下 NPC 加载时引擎**不**触发 equip 事件（例如通过脚本直接修改 NPC 装备的 mod），需要重新评估。目前无此证据。

### 7.7 为什么 OnEquipEvent 要合成"事件后槽位状态"而不是直接用 GetWornArmor

**引擎行为**：`TESEquipEvent` 派发期间，`RE::Actor::GetWornArmor(slot)` 返回的是**事件前**状态。穿鞋事件里它仍返回 `nullptr`（或旧的那双鞋），脱鞋事件里它仍指向正要被卸下的鞋。这是引擎的事件派发时序决定的，不是 bug，但很容易踩。

**v3.0.2 之前的错误修法**：把 `OnEquipEvent` 改成对每条 morph 直接 `GetWornArmor(morph.triggeredBy)` 求值。逻辑上"用 actor 的真实状态求值"听起来对，实际结果完全反转：

| 用户操作   | `GetWornArmor` 返回值（旧状态） | 求值结果                | 期望结果                |
| ---------- | ------------------------------- | ----------------------- | ----------------------- |
| 穿上高跟鞋 | `nullptr`                       | `Default → NoHeel=1.0`  | `HighHeel → NoHeel=0.0` |
| 脱下高跟鞋 | 高跟鞋                          | `HighHeel → NoHeel=0.0` | `Default → NoHeel=1.0`  |

**正确做法**：按事件是否触及 `triggeredBy` 槽分两支合成：

```cpp
if (slotMatch) {
    // 本事件改变了该槽：新状态由事件参数直接决定
    wornArmor = a_equipped ? a_armor : nullptr;
} else {
    // 本事件没触及该槽：GetWornArmor 的旧值就是当前真实值
    wornArmor = a_actor->GetWornArmor(morph.triggeredBy);
}
```

两支各自有可靠的信息源：`slotMatch` 时事件参数是权威的（它描述的就是这次变化），`!slotMatch` 时 `GetWornArmor` 是权威的（该槽没被动过）。不存在"两支都不可信"或"两支可能冲突"的情况。

**为什么不在 `EvaluateAndApply` 内部做这个合成**：合成逻辑依赖 `slotMatch`，而 `slotMatch` 是 `OnEquipEvent` 的上下文（事件 armor 与 morph 的 triggeredBy 的关系）。`OnActorLoaded` 没有这个上下文——它不是装备事件，`GetWornArmor` 已经是当前真实值。把合成放在各入口，让 `EvaluateAndApply` 只接受自洽的 `(armor, equipped)`，两个入口的职责边界更清晰。

**测试建议**：日志里 `slot-match` 与 `default-init` 两个标记直接对应两支。rudeboybox 报告的复现场景（新 NPC + 裤袜，脚空）应该出现 `default-init` + `rule[...] -> MATCH`；穿高跟鞋应该出现 `slot-match` + `rule[...] -> MATCH`。两者同时出现即修复生效。

### 7.8 swap 场景的瞬态可接受

**场景**：同一槽从装备 A 直接换到装备 B。引擎可能派发两个事件（`equipped=false, armor=A` + `equipped=true, armor=B`），也可能只派发一个（`equipped=true, armor=B`）。

- 两个事件：先 `A` 的卸下事件走 `slotMatch && !equipped → Default`，写入一次；再 `B` 的穿戴事件走 `slotMatch && equipped → 求值`，再写入一次。中间有一次瞬态，但两个事件各自调 `UpdateModelWeight`，最终收敛到正确值。
- 一个事件：`slotMatch && equipped`，直接用 `B` 求值，一次到位。

**决策**：不特殊处理。瞬态在两次 `UpdateModelWeight` 之间，玩家不可见。为消除瞬态引入"延迟到下一帧再 apply"的机制，会破坏"无状态求值"的简洁性，得不偿失。

---

## 8. 扩展点

### 8.1 新增叶子谓词类型

在 `src/MorphRuleEngine/Leaves/` 下新建：

```cpp
auto MakeActorSexPredicate(const nlohmann::json& a_payload)
    -> std::expected<PredicatePtr, LeafError>;
```

在 `main.cpp` 的 `LoadMorphRules` 里注册：

```cpp
compiler.RegisterLeaf("ActorSex", &Engine::MakeActorSexPredicate);
```

在 `MorphRules.schema.json` 的 `PredicateLeaf` 里增加对应分支。

**无需修改**：Config 层、`PredicateCompiler`、`MorphRuleEngine`。

**注意**：新增叶子类型前先评估——如果它表达的是"armor 属于哪一类"，大概率应该是谓词；如果表达的是"哪个槽位触发"，应该是 `TriggeredBy` 的职责，不该做成谓词。

### 8.2 新增组合子

在 `Predicate.h` 增加基类实现，在 `PredicateCompiler::BuildFromParsed` 的 `switch` 里加分支。

### 8.3 新增 morph 级元数据

如果未来 morph 需要除 `TriggeredBy` 外的其他元数据（如 `Priority`、`Comment`），改 `ParsedMorph` / `CompiledMorph` 结构，两处解析器各加一行。

### 8.4 命名空间 key 可配置

如果未来需要与其他插件共享 morph 状态，给 JSON 加顶层 `Namespace` 字段。当前不做。

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
├── Integration/
│   └── SKEE/
│       └── SKEEIntegration.{h,cpp}
├── Support/
│   └── FormName.h                SafeName() 日志兜底
├── main.cpp                      装配、生命周期、kPostLoadGame 兜底
└── pch.h

assets/SKSE/AdeptivePantyhose/
├── MorphRules.json               用户可编辑的规则表
└── MorphRules.schema.json        编辑器校验用
```

---

## 10. 调试与日志

### 10.1 日志级别

日志宏来自 `REX`（`commonlib-shared` 提供），统一定义在 `REX/LOG.h`：

| 级别            | 用途                         | 示例                                   |
| --------------- | ---------------------------- | -------------------------------------- |
| `REX::TRACE`    | 详细的函数进出与参数追踪     | 仅在 debug build 使用                  |
| `REX::DEBUG`    | 关键决策与状态变化           | morph apply 详情                       |
| `REX::INFO`     | 生命周期里程碑               | 插件加载、规则集编译完成               |
| `REX::WARN`     | 可恢复的异常                 | SKEE 未安装、接口版本过旧              |
| `REX::ERROR`    | 次要功能失效，但插件整体可用 | 单条规则编译失败（如果有这种容忍场景） |
| `REX::CRITICAL` | 主要功能失效，但不终止       | 需要用户干预但不阻塞启动的场景         |
| `REX::FAIL`     | 致命错误，立即终止           | 配置解析失败、关键接口获取失败         |

**`REX::FAIL` 与 `REX::CRITICAL` 的区别**：`CRITICAL` 只写日志；`FAIL` 写日志后调用 `SKSE::stl::report_and_fail`——弹出错误框并终止游戏进程。插件初始化阶段的不可恢复错误应使用 `REX::FAIL`；运行时的严重异常（不影响插件继续运行）用 `CRITICAL`。

**DEBUG 级在正式版由编译期剔除**：`commonlib-shared` 的 release 配置默认把 `SPDLOG_ACTIVE_LEVEL` 设为 `SPDLOG_LEVEL_INFO`，所有 `REX::DEBUG` 调用在编译期整个消失——参数不求值，字符串不进二进制。详细日志版通过构建参数把 `SPDLOG_ACTIVE_LEVEL` 提到 `SPDLOG_LEVEL_TRACE`，同一份源码原样编出全量日志。**不需要运行时开关，也不需要 `#ifdef`**。

### 10.2 日志文件位置

```
Documents/My Games/Skyrim Special Edition/SKSE/AdeptivePantyhose.log
```

`CMakeLists.txt` 会在 configure 阶段硬链接此文件到项目根目录，便于调试。

### 10.3 关键日志点

**启动期（INFO，用户贴日志时自动带上）**：

- `Plugin version: X.Y.Z`
- `SKSE version: X.Y.Z`
- `Game version: X.Y.Z`
- `SKSEPlugin_Load: loaded`
- `SKEEIntegration: initialized (bodyMorphVersion=N)`
- `MorphRules loaded: N morph(s)`
- `RegisterEvents: TESEquipEvent sink registered`
- `[PostLoad] running rules for player '...'` —— 每次读档兜底

**启动期（DEBUG，详细版）**：

- `OnMessage: PostPostLoad / DataLoaded / PostLoadGame` —— 生命周期钩子
- `[Rules] parsed: N predicate(s), M morph(s)` —— 启动期 dump 头部
- `  predicate '...': leaf Type(payload)` / `... AnyOf[...]` —— 每个谓词的内容
- `  morph '...' triggeredBy='...' rules=N` + 每条 rule 的 value / when
- 失败时：`MorphRules.json syntax error at {path}: {msg}` / `... compile error ...`，同时 `REX::FAIL` 弹窗

**运行期（DEBUG，详细版）**：

- `[Equip] actor='...' armor='...' equipped=N` + `file='...' local=... slotMask=...` —— 事件入口
- `  morph '...' triggeredBy=... -> slot-match / default-init` —— 求值原因
  - `slot-match`：事件触及了该槽，用事件参数合成
  - `default-init`：事件未触及该槽且该槽为空，用 `GetWornArmor` 读到 `nullptr`，走 `Default` 初始化
  - （`!slotMatch && equipped` 的 morph 在打日志前已 `continue`，不出现）
- `      rule[i] value=N equipped=B -> MATCH` 或 `      no rule matched ...` —— 谓词求值结果
- `    [SKEE] SetAbsolute actor=... morph='...' value=N` 或 `interface null, skip` —— 写入路径
- `    [SKEE] UpdateModelWeight actor=...` 或 `interface null, skip`
- `  [Update] UpdateModelWeight actor='...'` —— 至少一条 morph 命中

**WARN / 失败**：

- `SKEEIntegration: SKEE not available, integration disabled`
- `SKEEIntegration: BodyMorph version too old (need >= N, got M)`
- `RegisterEvents: ScriptEventSourceHolder null`
- `HandlePostLoadGame: PlayerCharacter null`

---

## 11. 已知限制

1. **单 slot 触发**：`TriggeredBy` 是单个槽，无法表达"kFeet 或 kCalves 任一触发"。若未来有此需求，改为数组即可。
2. **SKEE 接口版本依赖**：BodyMorph 接口要求 >= v4。SKEE 未安装或版本过旧时整个 morph 功能静默失效。
3. **配置文件的编辑体验**：依赖用户在 MO2 目录中手动编辑 JSON。无 GUI 配置界面。
4. **NPC 加载依赖引擎的 equip 事件**：如果某 mod 通过脚本绕过装备系统直接修改 NPC 装备，且引擎不为其触发 `TESEquipEvent`，则该 NPC 的 morph 不会更新。当前无证据表明存在这种场景，但若遇到，需要重新引入 `TESObjectLoadedEvent` 作为补充触发源。
5. **morph 是 actor 级状态**：插件设置的是 actor 的 morph 值，SKEE 将其应用到该 actor 上所有带对应 morph target 的 mesh。裤袜是否视觉上贴合某双鞋，取决于 mesh 作者是否在 BodySlide 中构建了兼容的形变。插件无法修复 mesh 层面不匹配（如用 `NoHeel=100` 烘焙的裤袜不响应高跟鞋）。
6. **swap 瞬态**：同槽直接换装时，若引擎派发两个事件，morph 值会经历一次中间态再收敛。见 §7.8。
7. **`TESEquipEvent` 期间 `GetWornArmor` 返回旧值**：这是引擎行为，不是可绕过的限制。任何需要"事件后状态"的逻辑都必须用事件参数合成，不能直接读 API。见 §7.7。
8. **SE / AE 双版本**：SE 1.5.97 与 AE 1.6.1130+ 分别产出独立 DLL（`_SE` / `_AE` 后缀），部署到不同 MO2 目录。装错版本的现象是"插件加载成功但事件回调不触发"。不要同时启用两个版本，否则日志会互相覆盖。

---

## 12. 变更记录

### v3.0.2 (2026-10-24)

**修复**：首次给 NPC 穿裤袜时 `NoHeel` 未初始化，脚部朝下；必须"先穿高跟鞋再脱掉"才能 priming，每个新 NPC 都要重复。由 rudeboybox 在 Nexus 评论区报告并给出精确复现步骤。

**根因**（两层）：

1. 原 `OnEquipEvent` 仅当事件 armor 占 `TriggeredBy` 槽才求值。裤袜占 `kCalves` 不占 `kFeet`，直接 `continue`，`NoHeel` 从未被写入。
2. 简单改用 `GetWornArmor` 也不对：`TESEquipEvent` 派发期间该 API 返回事件前状态，会把穿鞋求成 `Default`、脱鞋求成 `HighHeel`，行为反转。

**方案**：`OnEquipEvent` 按事件是否触及 `TriggeredBy` 槽分两支合成"事件后该槽的真实状态"：

- `slotMatch`：该槽被本事件改变，`wornArmor = a_equipped ? a_armor : nullptr`
- `!slotMatch`：该槽未被触及，`wornArmor = GetWornArmor(triggeredBy)`

同时把跳过条件从 `!slotMatch` 改为 `!slotMatch && equipped`，使槽为空时即使事件 armor 不占槽也求值，命中 `Default` 完成初始化。`EvaluateAndApply` 的 `(armor, equipped)` 现在始终自洽。

**文档**：§4.2、§5.1、§5.2、§6.3、§10.3 同步更新；新增 §7.7（引擎行为与合成策略）、§7.8（swap 瞬态）、§11.5~11.7（已知限制）。

**发版检查清单**（本次未完成，发布前需同步）：

- `CMakeLists.txt` 的 `project(... VERSION 3.0.1 ...)` → `3.0.2`
- `vcpkg.json` 的 `"version": "3.0.1"` → `"3.0.2"`
- `docs/Architecture.md` 头部（本次已改为 v3.0.2）

### v3.0.1

- AE 构建的最低 SKSE 版本从 2.3.0 降到 2.2.5，修复 1.6.1130 / 1.6.1170 上 SKSE 拒绝加载 DLL 的问题。
- 新增 SE 1.5.97 构建（`_SE` 后缀）。
- 补充 magecore 支持。

### v3.0.0

- 重构为 Config / MorphRuleEngine / Event / Integration 四层架构。
- 引入 `MorphRules.json` 数据驱动配置与 JSON Schema 校验。
- 无状态求值引擎；移除跨帧跳变检测。
- `REX::FAIL` fail-fast 配置错误处理。
