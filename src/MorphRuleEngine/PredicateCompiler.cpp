// src/MorphRuleEngine/PredicateCompiler.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/PredicateCompiler.h"

#include <array>
#include <format>
#include <nlohmann/json.hpp>
#include <optional>
#include <string_view>
#include <utility>

namespace {

using Slot = RE::BGSBipedObjectForm::BipedObjectSlot;

// magic_enum 依赖枚举值落在编译期可枚举的连续范围内。BipedObjectSlot
// 是位标志枚举（kFX01 = 1<<31），扩展到覆盖整个值域会显著拖慢编译并
// 撑大二进制。手写映射。
//
// 列表须与 BGSBipedObjectForm.h 的 BipedObjectSlot 定义、
// MorphRules.schema.json 的 BipedSlotName enum 三处保持一致。
//
// 用 constexpr 数组而非 unordered_map：unordered_map 无法 constexpr，
// 只能退化为 static const 局部变量，与项目 kCamelCase 常量约定冲突。
// 32 项线性扫描在启动期调用一次，成本可忽略。
constexpr auto kSlotMap = std::to_array<std::pair<std::string_view, Slot>>({
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
});

auto ParseBipedSlot(std::string_view a_name) -> std::optional<Slot> {
    for (const auto& [name, slot] : kSlotMap) {
        if (name == a_name) {
            return slot;
        }
    }
    return std::nullopt;
}

}  // namespace

namespace MorphRules::Engine {

void PredicateCompiler::RegisterLeaf(std::string a_type, LeafFactory a_factory) {
    m_factories.insert_or_assign(std::move(a_type), std::move(a_factory));
}

// 编译态生命周期的唯一管理者：进入时绑定 m_currentConfig 并清空缓存，
// 返回前（无论成功失败）一律清空三个成员。CompileImpl 内部不做任何
// 收尾，加 return 路径不会引入悬空。
auto PredicateCompiler::Compile(const Syntax::ParsedConfig& a_config) -> std::expected<CompiledRules, CompileError> {
    m_currentConfig = &a_config;
    m_compiled.clear();
    m_inProgress.clear();

    auto result = CompileImpl(a_config);

    m_currentConfig = nullptr;
    m_compiled.clear();
    m_inProgress.clear();

    return result;
}

auto PredicateCompiler::CompileImpl(const Syntax::ParsedConfig& a_config)
    -> std::expected<CompiledRules, CompileError> {
    // 先编译所有谓词（内部递归），保证 morphs 引用时已经缓存。
    for (const auto& entry : a_config.predicates) {
        auto result = BuildPredicate(entry.first);
        if (!result) {
            return std::unexpected(std::move(result.error()));
        }
    }

    CompiledRules rules;
    rules.morphs.reserve(a_config.morphs.size());

    for (const auto& [morphName, parsedMorph] : a_config.morphs) {
        const auto basePath = std::format("Morphs.{}", morphName);

        auto slot = ParseBipedSlot(parsedMorph.triggeredBy);
        if (!slot.has_value()) {
            return std::unexpected(CompileError{
                .location = std::format("{}.TriggeredBy", basePath),
                .message = std::format("unknown biped slot '{}'", parsedMorph.triggeredBy),
            });
        }

        CompiledMorphRuleList list;
        list.reserve(parsedMorph.rules.size());

        for (const auto& entry : parsedMorph.rules) {
            auto pred = BuildPredicate(entry.whenRef);
            if (!pred) {
                return std::unexpected(std::move(pred.error()));
            }
            list.push_back(CompiledMorphEntry{
                .value = entry.value,
                .when = std::move(*pred),
            });
        }

        CompiledMorph compiled;
        compiled.triggeredBy = *slot;
        compiled.rules = std::move(list);
        rules.morphs.emplace(morphName, std::move(compiled));
    }

    return rules;
}

auto PredicateCompiler::BuildPredicate(const std::string& a_name) -> std::expected<PredicatePtr, CompileError> {
    if (auto iter = m_compiled.find(a_name); iter != m_compiled.end()) {
        return iter->second;
    }

    if (m_inProgress.contains(a_name)) {
        return std::unexpected(CompileError{
            .location = std::format("Predicates.{}", a_name),
            .message = "cycle detected in predicate references",
        });
    }

    // "Default" 是内置恒真谓词，不进用户定义表。
    if (a_name == Syntax::kDefaultPredicate) {
        auto pred = std::make_shared<AlwaysTruePredicate>();
        m_compiled.emplace(a_name, pred);
        return pred;
    }

    auto iter = m_currentConfig->predicates.find(a_name);
    if (iter == m_currentConfig->predicates.end()) {
        // 语法层已做引用检查，理论上到不了这里。
        return std::unexpected(CompileError{
            .location = std::format("Predicates.{}", a_name),
            .message = "predicate not defined (should have been caught by syntax layer)",
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
                    .location = a_path,
                    .message = std::format("unknown leaf type '{}'", a_parsed.leafType),
                });
            }

            auto leaf = factoryIter->second(a_parsed.leafPayload);
            if (!leaf) {
                return std::unexpected(CompileError{
                    .location = std::format("{}.{}", a_path, leaf.error().location),
                    .message = leaf.error().message,
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

    return std::unexpected(CompileError{
        .location = a_path,
        .message = "internal error: unknown predicate kind",
    });
}

}  // namespace MorphRules::Engine