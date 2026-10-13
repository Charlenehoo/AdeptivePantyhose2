// src/MorphRuleEngine/MorphRuleEngine.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/MorphRuleEngine.h"

#include "Integration/SKEE/SKEEIntegration.h"

namespace MorphRules::Engine {

namespace {

// 对单条 morph 求值并应用。返回 true 表示有规则命中并执行了写入。
//
// equipped == true  ：正常求值谓词
// equipped == false ：谓词一律视为不命中，仅 Default（恒真）分支可能命中
bool EvaluateAndApply(RE::Actor* a_actor,
                      const std::string& a_morphName,
                      const CompiledMorph& a_morph,
                      RE::TESObjectARMO* a_armor,
                      bool a_equipped) {
    const PredicateContext ctx{a_actor, a_armor};

    for (const auto& rule : a_morph.rules) {
        const bool matched = a_equipped ? rule.when->Evaluate(ctx) : rule.when->IsAlwaysTrue();
        if (!matched) {
            continue;
        }
        SKEEIntegration::GetSingleton().SetAbsolute(a_actor, a_morphName, rule.value);
        return true;
    }
    return false;
}

}  // namespace

auto MorphRuleEngine::GetSingleton() -> MorphRuleEngine& {
    static MorphRuleEngine s_singleton;
    return s_singleton;
}

void MorphRuleEngine::SetRules(CompiledRules a_rules) { m_rules = std::move(a_rules); }

void MorphRuleEngine::OnEquipEvent(RE::Actor* a_actor, RE::TESObjectARMO* a_armor, bool a_equipped) {
    if (a_actor == nullptr || a_armor == nullptr) {
        return;
    }

    bool anyApplied = false;

    for (const auto& [morphName, morph] : m_rules.morphs) {
        if (!a_armor->HasPartOf(morph.triggeredBy)) {
            continue;
        }
        if (EvaluateAndApply(a_actor, morphName, morph, a_armor, a_equipped)) {
            anyApplied = true;
        }
    }

    if (anyApplied) {
        SKEEIntegration::GetSingleton().UpdateModelWeight(a_actor);
    }
}

void MorphRuleEngine::OnActorLoaded(RE::Actor* a_actor) {
    if (a_actor == nullptr) {
        return;
    }

    bool anyApplied = false;

    for (const auto& [morphName, morph] : m_rules.morphs) {
        auto* armor = a_actor->GetWornArmor(morph.triggeredBy);
        const bool equipped = armor != nullptr;
        if (EvaluateAndApply(a_actor, morphName, morph, armor, equipped)) {
            anyApplied = true;
        }
    }

    if (anyApplied) {
        SKEEIntegration::GetSingleton().UpdateModelWeight(a_actor);
    }
}

}  // namespace MorphRules::Engine