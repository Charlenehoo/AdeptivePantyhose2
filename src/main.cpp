#include "pch.h"  // IWYU pragma: keep

#include "Event/EventProcessor.h"
#include "Integration/SKEE/SKEEIntegration.h"

namespace {
void RegisterEvents() {
    auto& gameEvents = EventProcessor::GetSingleton();

    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESEquipEvent>(&gameEvents);
        holder->AddEventSink<RE::TESObjectLoadedEvent>(&gameEvents);
    } else {
        REX::WARN("RegisterEvents: ScriptEventSourceHolder null");
    }

    REX::INFO("RegisterEvents: done");
}

void OnMessage(SKSE::MessagingInterface::Message* a_msg) {
    if (a_msg == nullptr) {
        REX::WARN("OnMessage: Message is nullptr");
        return;
    }

    switch (a_msg->type) {
        case SKSE::MessagingInterface::kPostPostLoad: {
            REX::DEBUG("OnMessage: PostPostLoad");

            // 请求外部插件 API，此时所有 SKSE 插件的 Load 已完成
            SKEEIntegration::GetSingleton().Initialize();
            break;
        }
        case SKSE::MessagingInterface::kDataLoaded: {
            REX::DEBUG("OnMessage: DataLoaded");

            // 挂游戏事件，此时游戏数据与事件源已就绪
            RegisterEvents();
            break;
        }
        default:
            break;
    }
}
}  // namespace

// NOLINTBEGIN(readability-identifier-naming)
extern "C" [[maybe_unused]] __declspec(dllexport) auto SKSEPlugin_Load(const SKSE::LoadInterface* a_skse) -> bool {
    // NOLINTEND(readability-identifier-naming)
    if (a_skse == nullptr) {
        return false;
    }

    SKSE::Init(a_skse);

    const auto* messaging = SKSE::GetMessagingInterface();
    if (messaging == nullptr) {
        REX::CRITICAL("SKSEPlugin_Load: GetMessagingInterface returned nullptr");
        return false;
    }

    if (!messaging->RegisterListener(OnMessage)) {
        REX::CRITICAL("SKSEPlugin_Load: RegisterListener failed");
        return false;
    }

    REX::INFO("SKSEPlugin_Load: loaded");
    return true;
}

// NOLINTBEGIN(readability-identifier-naming)
extern "C" [[maybe_unused]] __declspec(dllexport) constinit SKSE::PluginVersionData SKSEPlugin_Version = []() noexcept {
    // NOLINTEND(readability-identifier-naming)
    SKSE::PluginVersionData data;
    data.PluginVersion(Plugin::kVersion);
    data.PluginName(Plugin::kName);
    data.AuthorName(Plugin::kAuthor);
    data.AuthorEmail(Plugin::kAuthorEmail);
    data.UsesAddressLibrary();
    data.UsesUpdatedStructs();
    data.CompatibleVersions({SKSE::RUNTIME_SSE_LATEST});
    data.MinimumRequiredXSEVersion(REL::Version{2, 3, 0});
    return data;
}();