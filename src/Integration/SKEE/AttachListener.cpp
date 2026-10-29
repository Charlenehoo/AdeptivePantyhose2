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
    // v3.0.4 起此回调不做任何事：
    // - 当前 mesh 的 SHAPEDATA 由 SKEE 的 BodyMorphInterface::OnAttach
    //   以 isAttaching=true 处理（我们在 observer vector 末尾，它已完成）。
    // - 其他已存在 mesh 的更新由 OnEquipEvent 栈里的
    //   SKEEIntegration::ApplyVertexDiffToWornMeshes 驱动。
    //
    // 不要在这里调 ApplyVertexDiff(true)：isAttaching=true 不重置顶点，
    // 会在 SKEE 已应用的顶点上再次叠加 morph delta（double application）。
}
