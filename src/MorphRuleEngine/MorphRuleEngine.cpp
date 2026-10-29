// src/MorphRuleEngine/MorphRuleEngine.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/MorphRuleEngine.h"

#include "Integration/SKEE/SKEEIntegration.h"
#include "Support/FormName.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <string>
#include <unordered_set>
#include <utility>

namespace MorphRules::Engine {

namespace {

// BipedObjectSlot 是位标志枚举，kFX01 = 1U << 31，共 32 个有效位。
// 与 BIPED_OBJECTS::kTotal（42）不同：后者是 bipedData 数组长度，
// 包含 GetWornArmor 不接受的额外槽位。
constexpr std::uint32_t kBipedSlotBitCount = 32;

// 对单条 morph 求值并应用。返回 true 表示有规则命中并执行了写入。
//
// equipped == true  ：正常求值谓词
// equipped == false ：谓词一律视为不命中，仅 Default（恒真）分支可能命中
auto EvaluateAndApply(RE::Actor* a_actor,
                      const std::string& a_morphName,
                      const CompiledMorph& a_morph,
                      RE::TESObjectARMO* a_armor,
                      bool a_equipped) -> bool {
    const PredicateContext ctx{
        .actor = a_actor,
        .armor = a_armor,
    };

    const auto iter = std::ranges::find_if(a_morph.rules, [&](const CompiledMorphEntry& a_rule) {
        return a_equipped ? a_rule.when->Evaluate(ctx) : a_rule.when->IsAlwaysTrue();
    });

    if (iter == a_morph.rules.end()) {
        // 全部未命中。equipped=false 时是预期行为——谓词一律视为不命中，
        // 只有 Default 分支能命中；若 Default 都没命中，说明规则表写错了。
        REX::DEBUG("      no rule matched (equipped={}, rules={})", a_equipped, a_morph.rules.size());
        return false;
    }

    const auto index = static_cast<std::size_t>(std::distance(a_morph.rules.begin(), iter));
    REX::DEBUG("      rule[{}] value={} equipped={} -> MATCH", index, iter->value, a_equipped);

    SKEEIntegration::GetSingleton().SetAbsolute(*a_actor, a_morphName, iter->value);
    return true;
}

}  // namespace

auto MorphRuleEngine::GetSingleton() -> MorphRuleEngine& {
    static MorphRuleEngine s_singleton;
    return s_singleton;
}

void MorphRuleEngine::SetRules(CompiledRules a_rules) { m_rules = std::move(a_rules); }

void MorphRuleEngine::OnEquipEvent(RE::Actor* a_actor, RE::TESObjectARMO* a_armor, bool a_equipped) {
    if (a_actor == nullptr) {
        return;
    }

    // 收集旧装备（TESEquipEvent 期间 GetWornArmor 返回旧值）
    std::unordered_set<RE::FormID> oldArmors;
    for (std::uint32_t slot = 0; slot < kBipedSlotBitCount; ++slot) {
        const auto slotBit = static_cast<RE::BGSBipedObjectForm::BipedObjectSlot>(1U << slot);
        if (auto* worn = a_actor->GetWornArmor(slotBit); worn != nullptr) {
            oldArmors.insert(worn->GetFormID());
        }
    }

    // 卸下事件：GetWornArmor 仍返回即将移除的 armor，显式排除
    if (a_armor != nullptr) {
        oldArmors.erase(a_armor->GetFormID());
    }

    bool anyApplied = false;

    for (const auto& [morphName, morph] : m_rules.morphs) {
        const bool slotMatch = a_armor != nullptr && a_armor->HasPartOf(morph.triggeredBy);

        // TESEquipEvent 派发时 GetWornArmor 仍返回旧值，不能直接拿它当
        // 求值输入。必须用事件自身的信息合成"事件后该槽的真实状态"：
        //
        // - 事件 armor 占 triggeredBy 槽：该槽状态被本事件改变，
        //   新状态 = a_equipped ? a_armor : nullptr
        // - 事件 armor 不占该槽：该槽未被本事件触及，GetWornArmor 的
        //   旧值就是当前真实值
        RE::TESObjectARMO* wornArmor = nullptr;
        if (slotMatch) {
            wornArmor = a_equipped ? a_armor : nullptr;
        } else {
            wornArmor = a_actor->GetWornArmor(morph.triggeredBy);
        }
        const bool equipped = wornArmor != nullptr;

        // 事件与 this morph 是否相关：
        // - 事件 armor 占 triggeredBy 槽 → 相关
        // - triggeredBy 槽当前为空 → 相关（初始化 Default，首次穿裤袜的关键）
        // - 其他 → 无关，跳过
        if (!slotMatch && equipped) {
            continue;
        }

        REX::DEBUG("  morph '{}' triggeredBy={:08X} -> {}",
                   morphName,
                   std::to_underlying(morph.triggeredBy),
                   slotMatch ? "slot-match" : "default-init");

        if (EvaluateAndApply(a_actor, morphName, morph, wornArmor, equipped)) {
            anyApplied = true;
        }
    }

    if (anyApplied) {
        SKEEIntegration::GetSingleton().ApplyVertexDiffToWornMeshes(*a_actor, oldArmors);
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

        if (equipped) {
            REX::DEBUG("  morph '{}' triggeredBy={:08X} worn='{}'({:08X})",
                       morphName,
                       std::to_underlying(morph.triggeredBy),
                       Support::SafeName(armor),
                       armor->GetFormID());
        } else {
            REX::DEBUG("  morph '{}' triggeredBy={:08X} worn=<none>", morphName, std::to_underlying(morph.triggeredBy));
        }

        if (EvaluateAndApply(a_actor, morphName, morph, armor, equipped)) {
            anyApplied = true;
        }
    }

    if (anyApplied) {
        REX::DEBUG("  [Update] UpdateModelWeight actor='{}'({:08X})", Support::SafeName(a_actor), a_actor->GetFormID());
        SKEEIntegration::GetSingleton().UpdateModelWeight(*a_actor);
    }
}

}  // namespace MorphRules::Engine
