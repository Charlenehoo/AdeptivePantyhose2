// src/Integration/SKEE/AttachListener.cpp
#include "pch.h"  // IWYU pragma: keep

#include "Integration/SKEE/AttachListener.h"

#include "Integration/SKEE/SKEEIntegration.h"
#include "MorphRuleEngine/MorphRuleEngine.h"

auto AttachListener::GetSingleton() -> AttachListener& {
    static AttachListener s_singleton;
    return s_singleton;
}

void AttachListener::OnAttach(RE::TESObjectREFR* a_refr,
                              RE::TESObjectARMO* a_armor,
                              RE::TESObjectARMA* /*a_addon*/,
                              RE::NiAVObject* a_object,
                              bool a_isFirstPerson,
                              RE::NiNode* /*a_skeleton*/,
                              RE::NiNode* /*a_root*/) {
    if (a_refr == nullptr || a_armor == nullptr || a_object == nullptr) {
        return;
    }
    auto* actor = a_refr->As<RE::Actor>();
    if (actor == nullptr) {
        return;
    }

    auto& skee = SKEEIntegration::GetSingleton();
    if (!skee.IsAvailable()) {
        return;
    }

    LOG_DEBUG("[Attach] refr={:08X} armor={:08X} object='{}' isFP={}",
               a_refr->GetFormID(),
               a_armor->GetFormID(),
               a_object->name.c_str(),
               a_isFirstPerson);

    // 1) 为当前装备求值（SSOT：Modex 的 AddWornItem 不触发 TESEquipEvent，
    //    求值必须在 OnAttach 里完成）
    MorphRules::Engine::MorphRuleEngine::GetSingleton().EvaluateRules(actor, a_armor, /*a_equipped=*/true);

    // 2) 对当前 mesh 以 isAttaching=false 补一次。OnAttach 意味着 mesh 已 attach，
    //    顶点就绪。不检查 SHAPEDATA——NoHeel=0 时 HasMorphs=false，SKEE 没创建
    //    SHAPEDATA，但 !bodyData 分支会用当前 base 顶点创建，正确。
    skee.ApplyVertexDiff(*a_refr, *a_object, /*a_isAttaching=*/false);
}
