// src/BodyMorphManager/BodyMorphManager.cpp
#include "PCH.h"  // IWYU pragma: keep

#include "BodyMorphManager.h"

namespace {

constexpr const char* kMorphKey = "AdeptivePantyhoseMorphKey";
constexpr std::uint32_t kMinInterfaceVersion = 4;

}  // namespace

auto BodyMorphManager::GetSingleton() -> BodyMorphManager& {
    static BodyMorphManager s_instance;
    return s_instance;
}

auto BodyMorphManager::IsReady() const noexcept -> bool {
    return m_bodyMorphInterface != nullptr;
}

auto BodyMorphManager::Init() -> bool {
    SKSE_LOG_TRACE("BodyMorphManager::Init — begin");

    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        // 防御性检查：RegisterListener 成功过，理论上不该到这
        SKSE_LOG_TRACE(
            "BodyMorphManager::Init — messaging interface vanished; "
            "SKSE state likely corrupted");
        return false;
    }

    SKEE::InterfaceExchangeMessage exchange{};
    messaging->Dispatch(
        static_cast<std::uint32_t>(
            SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface),
        &exchange, sizeof(exchange), "SKEE");

    auto* interfaceMap = exchange.interfaceMap;
    if (!interfaceMap) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init — SKEE interface map unavailable "
            "(is SKEE installed?)");
        return false;
    }

    m_bodyMorphInterface = static_cast<SKEE::IBodyMorphInterface*>(
        interfaceMap->QueryInterface("BodyMorph"));
    if (!m_bodyMorphInterface) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init — BodyMorph interface unavailable");
        return false;
    }

    const auto version = m_bodyMorphInterface->GetVersion();
    if (version < kMinInterfaceVersion) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init — interface too old "
            "(require >= {}, got {})",
            kMinInterfaceVersion, version);
        m_bodyMorphInterface = nullptr;
        return false;
    }

    SKSE_LOG_INFO("BodyMorphManager initialized (interface v{})", version);
    return true;
}

void BodyMorphManager::SetMorph(RE::Actor* a_actor, const char* a_morphName,
                                float a_value) {
    // 防御性检查：IsReady() == false 时不应注册 event sink，
    // 因此正常流程下这里不会被调用。TRACE 级别而非 ERROR——
    // 触发说明上游有 bug，但不该刷屏。
    if (!m_bodyMorphInterface) {
        SKSE_LOG_TRACE(
            "BodyMorphManager::SetMorph called while not ready — ignored");
        return;
    }

    if (!a_actor) {
        SKSE_LOG_WARN("BodyMorphManager::SetMorph — null actor");
        return;
    }

    SKSE_LOG_TRACE("BodyMorphManager::SetMorph — actor={}, morph={}, value={}",
                   a_actor->GetName() ? a_actor->GetName() : "unnamed",
                   a_morphName, a_value);

    // 先清除旧值，非零时再设新值。
    // 两步合并处理 "设为 0" 和 "设为非 0" 两种情况。
    m_bodyMorphInterface->ClearMorph(a_actor, a_morphName, kMorphKey);
    if (a_value != 0.0f) {
        m_bodyMorphInterface->SetMorph(a_actor, a_morphName, kMorphKey,
                                       a_value);
    }
    m_bodyMorphInterface->UpdateModelWeight(a_actor);
}