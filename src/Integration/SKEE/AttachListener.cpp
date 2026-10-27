// src/Integration/SKEE/AttachListener.cpp
#include "pch.h"  // IWYU pragma: keep

#include "Integration/SKEE/AttachListener.h"

auto AttachListener::GetSingleton() -> AttachListener& {
    static AttachListener s_singleton;
    return s_singleton;
}

void AttachListener::OnAttach(RE::TESObjectREFR* /*a_refr*/,
                              RE::TESObjectARMO* /*a_armor*/,
                              RE::TESObjectARMA* /*a_addon*/,
                              RE::NiAVObject* /*a_object*/,
                              bool /*a_isFirstPerson*/,
                              RE::NiNode* /*a_skeleton*/,
                              RE::NiNode* /*a_root*/) {
    // 空实现，先验证签名与基类匹配
}
