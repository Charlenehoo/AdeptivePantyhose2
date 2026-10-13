# AdeptivePantyhose 架构

**版本**：v2.0.0
**最后更新**：2026-10-13

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
│  assets/SKSE/AdeptivePantyhose/MorphRules.json          │  数据
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│  Config 层（语法层）                                     │  纯 C++23 + nlohmann::json
│    - Types.h          ParsedConfig 等纯数据类型          │  零游戏依赖
│    - SyntaxParser     解析 JSON 结构，验证引用完整性     │
└────────────────────────┬────────────────────────────────┘
                         │ ParsedConfig
                         ▼
┌─────────────────────────────────────────────────────────┐
│  MorphRuleEngine 层（领域层）                            │  引入游戏概念
│    - Predicate            抽象基类 + 内置组合子          │  RE::Actor / RE::TESObjectARMO
│    - Leaves/              KeywordPredicate 等叶子工厂    │  FormID / Keyword
│    - PredicateCompiler    编译 + 环检测                  │
│    - MorphRuleEngine      求值 + apply                   │
└────────────────────────┬────────────────────────────────┘
                         │ CompiledRules
                         ▼
┌─────────────────────────────────────────────────────────┐
│  Event 层（触发）                                        │
│    - EventProcessor      监听 TESEquipEvent，喂给引擎    │
│    - main.cpp 消息        kPostLoadGame 对玩家兜底一次   │
└────────────────────────┬────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────┐
│  Integration 层（外部接口）                              │
│    - SKEEIntegration     请求 SKEE BodyMorph 接口        │
└─────────────────────────────────────────────────────────┘
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

- **`TriggeredBy`**：触发该 morph 评估的 biped 部位槽。只有占据该槽的 armor 事件才会被评估。
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
    │     ├── 若 armor 不占据 morph.TriggeredBy → 跳过
    │     └── 若占槽：
    │           ├── 遍历 rules
    │           ├── matched = equipped && rule.when->Evaluate(ctx)
    │           └── 首个 matched → ApplyMorph(morphName, rule.value)
    │
    └── 若有任何 morph 被 apply → UpdateModelWeight(actor)
```

**场景 B：玩家读档（`kPostLoadGame` 消息）**

```
main.cpp: OnMessage(kPostLoadGame)
    │
    ▼
MorphRuleEngine::OnActorLoaded(RE::PlayerCharacter::GetSingleton())
    │
    ├── 对每条 morph：
    │     ├── armor = actor->GetWornArmor(morph.TriggeredBy)
    │     └── equipped = (armor != nullptr)
    │           └── 与 OnEquipEvent 相同路径求值
    │
    └── 若有任何 morph 被 apply → UpdateModelWeight(actor)
