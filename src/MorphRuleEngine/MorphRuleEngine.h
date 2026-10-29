// src/MorphRuleEngine/MorphRuleEngine.h
#pragma once

#include "MorphRuleEngine/CompiledRules.h"

namespace RE {
class Actor;
class TESObjectARMO;
}  // namespace RE

namespace MorphRules::Engine {

// 无状态求值引擎：对每次事件独立求值所有 morph 规则，第一个命中的
// 规则生效。不维护跨帧状态。
class MorphRuleEngine {
public:
    static auto GetSingleton() -> MorphRuleEngine&;

    // 启动时注入编译后的规则。
    void SetRules(CompiledRules a_rules);

    // 只求值并 SetAbsolute，不碰 mesh。返回是否有规则命中。
    // 幂等——重复调用结果相同。
    auto EvaluateRules(RE::Actor* a_actor, RE::TESObjectARMO* a_armor, bool a_equipped) -> bool;

    // 处理 TESEquipEvent：求值 + 对旧装备重应用
    void OnEquipEvent(RE::Actor* a_actor, RE::TESObjectARMO* a_armor, bool a_equipped);

    // 对 actor 当前所有已装备的 armor 跑一遍规则。
    // 每个 morph 通过 GetWornArmor(triggeredBy) 取那一件 armor。
    // 用于玩家读档兜底。
    void OnActorLoaded(RE::Actor* a_actor);

private:
    MorphRuleEngine() = default;

    CompiledRules m_rules;
};

}  // namespace MorphRules::Engine
