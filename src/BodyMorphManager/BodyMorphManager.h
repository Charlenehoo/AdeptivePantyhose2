// src/BodyMorphManager/BodyMorphManager.h
#pragma once
#include "SKEE/IPluginInterface.h"

class BodyMorphManager
{
public:
    BodyMorphManager(const BodyMorphManager&) = delete;
    BodyMorphManager(BodyMorphManager&&) = delete;
    auto operator=(const BodyMorphManager&) -> BodyMorphManager& = delete;
    auto operator=(BodyMorphManager&&) -> BodyMorphManager& = delete;
    static auto GetSingleton() -> BodyMorphManager&;

    auto Init() -> bool;
    void SetMorph(RE::Actor* actor, const char* morphName, float value);

private:
    BodyMorphManager() = default;
    ~BodyMorphManager() = default;
    SKEE::IBodyMorphInterface* m_bodyMorphInterface = nullptr;
};