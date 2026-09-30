#include "PCH.h"

#include "BodyMorphManager/BodyMorphManager.h"
#include "EventProcessor/EventProcessor.h"

namespace {

void OnPostPostLoad() {
    SKSE_LOG_TRACE("OnPostPostLoad - initializing BodyMorphManager");

    if (!BodyMorphManager::GetSingleton().Init()) {
        SKSE_LOG_ERROR(
            "OnPostPostLoad - BodyMorphManager init failed; plugin will be "
            "inactive");
    }
}

void OnDataLoaded() {
    if (!BodyMorphManager::GetSingleton().IsReady()) {
        SKSE_LOG_WARN(
            "OnDataLoaded - BodyMorphManager not ready; skipping event sinks");
        return;
    }

    SKSE_LOG_TRACE("OnDataLoaded - registering event sinks");

    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    if (!holder) {
        SKSE_LOG_ERROR("OnDataLoaded - ScriptEventSourceHolder unavailable");
        return;
    }

    auto& processor = EventProcessor::GetSingleton();
    holder->AddEventSink<RE::TESEquipEvent>(&processor);
    holder->AddEventSink<RE::TESObjectLoadedEvent>(&processor);

    SKSE_LOG_INFO("OnDataLoaded - event sinks registered");
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

    SKSE_LOG_TRACE("SKSEPlugin_Load - getting messaging interface");
    auto* messaging = SKSE::GetMessagingInterface();
    if (!messaging) {
        SKSE_LOG_ERROR("SKSEPlugin_Load - messaging interface unavailable");
        return false;
    }

    SKSE_LOG_TRACE("SKSEPlugin_Load - registering message listener");
    if (!messaging->RegisterListener(OnMessage)) {
        SKSE_LOG_ERROR("SKSEPlugin_Load - failed to register message listener");
        return false;
    }

    SKSE_LOG_INFO("SKSEPlugin_Load - plugin loaded");
    return true;
}

// NOLINTBEGIN(readability-identifier-naming)
extern "C" [[maybe_unused]]
__declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version =
    []() {
        SKSE::PluginVersionData versionData;
        versionData.PluginVersion(Plugin::VERSION);
        versionData.PluginName(Plugin::NAME);
        versionData.AuthorName(Plugin::AUTHOR);
        versionData.UsesAddressLibrary();
        versionData.UsesUpdatedStructs();
        versionData.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST});
        versionData.MinimumRequiredXSEVersion(REL::Version{2, 3, 0});
        return versionData;
    }();
// NOLINTEND(readability-identifier-naming)