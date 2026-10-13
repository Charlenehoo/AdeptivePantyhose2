// src/Config/Types.h
#pragma once

#include <cstdint>
#include <expected>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

// 配置文件的语法层表示。
//
// 本命名空间只理解 MorphRules.json 的结构：
//   - 顶层 Predicates / Morphs 两个对象
//   - 谓词表达式的形状（叶子 / AnyOf / AllOf / Not）
//   - morph 规则列表的形状（Value + When）
//
// 它不解释任何游戏领域概念——叶子谓词的负载（如 "ByKeywords" 下
// 的具体关键字）以原始 nlohmann::json 形式保留，交由领域层编译。
namespace MorphRules::Syntax {

enum class PredicateKind : std::uint8_t {
    kLeaf,   // { "ByKeywords": [...] } / { "ByFormIDRange": [...] } / ...
    kAnyOf,  // { "AnyOf": ["A", "B"] }
    kAllOf,  // { "AllOf": ["A", "B"] }
    kNot,    // { "Not": "A" }
};

struct ParsedPredicate {
    PredicateKind kind = PredicateKind::kLeaf;

    // 仅 kind == kLeaf 时有效。
    std::string leafType;        // 例如 "ByKeywords"、"ByFormIDRange"
    nlohmann::json leafPayload;  // 原始载荷，不解释

    // 仅 kind == kAnyOf / kAllOf 时有效。
    std::vector<std::string> refs;

    // 仅 kind == kNot 时有效。
    std::string ref;
};

struct ParsedMorphEntry {
    float value = 1.0F;
    std::string whenRef;
};

struct ParsedConfig {
    std::unordered_map<std::string, ParsedPredicate> predicates;
    std::unordered_map<std::string, std::vector<ParsedMorphEntry>> morphs;
};

struct ParseError {
    // JSON 结构路径，如 "Predicates.WearingHighHeels.AnyOf[1]"。
    // 文件层面的错误（打不开、JSON 语法错）用 "<file>" 表示。
    std::string location;
    std::string message;
};

using ParseResult = std::expected<ParsedConfig, ParseError>;

// 内置保留谓词名，恒为真，用作 morph 规则列表的兜底。
// 用户在 Predicates 段中不能定义同名谓词。
inline constexpr std::string_view kDefaultPredicate = "Default";

}  // namespace MorphRules::Syntax