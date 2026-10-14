// src/main.cpp

#include "pch.h"  // IWYU pragma: keep

#include "Config/SyntaxParser.h"
#include "Event/EventProcessor.h"
#include "Integration/SKEE/SKEEIntegration.h"
#include "MorphRuleEngine/Leaves/FormIDRangePredicate.h"
#include "MorphRuleEngine/Leaves/KeywordPredicate.h"
#include "MorphRuleEngine/MorphRuleEngine.h"
#include "MorphRuleEngine/PredicateCompiler.h"
#include "Plugin.h"


namespace {

constexpr const char* kMorphRulesPath = "Data/SKSE/AdeptivePantyhose/MorphRules.json";

void LoadMorphRules() {
    using namespace MorphRules;

    auto parsed = Syntax::ParseFile(kMorphRulesPath);
    if (!parsed) {
        REX::FAIL("MorphRules.json syntax error at {}: {}", parsed.error().location, parsed.error().message);
        return;
    }

    Engine::PredicateCompiler compiler;
    compiler.RegisterLeaf("ByKeywords", &Engine::MakeKeywordPredicate);
    compiler.RegisterLeaf("ByFormIDRange", &Engine::MakeFormIDRangePredicate);

    auto compiled = compiler.Compile(*parsed);
    if (!compiled) {
        REX::FAIL("MorphRules.json compile error at {}: {}", compiled.error().location, compiled.error().message);
        return;
    }

    const auto morphCount = compiled->morphs.size();
    Engine::MorphRuleEngine::GetSingleton().SetRules(std::move(*compiled));
    REX::INFO("MorphRules loaded: {} morph(s)", morphCount);
}

void RegisterEvents() {
    auto& processor = EventProcessor::GetSingleton();

    if (auto* holder = RE::ScriptEventSourceHolder::GetSingleton()) {
        holder->AddEventSink<RE::TESEquipEvent>(&processor);
        REX::INFO("RegisterEvents: TESEquipEvent sink registered");
    } else {
        REX::WARN("RegisterEvents: ScriptEventSourceHolder null");
    }
}

void HandlePostLoadGame() {
    auto* player = RE::PlayerCharacter::GetSingleton();
    if (player == nullptr) {
        REX::WARN("HandlePostLoadGame: PlayerCharacter null");
        return;
    }

    MorphRules::Engine::MorphRuleEngine::GetSingleton().OnActorLoaded(player);
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

            // 加载配置 + 挂游戏事件，此时游戏数据与事件源已就绪
            LoadMorphRules();
            RegisterEvents();
            break;
        }
        case SKSE::MessagingInterface::kPostLoadGame: {
            REX::DEBUG("OnMessage: PostLoadGame");

            // 玩家读档后主动跑一遍规则，覆盖存档里已穿高跟的场景
            HandlePostLoadGame();
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
        REX::FAIL("SKSEPlugin_Load: GetMessagingInterface returned nullptr");
        return false;
    }

    if (!messaging->RegisterListener(OnMessage)) {
        REX::FAIL("SKSEPlugin_Load: RegisterListener failed");
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