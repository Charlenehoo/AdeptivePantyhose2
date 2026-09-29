#include "PCH.h"

#include "BodyMorphManager/BodyMorphManager.h"
#include "EventProcessor/EventProcessor.h"

#include <Windows.h>
#ifdef ERROR
#undef ERROR
#endif

namespace {

bool OnPostPostLoad() {
    RE::ConsoleLog::GetSingleton()->Print("Hello world");
    SKSE_LOG_TRACE("kPostPostLoad: initializing BodyMorphManager");
    if (!BodyMorphManager::GetSingleton().Init()) {
        SKSE_LOG_ERROR("BodyMorphManager init failed");
        return false;
    }
    return true;
}

void OnDataLoaded() {
    SKSE_LOG_TRACE("kDataLoaded: loading config and registering sinks");

    auto& processor = EventProcessor::GetSingleton();

    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        SKSE_LOG_ERROR("ScriptEventSourceHolder unavailable");
        return;
    }

    holder->AddEventSink<RE::TESEquipEvent>(&processor);
    holder->AddEventSink<RE::TESObjectLoadedEvent>(&processor);
    SKSE_LOG_INFO("Event sinks registered");
}

void OnMessage(SKSE::MessagingInterface::Message* msg) {
    if (!msg) return;

    switch (msg->type) {
        case SKSE::MessagingInterface::kPostPostLoad:
            OnPostPostLoad();
            break;
        case SKSE::MessagingInterface::kDataLoaded:
            OnDataLoaded();
            break;
        default:
            break;
    }
}

}  // namespace

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse) {
    ::MessageBoxW(nullptr, L"SKSEPlugin_Load reached", L"AdeptivePantyhose2",
                  MB_OK | MB_ICONINFORMATION);

    SKSE::Init(skse);

    auto messagingInterface = SKSE::GetMessagingInterface();
    if (!messagingInterface) {
        SKSE_LOG_CRITICAL("Failed to get messaging interface");
    }

    if (!messagingInterface->RegisterListener(OnMessage)) {
        SKSE_LOG_CRITICAL("Failed to register message listener");
    }

    SKSE_LOG_INFO("Plugin loaded successfully");
    return true;
}