```

**两个入口共用 `EvaluateAndApply` 内部函数**——保证 equip 事件与读档事件的行为完全一致。

**遍历方向**：引擎遍历**规则集**，而不是遍历 actor 的装备。每个 morph 自己知道关心哪个 slot，直接 `GetWornArmor(slot)` 取那一件即可。遍历成本 = morph 数量（O(1~5)），与装备数量无关。

**为什么玩家的读档不走 `TESEquipEvent`**：读档不是"装备状态发生变化"，而是状态被引擎从存档恢复。引擎是否为恢复出来的每一件装备触发 equip 事件是未定义行为，不能依赖。`kPostLoadGame` 消息在存档完全恢复后触发，此时玩家的装备状态已就绪，主动跑一遍规则最可靠。

**为什么 NPC 加载不需要单独处理**：cell 加载时引擎会为每个 NPC 批量触发它们的装备 `TESEquipEvent`（日志里可见 Whiterun Guard、Hilde、Sven、Faendal 等 NPC 在 200ms 内集中触发全部装备的 equip 事件）。这条路径已经覆盖"NPC 加载后已穿高跟"的场景，无需额外挂钩。

---

## 5. 语义规范

### 5.1 equipped 门禁

**规则求值前先看 `equipped`**：

```cpp
matched = a_equipped && rule.when->Evaluate(ctx)
```

- `equipped == true`：正常求值谓词
- `equipped == false`：谓词一律视为不命中，直接走 `Default` 分支

**这是本插件的核心假设**：脱下鞋子 = 裸足。因此卸下高跟和卸下平底鞋的结果相同——都落到 `Default` 的 `Value: 1.0`，即 `SetMorph("NoHeel", 1.0)`。

**为什么硬编码而不做成 JSON 可配置**：这是插件对游戏玩法的基本假设，不是每个用户需要表达的自由度。如果某个用户想要"卸鞋后 morph 不变"，他应该改写 JSON，而不是要求插件提供一个"忽略 unequip"的开关。

### 5.2 slot 过滤

**TriggeredBy 是硬性前置条件**：

```cpp
if (!a_armor->HasPartOf(morph.triggeredBy)) {
    continue;   // 整个 morph 跳过，不求值任何规则
}
```

这解决了"穿头盔触发 morph 更新"的问题。头盔的 equip 事件到达引擎，但 `NoHeel` 的 `TriggeredBy` 是 `kFeet`，不匹配，整个 morph 跳过。

### 5.3 首条命中胜出

`Rules` 是有序列表，**第一个命中的规则生效，其余不求值**：

```cpp
for (const auto& rule : a_morph.rules) {
    if (matched) {
        ApplyMorph(a_actor, a_morphName, rule.value);
        return;   // 短路
    }
}
```

**全部不命中时**：不触碰该 morph。调用方通过是否触发过 apply 决定是否调 `UpdateModelWeight`。

### 5.4 Value = 0.0 表示清除

SKEE 的 `SetMorph(actor, name, key, value)` 语义是**相对变化量**，不是绝对赋值。要得到绝对值 `v`，必须 `ClearMorph` 之后再 `SetMorph`：

```cpp
// ApplyMorph 内部
bodyMorph->ClearMorph(actor, name, kMorphNamespace);
if (value != 0.0F) {
    bodyMorph->SetMorph(actor, name, kMorphNamespace, value);
}
```

`Value == 0.0` 时只 `ClearMorph`，跳过 `SetMorph`——效果等价且少一次引擎调用。

**JSON 里用户看到的是绝对值语义**，不需要理解 SKEE 的接口细节。

### 5.5 SKEE 命名空间 key

所有 morph 操作使用统一 key `AdeptivePantyhoseMorphKey`。这个 key 是插件在 SKEE 系统中的命名空间，隔离本插件设置的 morph，避免与其他插件冲突。

**旧 key `CH_AdeptivePantyhoseMorphKey` 的清理**：每次 `ApplyMorph` 时顺便 `ClearMorph` 旧 key，兼容从旧版本升级的存档。这是一次性兼容代码，等足够长时间后可以移除。

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
- `TriggeredBy` 字符串用 `magic_enum::enum_cast` 转为 `RE::BGSBipedObjectForm::BipedObjectSlot`

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

**运行时**：`MorphRuleEngine` 是**无状态**的。没有 `m_lastValue` 之类的缓存，每次事件独立求值。这消除了状态漂移、跨存档污染、跳变检测的边界问题。

### 6.3 Event 层

**职责**：监听游戏事件，翻译成对引擎的调用。**不含判定逻辑**。

只监听**一个事件** `TESEquipEvent`，并在 SKSE 的 `kPostLoadGame` 消息里对玩家做一次兜底：

| 触发源               | 用途                                            | 过滤条件                            |
| -------------------- | ----------------------------------------------- | ----------------------------------- |
| `TESEquipEvent`      | 装备/卸下、NPC 加载恢复装备、玩家新游戏初始装备 | actor 是 Actor，baseObject 是 armor |
| `kPostLoadGame` 消息 | 玩家读档                                        | 无（读档后直接对玩家跑一遍）        |

**为什么 `TESEquipEvent` 就够了（对 NPC）**：日志证据显示，cell 加载时引擎会为每个 NPC 批量触发它们的装备 `TESEquipEvent`，即使玩家从未与该 NPC 交互。这覆盖了"NPC 加载后已穿高跟，但之后没有穿脱"的场景。

**为什么玩家读档需要单独兜底**：读档不是"装备状态变化"，而是状态被引擎从存档恢复。引擎是否为恢复出来的装备触发 equip 事件是未定义行为，不能依赖。`kPostLoadGame` 消息在存档完全恢复后触发，此时玩家的装备状态已就绪。

**为什么不用 `TESObjectLoadedEvent`**：见 §7.6。

### 6.4 Integration 层

**职责**：封装与外部插件的接口交换协议。

**`SKEEIntegration`**：

- `Initialize()` 在 `kPostPostLoad` 阶段调用，向 SKEE 发起接口交换请求
- 持有一个类型化的 `SKEE::IBodyMorphInterface*`（不再是 `void*`）
- 通过 `GetBodyMorph()` 暴露给 `MorphRuleEngine`
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

### 7.3 为什么 TriggeredBy 是硬性前置而非谓词

**问题**：如果 slot 判定作为一个谓词放在 `When` 里，用户可能会忘记写，导致穿头盔也触发 morph。

**决策**：把 slot 过滤提升为 morph 级元数据 `TriggeredBy`。

**收益**：

- 用户不可能忘记——`TriggeredBy` 是必填字段
- 引擎的 slot 过滤逻辑只有一处
- 语义上更准确——"这个 morph 由哪个槽位触发"是 morph 的属性，不是某条规则的条件
- **消除了 `HasBipedSlot` 之类的冗余谓词**：既然 `TriggeredBy: kFeet` 已保证 armor 占据 `kFeet`，谓词层不需要再重复判断

### 7.4 为什么遍历规则集而非遍历装备

**问题**：读档兜底需要检查玩家所有已装备的 armor。

**方案 A**：遍历所有 `BipedObjectSlot`，逐个 `GetWornArmor`，然后对每件 armor 跑一遍规则。
**方案 B**：遍历规则集，对每个 morph 通过 `GetWornArmor(morph.triggeredBy)` 取那一件 armor。

**决策**：**B**。

- 遍历次数 = morph 数量（通常 1~5），与装备数量无关
- 天然无重复——`TriggeredBy` 决定槽位，不会同一件 armor 处理多次
- 与 `OnEquipEvent` 的逻辑结构一致——都是"对每条 morph 求值"

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
├── main.cpp                      装配、生命周期、kPostLoadGame 兜底
└── pch.h

assets/SKSE/AdeptivePantyhose/
├── MorphRules.json               用户可编辑的规则表
└── MorphRules.schema.json        编辑器校验用

schemas/
└── MorphRules.schema.json        （打包时同步到 assets）
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

### 10.2 日志文件位置

```
Documents/My Games/Skyrim Special Edition/SKSE/AdeptivePantyhose.log
```

`CMakeLists.txt` 会在项目根目录硬链接此文件，便于调试。

### 10.3 关键日志点

- **加载期成功**：`MorphRules loaded: N predicate(s), M morph(s)`
- **加载期失败**：`REX::FAIL("MorphRules.json syntax error at {}: {}", location, message)`
- **编译失败**：`REX::FAIL("MorphRules.json compile error at {}: {}", location, message)`
- **SKEE 未安装**：`REX::WARN("SKEEIntegration: SKEE not available, integration disabled")`

---

## 11. 已知限制

1. **单 slot 触发**：`TriggeredBy` 是单个槽，无法表达"kFeet 或 kCalves 任一触发"。若未来有此需求，改为数组即可。
2. **SKEE 接口版本依赖**：BodyMorph 接口要求 >= v4。SKEE 未安装或版本过旧时整个 morph 功能静默失效。
3. **配置文件的编辑体验**：依赖用户在 MO2 目录中手动编辑 JSON。无 GUI 配置界面。
4. **NPC 加载依赖引擎的 equip 事件**：如果某 mod 通过脚本绕过装备系统直接修改 NPC 装备，且引擎不为其触发 `TESEquipEvent`，则该 NPC 的 morph 不会更新。当前无证据表明存在这种场景，但若遇到，需要重新引入 `TESObjectLoadedEvent` 作为补充触发源。
