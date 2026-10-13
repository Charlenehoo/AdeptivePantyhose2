// src/MorphRuleEngine/PredicateCompiler.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/PredicateCompiler.h"

#include <format>
#include <nlohmann/json.hpp>

namespace {

// magic_enum 依赖枚举值落在编译期可枚举的连续范围内。BipedObjectSlot
// 是位标志枚举（kFX01 = 1<<31），扩展到覆盖整个值域会显著拖慢编译并
// 撑大二进制。手写映射。
//
// 列表须与 BGSBipedObjectForm.h 的 BipedObjectSlot 定义、
// MorphRules.schema.json 的 BipedSlotName enum 三处保持一致。
auto ParseBipedSlot(std::string_view a_name) -> std::optional<RE::BGSBipedObjectForm::BipedObjectSlot> {
    using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;

    static const std::unordered_map<std::string_view, Slot> kMap = {
        {"kHead", Slot::kHead},
        {"kHair", Slot::kHair},
        {"kBody", Slot::kBody},
        {"kHands", Slot::kHands},
        {"kForearms", Slot::kForearms},
        {"kAmulet", Slot::kAmulet},
        {"kRing", Slot::kRing},
        {"kFeet", Slot::kFeet},
        {"kCalves", Slot::kCalves},
        {"kShield", Slot::kShield},
        {"kTail", Slot::kTail},
        {"kLongHair", Slot::kLongHair},
        {"kCirclet", Slot::kCirclet},
        {"kEars", Slot::kEars},
        {"kModMouth", Slot::kModMouth},
        {"kModNeck", Slot::kModNeck},
        {"kModChestPrimary", Slot::kModChestPrimary},
        {"kModBack", Slot::kModBack},
        {"kModMisc1", Slot::kModMisc1},
        {"kModPelvisPrimary", Slot::kModPelvisPrimary},
        {"kDecapitateHead", Slot::kDecapitateHead},
        {"kDecapitate", Slot::kDecapitate},
        {"kModPelvisSecondary", Slot::kModPelvisSecondary},
        {"kModLegRight", Slot::kModLegRight},
        {"kModLegLeft", Slot::kModLegLeft},
        {"kModFaceJewelry", Slot::kModFaceJewelry},
        {"kModChestSecondary", Slot::kModChestSecondary},
        {"kModShoulder", Slot::kModShoulder},
        {"kModArmLeft", Slot::kModArmLeft},
        {"kModArmRight", Slot::kModArmRight},
        {"kModMisc2", Slot::kModMisc2},
        {"kFX01", Slot::kFX01},
    };

    if (auto iter = kMap.find(a_name); iter != kMap.end()) {
        return iter->second;
    }
    return std::nullopt;
}
}  // namespace

namespace MorphRules::Engine {

void PredicateCompiler::RegisterLeaf(std::string a_type, LeafFactory a_factory) {
    m_factories.insert_or_assign(std::move(a_type), std::move(a_factory));
}

auto PredicateCompiler::Compile(const Syntax::ParsedConfig& a_config) -> std::expected<CompiledRules, CompileError> {
    m_config = &a_config;
    m_compiled.clear();
    m_inProgress.clear();

    // 先编译所有谓词（内部递归），保证 morphs 引用时已经缓存。
    for (const auto& [name, _] : a_config.predicates) {
        auto result = BuildPredicate(name);
        if (!result) {
            m_config = nullptr;
            return std::unexpected(std::move(result.error()));
        }
    }

    CompiledRules rules;
    rules.morphs.reserve(a_config.morphs.size());

    for (const auto& [morphName, parsedMorph] : a_config.morphs) {
        const auto basePath = std::format("Morphs.{}", morphName);

        auto slot = ParseBipedSlot(parsedMorph.triggeredBy);
        if (!slot.has_value()) {
            m_config = nullptr;
            return std::unexpected(CompileError{
                std::format("{}.TriggeredBy", basePath),
                std::format("unknown biped slot '{}'", parsedMorph.triggeredBy),
            });
        }

        CompiledMorphRuleList list;
        list.reserve(parsedMorph.rules.size());

        for (const auto& entry : parsedMorph.rules) {
            auto pred = BuildPredicate(entry.whenRef);
            if (!pred) {
                m_config = nullptr;
                return std::unexpected(std::move(pred.error()));
            }
            list.push_back(CompiledMorphEntry{entry.value, std::move(*pred)});
        }

        CompiledMorph compiled;
        compiled.triggeredBy = *slot;
        compiled.rules = std::move(list);
        rules.morphs.emplace(morphName, std::move(compiled));
    }

    m_config = nullptr;
    return rules;
}

auto PredicateCompiler::BuildPredicate(const std::string& a_name) -> std::expected<PredicatePtr, CompileError> {
    if (auto iter = m_compiled.find(a_name); iter != m_compiled.end()) {
        return iter->second;
    }

    if (m_inProgress.contains(a_name)) {
        return std::unexpected(CompileError{
            std::format("Predicates.{}", a_name),
            "cycle detected in predicate references",
        });
    }

    // "Default" 是内置恒真谓词，不进用户定义表。
    if (a_name == Syntax::kDefaultPredicate) {
        auto pred = std::make_shared<AlwaysTruePredicate>();
        m_compiled.emplace(a_name, pred);
        return pred;
    }

    auto iter = m_config->predicates.find(a_name);
    if (iter == m_config->predicates.end()) {
        // 语法层已做引用检查，理论上到不了这里。
        return std::unexpected(CompileError{
            std::format("Predicates.{}", a_name),
            "predicate not defined (should have been caught by syntax layer)",
        });
    }

    const auto basePath = std::format("Predicates.{}", a_name);

    m_inProgress.insert(a_name);
    auto result = BuildFromParsed(iter->second, basePath);
    m_inProgress.erase(a_name);

    if (result) {
        m_compiled.emplace(a_name, *result);
    }
    return result;
}

auto PredicateCompiler::BuildFromParsed(const Syntax::ParsedPredicate& a_parsed, const std::string& a_path)
    -> std::expected<PredicatePtr, CompileError> {
    using Kind = Syntax::PredicateKind;

    switch (a_parsed.kind) {
        case Kind::kLeaf: {
            auto factoryIter = m_factories.find(a_parsed.leafType);
            if (factoryIter == m_factories.end()) {
                return std::unexpected(CompileError{
                    a_path,
                    std::format("unknown leaf type '{}'", a_parsed.leafType),
                });
            }

            auto leaf = factoryIter->second(a_parsed.leafPayload);
            if (!leaf) {
                return std::unexpected(CompileError{
                    std::format("{}.{}", a_path, leaf.error().location),
                    leaf.error().message,
                });
            }
            return *leaf;
        }

        case Kind::kAnyOf:
        case Kind::kAllOf: {
            std::vector<PredicatePtr> children;
            children.reserve(a_parsed.refs.size());

            for (const auto& ref : a_parsed.refs) {
                auto child = BuildPredicate(ref);
                if (!child) {
                    return std::unexpected(std::move(child.error()));
                }
                children.push_back(std::move(*child));
            }

            if (a_parsed.kind == Kind::kAnyOf) {
                return std::make_shared<AnyOfPredicate>(std::move(children));
            }
            return std::make_shared<AllOfPredicate>(std::move(children));
        }

        case Kind::kNot: {
            auto child = BuildPredicate(a_parsed.ref);
            if (!child) {
                return std::unexpected(std::move(child.error()));
            }
            return std::make_shared<NotPredicate>(std::move(*child));
        }
    }

    return std::unexpected(CompileError{a_path, "internal error: unknown predicate kind"});
}

}  // namespace MorphRules::Engine