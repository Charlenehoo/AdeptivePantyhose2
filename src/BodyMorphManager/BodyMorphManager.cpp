// src/BodyMorphManager/BodyMorphManager.cpp
#include "BodyMorphManager.h"

#include "PCH.h"  // IWYU pragma: keep


namespace {
constexpr const char* kMORPH_KEY = "AdeptivePantyhoseMorphKey";
}

auto BodyMorphManager::GetSingleton() -> BodyMorphManager&
{
    static BodyMorphManager s_instance;
    return s_instance;
}

auto BodyMorphManager::Init() -> bool
{
    SKSE::log::trace(">>>> Entering BodyMorphManager::Init");

    auto messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE::log::error("BodyMorphManager::Init - Failed to get messaging interface");
        SKSE::log::trace("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    SKSE::log::trace("BodyMorphManager::Init - Dispatching interface exchange message");
    SKEE::InterfaceExchangeMessage exchange{};
    messaging->Dispatch(static_cast<uint32_t>(SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface),
                        &exchange,
                        sizeof(exchange),
                        "SKEE");

    auto interfaceMap = exchange.interfaceMap;
    if (!interfaceMap) {
        SKSE::log::error("BodyMorphManager::Init - Failed to get interface map");
        SKSE::log::trace("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    SKSE::log::trace("BodyMorphManager::Init - Querying BodyMorph interface");
    m_bodyMorphInterface = static_cast<SKEE::IBodyMorphInterface*>(interfaceMap->QueryInterface("BodyMorph"));
    if (!m_bodyMorphInterface) {
        SKSE::log::error("BodyMorphManager::Init - Failed to get BodyMorph interface");
        SKSE::log::trace("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    const auto version = m_bodyMorphInterface->GetVersion();
    if (version < 4) {
        SKSE::log::error("BodyMorphManager::Init - Interface version too old (require >=4, got {})", version);
        SKSE::log::trace("<<<< Exiting BodyMorphManager::Init (false)");
        return false;
    }

    SKSE::log::trace("BodyMorphManager::Init - Initialization successful");
    SKSE::log::trace("<<<< Exiting BodyMorphManager::Init (true)");
    return true;
}

void BodyMorphManager::SetMorph(RE::Actor* actor, const char* morphName, float value)
{
    SKSE::log::trace(">>>> Entering BodyMorphManager::SetMorph");

    if (!actor) {
        SKSE::log::warn("BodyMorphManager::SetMorph - Actor is null");
        SKSE::log::trace("<<<< Exiting BodyMorphManager::SetMorph (no actor)");
        return;
    }
    if (!m_bodyMorphInterface) {
        SKSE::log::error("BodyMorphManager::SetMorph - BodyMorph interface not available");
        SKSE::log::trace("<<<< Exiting BodyMorphManager::SetMorph (no interface)");
        return;
    }

    const char* actorName = actor->GetName() ? actor->GetName() : "unnamed";
    SKSE::log::trace("BodyMorphManager::SetMorph - actor={}, morph={}, value={}", actorName, morphName, value);

    m_bodyMorphInterface->ClearMorph(actor, morphName, kMORPH_KEY);
    if (value != 0.0f) {
        m_bodyMorphInterface->SetMorph(actor, morphName, kMORPH_KEY, value);
    } else {
        SKSE::log::trace("BodyMorphManager::SetMorph - value is 0, skipping SetMorph");
    }
    m_bodyMorphInterface->UpdateModelWeight(actor);

    SKSE::log::trace("<<<< Exiting BodyMorphManager::SetMorph");
}