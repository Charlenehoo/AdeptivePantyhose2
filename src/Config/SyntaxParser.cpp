// src/Config/SyntaxParser.cpp

#include "pch.h"  // IWYU pragma: keep

#include "Config/SyntaxParser.h"

#include <format>
#include <fstream>

namespace MorphRules::Syntax {

namespace {

constexpr const char* kKeyPredicates = "Predicates";
constexpr const char* kKeyMorphs = "Morphs";

constexpr const char* kKeyAnyOf = "AnyOf";
constexpr const char* kKeyAllOf = "AllOf";
constexpr const char* kKeyNot = "Not";

constexpr const char* kKeyTriggeredBy = "TriggeredBy";
constexpr const char* kKeyRules = "Rules";

constexpr const char* kKeyValue = "Value";
constexpr const char* kKeyWhen = "When";

constexpr std::string_view kPredicatesRoot = kKeyPredicates;
constexpr std::string_view kMorphsRoot = kKeyMorphs;

constexpr float kMorphValueDefault = 1.0F;

auto Err(std::string a_location, std::string a_message) -> std::unexpected<ParseError> {
    return std::unexpected(ParseError{std::move(a_location), std::move(a_message)});
}

auto JoinPath(std::string_view a_base, std::string_view a_key) -> std::string {
    return std::format("{}.{}", a_base, a_key);
}

auto JoinIndex(std::string_view a_base, std::size_t a_index) -> std::string {
    return std::format("{}[{}]", a_base, a_index);
}

// ---------- 谓词引用 ----------

auto ParseSingleRef(const nlohmann::json& a_json, std::string_view a_path) -> std::expected<std::string, ParseError> {
    if (!a_json.is_string()) {
        return Err(std::string(a_path), "predicate reference must be a string");
    }
    auto name = a_json.get<std::string>();
    if (name.empty()) {
        return Err(std::string(a_path), "predicate reference must not be empty");
    }
    return name;
}

auto ParsePredicateRefArray(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<std::vector<std::string>, ParseError> {
    if (!a_json.is_array()) {
        return Err(std::string(a_path), "must be an array of predicate names");
    }
    if (a_json.empty()) {
        return Err(std::string(a_path), "must not be empty");
    }

    std::vector<std::string> result;
    result.reserve(a_json.size());

    for (std::size_t i = 0; i < a_json.size(); ++i) {
        auto item = ParseSingleRef(a_json.at(i), JoinIndex(a_path, i));
        if (!item) {
            return std::unexpected(std::move(item.error()));
        }
        result.push_back(std::move(*item));
    }
    return result;
}

// ---------- 叶子谓词 ----------

auto ParseLeafPredicate(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<ParsedPredicate, ParseError> {
    if (a_json.size() != 1) {
        return Err(std::string(a_path),
                   std::format("leaf predicate expression must contain exactly one key "
                               "(found {}); use AllOf to combine multiple matchers",
                               a_json.size()));
    }

    auto iter = a_json.begin();

    ParsedPredicate result;
    result.kind = PredicateKind::kLeaf;
    result.leafType = iter.key();
    result.leafPayload = iter.value();
    return result;
}

// ---------- 单个谓词表达式 ----------

auto ParsePredicateExpr(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<ParsedPredicate, ParseError> {
    if (!a_json.is_object()) {
        return Err(std::string(a_path), "predicate expression must be an object");
    }

    const bool hasAnyOf = a_json.contains(kKeyAnyOf);
    const bool hasAllOf = a_json.contains(kKeyAllOf);
    const bool hasNot = a_json.contains(kKeyNot);

    const auto combinatorCount = static_cast<int>(hasAnyOf) + static_cast<int>(hasAllOf) + static_cast<int>(hasNot);
    if (combinatorCount > 1) {
        return Err(std::string(a_path),
                   "predicate expression mixes combinators; "
                   "use exactly one of AnyOf / AllOf / Not");
    }

    if (hasAnyOf || hasAllOf) {
        const char* key = hasAnyOf ? kKeyAnyOf : kKeyAllOf;

        auto refs = ParsePredicateRefArray(a_json.at(key), JoinPath(a_path, key));
        if (!refs) {
            return std::unexpected(std::move(refs.error()));
        }

        ParsedPredicate result;
        result.kind = hasAnyOf ? PredicateKind::kAnyOf : PredicateKind::kAllOf;
        result.refs = std::move(*refs);
        return result;
    }

    if (hasNot) {
        auto ref = ParseSingleRef(a_json.at(kKeyNot), JoinPath(a_path, kKeyNot));
        if (!ref) {
            return std::unexpected(std::move(ref.error()));
        }

        ParsedPredicate result;
        result.kind = PredicateKind::kNot;
        result.ref = std::move(*ref);
        return result;
    }

    return ParseLeafPredicate(a_json, a_path);
}

// ---------- Predicates 段 ----------

auto ParsePredicates(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<std::unordered_map<std::string, ParsedPredicate>, ParseError> {
    if (!a_json.is_object()) {
        return Err(std::string(a_path), "must be an object");
    }

    std::unordered_map<std::string, ParsedPredicate> result;
    result.reserve(a_json.size());

    for (auto iter = a_json.begin(); iter != a_json.end(); ++iter) {
        const auto& name = iter.key();
        const auto entryPath = JoinPath(a_path, name);

        if (name.empty()) {
            return Err(std::string(a_path), "predicate name must not be empty");
        }
        if (name == kDefaultPredicate) {
            return Err(entryPath, "predicate name 'Default' is reserved; rename to avoid conflict");
        }

        auto parsed = ParsePredicateExpr(iter.value(), entryPath);
        if (!parsed) {
            return std::unexpected(std::move(parsed.error()));
        }
        result.emplace(name, std::move(*parsed));
    }

    return result;
}

// ---------- Morphs 段 ----------

auto ParseMorphEntry(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<ParsedMorphEntry, ParseError> {
    if (!a_json.is_object()) {
        return Err(std::string(a_path), "morph rule entry must be an object");
    }
    if (!a_json.contains(kKeyWhen)) {
        return Err(std::string(a_path), "missing required field 'When'");
    }

    auto whenRef = ParseSingleRef(a_json.at(kKeyWhen), JoinPath(a_path, kKeyWhen));
    if (!whenRef) {
        return std::unexpected(std::move(whenRef.error()));
    }

    ParsedMorphEntry result;
    result.whenRef = std::move(*whenRef);
    result.value = kMorphValueDefault;

    if (a_json.contains(kKeyValue)) {
        const auto& valueJson = a_json.at(kKeyValue);
        const auto valuePath = JoinPath(a_path, kKeyValue);

        if (!valueJson.is_number()) {
            return Err(valuePath, "'Value' must be a number");
        }
        result.value = valueJson.get<float>();
    }

    return result;
}

auto ParseMorphRuleList(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<std::vector<ParsedMorphEntry>, ParseError> {
    if (!a_json.is_array()) {
        return Err(std::string(a_path), "must be an array of rule entries");
    }
    if (a_json.empty()) {
        return Err(std::string(a_path), "rule list must not be empty");
    }

    std::vector<ParsedMorphEntry> result;
    result.reserve(a_json.size());

    for (std::size_t i = 0; i < a_json.size(); ++i) {
        auto entry = ParseMorphEntry(a_json.at(i), JoinIndex(a_path, i));
        if (!entry) {
            return std::unexpected(std::move(entry.error()));
        }
        result.push_back(std::move(*entry));
    }
    return result;
}

auto ParseMorph(const nlohmann::json& a_json, std::string_view a_path) -> std::expected<ParsedMorph, ParseError> {
    if (!a_json.is_object()) {
        return Err(std::string(a_path), "morph must be an object");
    }
    if (!a_json.contains(kKeyTriggeredBy)) {
        return Err(std::string(a_path), "missing required field 'TriggeredBy'");
    }
    if (!a_json.contains(kKeyRules)) {
        return Err(std::string(a_path), "missing required field 'Rules'");
    }

    const auto& triggeredByJson = a_json.at(kKeyTriggeredBy);
    const auto triggeredByPath = JoinPath(a_path, kKeyTriggeredBy);

    if (!triggeredByJson.is_string()) {
        return Err(triggeredByPath, "'TriggeredBy' must be a string (e.g. \"kFeet\")");
    }
    auto triggeredBy = triggeredByJson.get<std::string>();
    if (triggeredBy.empty()) {
        return Err(triggeredByPath, "'TriggeredBy' must not be empty");
    }

    auto rules = ParseMorphRuleList(a_json.at(kKeyRules), JoinPath(a_path, kKeyRules));
    if (!rules) {
        return std::unexpected(std::move(rules.error()));
    }

    ParsedMorph result;
    result.triggeredBy = std::move(triggeredBy);
    result.rules = std::move(*rules);
    return result;
}

auto ParseMorphs(const nlohmann::json& a_json, std::string_view a_path)
    -> std::expected<std::unordered_map<std::string, ParsedMorph>, ParseError> {
    if (!a_json.is_object()) {
        return Err(std::string(a_path), "must be an object");
    }

    std::unordered_map<std::string, ParsedMorph> result;
    result.reserve(a_json.size());

    for (auto iter = a_json.begin(); iter != a_json.end(); ++iter) {
        const auto& name = iter.key();
        const auto entryPath = JoinPath(a_path, name);

        if (name.empty()) {
            return Err(std::string(a_path), "morph name must not be empty");
        }

        auto morph = ParseMorph(iter.value(), entryPath);
        if (!morph) {
            return std::unexpected(std::move(morph.error()));
        }
        result.emplace(name, std::move(*morph));
    }

    return result;
}

// ---------- 引用完整性检查 ----------

auto IsKnownPredicate(std::string_view a_name, const std::unordered_map<std::string, ParsedPredicate>& a_predicates)
    -> bool {
    if (a_name == kDefaultPredicate) {
        return true;
    }
    return a_predicates.contains(std::string{a_name});
}

auto ValidatePredicateRefs(const ParsedConfig& a_config) -> std::expected<void, ParseError> {
    for (const auto& [name, pred] : a_config.predicates) {
        const auto basePath = JoinPath(kPredicatesRoot, name);

        switch (pred.kind) {
            case PredicateKind::kAnyOf:
            case PredicateKind::kAllOf: {
                const char* key = pred.kind == PredicateKind::kAnyOf ? kKeyAnyOf : kKeyAllOf;
                for (std::size_t i = 0; i < pred.refs.size(); ++i) {
                    if (!IsKnownPredicate(pred.refs.at(i), a_config.predicates)) {
                        return Err(JoinIndex(JoinPath(basePath, key), i),
                                   std::format("referenced predicate '{}' is not defined", pred.refs.at(i)));
                    }
                }
                break;
            }
            case PredicateKind::kNot: {
                if (!IsKnownPredicate(pred.ref, a_config.predicates)) {
                    return Err(JoinPath(basePath, kKeyNot),
                               std::format("referenced predicate '{}' is not defined", pred.ref));
                }
                break;
            }
            case PredicateKind::kLeaf:
                break;
        }
    }
    return {};
}

auto ValidateMorphRefs(const ParsedConfig& a_config) -> std::expected<void, ParseError> {
    for (const auto& [name, morph] : a_config.morphs) {
        const auto rulesRoot = JoinPath(kMorphsRoot, name);
        const auto basePath = JoinPath(rulesRoot, kKeyRules);

        for (std::size_t i = 0; i < morph.rules.size(); ++i) {
            if (!IsKnownPredicate(morph.rules.at(i).whenRef, a_config.predicates)) {
                return Err(JoinIndex(basePath, i),
                           std::format("'When' references undefined predicate '{}'", morph.rules.at(i).whenRef));
            }
        }
    }
    return {};
}

}  // namespace

// ---------- 公开入口 ----------

auto ParseJson(const nlohmann::json& a_json) -> ParseResult {
    if (!a_json.is_object()) {
        return Err("<root>", "top-level JSON value must be an object");
    }
    if (!a_json.contains(kKeyPredicates)) {
        return Err("<root>", "missing required section 'Predicates'");
    }
    if (!a_json.contains(kKeyMorphs)) {
        return Err("<root>", "missing required section 'Morphs'");
    }

    ParsedConfig config;

    auto predicates = ParsePredicates(a_json.at(kKeyPredicates), kKeyPredicates);
    if (!predicates) {
        return std::unexpected(std::move(predicates.error()));
    }
    config.predicates = std::move(*predicates);

    auto morphs = ParseMorphs(a_json.at(kKeyMorphs), kKeyMorphs);
    if (!morphs) {
        return std::unexpected(std::move(morphs.error()));
    }
    config.morphs = std::move(*morphs);

    if (auto valid = ValidatePredicateRefs(config); !valid) {
        return std::unexpected(std::move(valid.error()));
    }
    if (auto valid = ValidateMorphRefs(config); !valid) {
        return std::unexpected(std::move(valid.error()));
    }

    return config;
}

auto ParseFile(const std::filesystem::path& a_path) -> ParseResult {
    std::ifstream file(a_path);
    if (!file.is_open()) {
        return Err("<file>", std::format("failed to open '{}'", a_path.string()));
    }

    nlohmann::json json;
    try {
        file >> json;
    } catch (const nlohmann::json::parse_error& e) {
        return Err("<file>", std::format("JSON parse error: {}", e.what()));
    }

    return ParseJson(json);
}

}  // namespace MorphRules::Syntax