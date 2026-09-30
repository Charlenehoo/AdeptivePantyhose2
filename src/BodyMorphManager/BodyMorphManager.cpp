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
    SKSE_LOG_TRACE("BodyMorphManager::Init - begin");

    // 防御性检查：RegisterListener 成功过，理论上不该到这
    const auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE_LOG_TRACE(
            "BodyMorphManager::Init - messaging interface unexpectedly null; "
            "SKSE state likely corrupted");
        return false;
    }

    SKSE_LOG_TRACE("BodyMorphManager::Init - dispatching interface exchange");
    SKEE::InterfaceExchangeMessage exchange{};
    messaging->Dispatch(
        static_cast<std::uint32_t>(
            SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface),
        &exchange, sizeof(exchange), "SKEE");

    auto* interfaceMap = exchange.interfaceMap;
    if (!interfaceMap) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init - SKEE interface map unavailable "
            "(is SKEE installed?)");
        return false;
    }

    SKSE_LOG_TRACE("BodyMorphManager::Init - querying BodyMorph interface");
    m_bodyMorphInterface = static_cast<SKEE::IBodyMorphInterface*>(
        interfaceMap->QueryInterface("BodyMorph"));
    if (!m_bodyMorphInterface) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init - BodyMorph interface unavailable");
        return false;
    }

    const auto version = m_bodyMorphInterface->GetVersion();
    if (version < kMinInterfaceVersion) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init - interface too old "
            "(require >= {}, got {})",
            kMinInterfaceVersion, version);
        m_bodyMorphInterface = nullptr;
        return false;
    }

    SKSE_LOG_INFO("BodyMorphManager::Init - initialized (interface v{})",
                  version);
    return true;
}

void BodyMorphManager::SetMorph(RE::Actor* a_actor, const char* a_morphName,
                                float a_value) {
    // 防御性检查：IsReady() == false 时不应注册 event sink，
    // 因此正常流程下这里不会被调用。TRACE 级别而非 ERROR——
    // 触发说明上游有 bug，但不该刷屏。
    if (!m_bodyMorphInterface) {
        SKSE_LOG_TRACE(
            "BodyMorphManager::SetMorph - called while not ready; ignored");
        return;
    }

    if (!a_actor) {
        SKSE_LOG_WARN("BodyMorphManager::SetMorph - null actor");
        return;
    }

    SKSE_LOG_TRACE("BodyMorphManager::SetMorph - actor={}, morph={}, value={}",
                   a_actor->GetName() ? a_actor->GetName() : "unnamed",
                   a_morphName, a_value);

    // 先清除本插件的旧贡献，再设新值。
    // kMorphKey 标识"这是本插件"——SKEE 按 key 维护多插件贡献表。
    m_bodyMorphInterface->ClearMorph(a_actor, a_morphName, kMorphKey);
    if (a_value != 0.0f) {
        m_bodyMorphInterface->SetMorph(a_actor, a_morphName, kMorphKey,
                                       a_value);
    }
    m_bodyMorphInterface->UpdateModelWeight(a_actor);
}