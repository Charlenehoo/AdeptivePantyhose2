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

    REX::DEBUG("[Attach] refr={:08X} armor={:08X} object='{}' isFP={}",
               a_refr->GetFormID(),
               a_armor->GetFormID(),
               a_object->name.c_str(),
               a_isFirstPerson);

    // 1) 为当前装备求值。这是 SSOT：无论 TESEquipEvent 是否触发（Modex 的
    //    AddWornItem 不触发），OnAttach 一定会被调用，求值在这里完成。
    //    幂等——如果 TESEquipEvent 已经求值过，重复 SetAbsolute 无害。
    MorphRules::Engine::MorphRuleEngine::GetSingleton().EvaluateRules(actor, a_armor, /*a_equipped=*/true);

    // 2) 对当前 mesh 以 isAttaching=false 补一次。
    //    SKEE 的 OnAttach（observer 顺序先于我们）已用 isAttaching=true 处理过，
    //    但用的是 SetAbsolute 前的旧 morph 值。我们刚更新了 morph 值，需要用
    //    isAttaching=false 重新应用：重置到 SHAPEDATA（刚由 SKEE 用正确 base
    //    创建），再应用新值。与 SKEE 的 isAttaching=true 结果相同——幂等。
    //
    //    如果 SHAPEDATA 不存在（SKEE 的 HasMorphs 为 false 或 TRI 不匹配），
    //    跳过：没有 morph target 就没有可应用的内容。
    //
    //    注意：这个 D3D 操作在 AttachBipedObject hook 栈里。v3.0.3 的 IED
    //    冲突源于对其他 mesh 的操作，对当前 mesh 操作是否冲突需要实测。
    if (!SKEEIntegration::HasShapeDataRecursive(a_object)) {
        REX::DEBUG("[Attach] current mesh '{}' no SHAPEDATA, skip re-apply", a_object->name.c_str());
        return;
    }

    REX::DEBUG("[Attach] current mesh '{}' re-applying with isAttaching=false", a_object->name.c_str());
    skee.ApplyVertexDiff(*a_refr, *a_object, /*a_isAttaching=*/false);
}
