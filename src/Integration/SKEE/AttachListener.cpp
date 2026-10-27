// src/Integration/SKEE/AttachListener.cpp
#include "pch.h"  // IWYU pragma: keep

#include "Integration/SKEE/AttachListener.h"

#include "Integration/SKEE/SKEEIntegration.h"

namespace {

[[nodiscard]] auto HasShapeData(RE::NiAVObject* a_object) -> bool {
    if (a_object == nullptr) {
        return false;
    }
    return a_object->GetExtraData("SHAPEDATA") != nullptr;
}

}  // namespace

auto AttachListener::GetSingleton() -> AttachListener& {
    static AttachListener s_singleton;
    return s_singleton;
}

void AttachListener::OnAttach(RE::TESObjectREFR* a_refr,
                              RE::TESObjectARMO* a_armor,
                              RE::TESObjectARMA* a_addon,
                              RE::NiAVObject* a_object,
                              bool a_isFirstPerson,
                              RE::NiNode* /*a_skeleton*/,
                              RE::NiNode* /*a_root*/) {
    if (a_refr == nullptr || a_armor == nullptr || a_object == nullptr) {
        return;
    }

    REX::DEBUG("[Attach] refr={:08X} attachingArmor={:08X} attachingAddon={:08X} isFP={} object='{}' hasShapeData={}",
               a_refr->GetFormID(),
               a_armor->GetFormID(),
               a_addon != nullptr ? a_addon->GetFormID() : 0,
               a_isFirstPerson,
               a_object->name.c_str(),
               HasShapeData(a_object));

    if (!HasShapeData(a_object)) {
        REX::WARN("[Attach] current mesh '{}' has no SHAPEDATA, initializing with isAttaching=true",
                  a_object->name.c_str());
        SKEEIntegration::GetSingleton().ApplyVertexDiffCurrentMesh(*a_refr, *a_object);
    }
}
