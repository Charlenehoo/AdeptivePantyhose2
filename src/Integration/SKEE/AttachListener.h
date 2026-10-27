// src/Integration/SKEE/AttachListener.h
#pragma once

#include <IPluginInterface.h>

class AttachListener final : public IAddonAttachmentInterface {
public:
    static auto GetSingleton() -> AttachListener&;

    void OnAttach(RE::TESObjectREFR* a_refr,
                  RE::TESObjectARMO* a_armor,
                  RE::TESObjectARMA* a_addon,
                  RE::NiAVObject* a_object,
                  bool a_isFirstPerson,
                  RE::NiNode* a_skeleton,
                  RE::NiNode* a_root) override;

private:
    AttachListener() = default;
};
