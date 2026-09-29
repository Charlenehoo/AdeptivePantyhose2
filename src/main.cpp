#include "PCH.h"

#include "BodyMorphManager/BodyMorphManager.h"
#include "EventProcessor/EventProcessor.h"

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

#ifdef NDEBUG
constexpr REX::ELogLevel kLogLevel = REX::ELogLevel::Info;
#else
constexpr REX::ELogLevel kLogLevel = REX::ELogLevel::Trace;
#endif

}  // namespace

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse, SKSE::InitInfo{
                         .log = true,
                         .logLevel = kLogLevel,
                         .logName = "AdeptivePantyhose",
                         .logRotate = 5 * 1024 * 1024,
                         .trampoline = false,
                         .trampolineSize = 0,
                         .hook = true,
                     });

    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE_LOG_CRITICAL("Failed to get messaging interface");
        return false;
    }

    if (!messaging->RegisterListener(OnMessage)) {
        SKSE_LOG_CRITICAL("Failed to register message listener");
        return false;
    }

    SKSE_LOG_INFO("{} v{} loaded", Plugin::NAME, Plugin::VERSION.string());
    return true;
}

SKSE_PLUGIN_VERSION = []() {
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