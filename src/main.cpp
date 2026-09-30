#include "PCH.h"

#include "BodyMorphManager/BodyMorphManager.h"
#include "EventProcessor/EventProcessor.h"

namespace {

void OnPostPostLoad() {
    SKSE_LOG_TRACE("kPostPostLoad: initializing BodyMorphManager");

    if (BodyMorphManager::GetSingleton().Init()) {
        SKSE_LOG_INFO("BodyMorphManager initialized");
    } else {
        SKSE_LOG_ERROR(
            "BodyMorphManager init failed — morph features will be "
            "unavailable");
    }
}

void OnDataLoaded() {
    RE::ConsoleLog::GetSingleton()->Print("Hello world");
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

void OnMessage(SKSE::MessagingInterface::Message* a_msg) {
    if (!a_msg) {
        return;
    }

    switch (a_msg->type) {
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

#ifdef NDEBUG
constexpr REX::ELogLevel kLogLevel = REX::ELogLevel::Info;
#else
constexpr REX::ELogLevel kLogLevel = REX::ELogLevel::Trace;
#endif

}  // namespace

// NOLINTNEXTLINE(readability-identifier-naming)
extern "C" [[maybe_unused]] __declspec(dllexport) bool SKSEPlugin_Load(
    const SKSE::LoadInterface* a_skse) {
    SKSE::Init(a_skse, SKSE::InitInfo{
                           .log = true,
                           .logLevel = kLogLevel,
                       });

    auto* messagingInterface = SKSE::GetMessagingInterface();
    if (!messagingInterface) {
        SKSE_LOG_ERROR("Failed to get messaging interface");
        return false;
    }

    if (!messagingInterface->RegisterListener(OnMessage)) {
        SKSE_LOG_ERROR("Failed to register message listener");
        return false;
    }

    SKSE_LOG_INFO(Plugin::NAME, "Loaded");
    return true;
}

extern "C" [[maybe_unused]]
// NOLINTNEXTLINE(readability-identifier-naming)
__declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version =
    []() {
        SKSE::PluginVersionData v;
        v.PluginVersion(Plugin::VERSION);
        v.PluginName(Plugin::NAME);
        v.AuthorName(Plugin::AUTHOR);
        v.UsesAddressLibrary();
        v.UsesUpdatedStructs();
        v.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST});
        v.MinimumRequiredXSEVersion(REL::Version{2, 3, 0});
        return v;
    }();