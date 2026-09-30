// src/BodyMorphManager/BodyMorphManager.h
#pragma once

#include "SKEE/IPluginInterface.h"

class BodyMorphManager {
   public:
    BodyMorphManager(const BodyMorphManager&) = delete;
    BodyMorphManager(BodyMorphManager&&) = delete;
    auto operator=(const BodyMorphManager&) -> BodyMorphManager& = delete;
    auto operator=(BodyMorphManager&&) -> BodyMorphManager& = delete;

    static auto GetSingleton() -> BodyMorphManager&;

    // 在 kPostPostLoad 阶段调用一次。成功返回 true，失败返回 false 并打 ERROR。
    [[nodiscard]] auto Init() -> bool;

    // 当前是否可用。Init 成功后为 true。用于在注册 event sink 前判断。
    [[nodiscard]] auto IsReady() const noexcept -> bool;

    // 设置本插件对 a_actor 的 a_morphName 的贡献值。
    //
    // SKEE 内部对每个 (actor, morph) 维护多插件贡献表，
    // 本函数只修改 "AdeptivePantyhoseMorphKey" 这一份，
    // 其他插件的贡献不受影响。
    //
    // a_value == 0 时移除本插件的贡献（等价于 ClearMorph）。
    // 重复调用覆盖本插件的上一次贡献，不累加。
    void SetMorph(RE::Actor* a_actor, const char* a_morphName, float a_value);

   private:
    BodyMorphManager() = default;
    ~BodyMorphManager() = default;

    SKEE::IBodyMorphInterface* m_bodyMorphInterface = nullptr;
};