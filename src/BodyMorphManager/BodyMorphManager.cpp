// src/BodyMorphManager/BodyMorphManager.cpp
#include "PCH.h"  // IWYU pragma: keep

#include "BodyMorphManager.h"

namespace {
constexpr const char* kMORPH_KEY = "AdeptivePantyhoseMorphKey";
}

auto BodyMorphManager::GetSingleton() -> BodyMorphManager& {
    static BodyMorphManager s_instance;
    return s_instance;
}

auto BodyMorphManager::Init() -> bool {
    SKSE_LOG_TRACE(">>>> Entering BodyMorphManager::Init");

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init - Failed to get messaging interface");
        SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    SKSE_LOG_TRACE(
        "BodyMorphManager::Init - Dispatching interface exchange message");
    SKEE::InterfaceExchangeMessage exchange{};
    messaging->Dispatch(
        static_cast<uint32_t>(
            SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface),
        &exchange, sizeof(exchange), "SKEE");

    auto interfaceMap = exchange.interfaceMap;
    if (!interfaceMap) {
        SKSE_LOG_ERROR("BodyMorphManager::Init - Failed to get interface map");
        SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    SKSE_LOG_TRACE("BodyMorphManager::Init - Querying BodyMorph interface");
    m_bodyMorphInterface = static_cast<SKEE::IBodyMorphInterface*>(
        interfaceMap->QueryInterface("BodyMorph"));
    if (!m_bodyMorphInterface) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init - Failed to get BodyMorph interface");
        SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    const auto version = m_bodyMorphInterface->GetVersion();
    if (version < 4) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::Init - Interface version too old (require >=4, "
            "got {})",
            version);
        SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    SKSE_LOG_TRACE("BodyMorphManager::Init - Initialization successful");
    SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::Init (true)");
    return true;
}

void BodyMorphManager::SetMorph(RE::Actor* actor, const char* morphName,
                                float value) {
    SKSE_LOG_TRACE(">>>> Entering BodyMorphManager::SetMorph");

    if (!actor) {
        SKSE_LOG_WARN("BodyMorphManager::SetMorph - Actor is null");
        SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::SetMorph (no actor)");
        return;
    }
    if (!m_bodyMorphInterface) {
        SKSE_LOG_ERROR(
            "BodyMorphManager::SetMorph - BodyMorph interface not available");
        SKSE_LOG_TRACE(
            "<<<< Exiting BodyMorphManager::SetMorph (no interface)");
        return;
    }

    const char* actorName = actor->GetName() ? actor->GetName() : "unnamed";
    SKSE_LOG_TRACE("BodyMorphManager::SetMorph - actor={}, morph={}, value={}",
                   actorName, morphName, value);

    m_bodyMorphInterface->ClearMorph(actor, morphName, kMORPH_KEY);
    if (value != 0.0f) {
        m_bodyMorphInterface->SetMorph(actor, morphName, kMORPH_KEY, value);
    } else {
        SKSE_LOG_TRACE(
            "BodyMorphManager::SetMorph - value is 0, skipping SetMorph");
    }
    m_bodyMorphInterface->UpdateModelWeight(actor);

    SKSE_LOG_TRACE("<<<< Exiting BodyMorphManager::SetMorph");
}