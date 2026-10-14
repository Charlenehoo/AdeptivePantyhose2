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

#include <cstddef>
#include <string>

namespace {

constexpr const char* kMorphRulesPath = "Data/SKSE/AdeptivePantyhose/MorphRules.json";

// 把解析出来的规则打到 DEBUG 日志。用户报告"某个鞋没识别"时，
// 一眼就能确认规则有没有被正确解析——比追问"你 JSON 怎么写的"高效。
void DumpParsedConfig(const MorphRules::Syntax::ParsedConfig& a_config) {
    using MorphRules::Syntax::PredicateKind;

    REX::DEBUG("[Rules] parsed: {} predicate(s), {} morph(s)", a_config.predicates.size(), a_config.morphs.size());

    for (const auto& [name, pred] : a_config.predicates) {
        switch (pred.kind) {
            case PredicateKind::kLeaf: {
                std::string payload = pred.leafPayload.dump();
                constexpr std::size_t kMaxPayloadLen = 200;
                if (payload.size() > kMaxPayloadLen) {
                    payload.resize(kMaxPayloadLen);
                    payload += "...";
                }
                REX::DEBUG("  predicate '{}': leaf {}({})", name, pred.leafType, payload);
                break;
            }
            case PredicateKind::kAnyOf:
            case PredicateKind::kAllOf: {
                std::string joined;
                for (std::size_t i = 0; i < pred.refs.size(); ++i) {
                    if (i > 0) {
                        joined += ", ";
                    }
                    joined += pred.refs.at(i);
                }
                const char* kind = pred.kind == PredicateKind::kAnyOf ? "AnyOf" : "AllOf";
                REX::DEBUG("  predicate '{}': {}[{}]", name, kind, joined);
                break;
            }
            case PredicateKind::kNot:
                REX::DEBUG("  predicate '{}': Not({})", name, pred.ref);
                break;
        }
    }

    for (const auto& [name, morph] : a_config.morphs) {
        REX::DEBUG("  morph '{}' triggeredBy='{}' rules={}", name, morph.triggeredBy, morph.rules.size());
        for (std::size_t i = 0; i < morph.rules.size(); ++i) {
            REX::DEBUG("    rule[{}] value={} when='{}'", i, morph.rules.at(i).value, morph.rules.at(i).whenRef);
        }
    }
}

void LoadMorphRules() {
    using namespace MorphRules;

    auto parsed = Syntax::ParseFile(kMorphRulesPath);
    if (!parsed) {
        REX::FAIL("MorphRules.json syntax error at {}: {}", parsed.error().location, parsed.error().message);
        return;
    }

    DumpParsedConfig(*parsed);

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

    // INFO 级：读档兜底是唯一走 OnActorLoaded 的路径，日志里出现
    // 说明读档场景确实触发了。用户报告"读档后没生效"时先看这行在不在。
    REX::INFO("[PostLoad] running rules for player '{}'({:08X})", player->GetName(), player->GetFormID());
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

    // 版本信息走 INFO：用户报告问题时附带的日志会包含这几行，
    // 维护者一眼看到游戏 / SKSE / 插件版本是否匹配，不用追问。
    // API 参照 powerofthree's Tweaks：都从 LoadInterface 取。
    REX::INFO("Plugin version: {}", Plugin::kVersion);
    REX::INFO("SKSE version: {}", a_skse->SKSEVersion());
    REX::INFO("Game version: {}", a_skse->RuntimeVersion());

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

#ifdef SKYRIM_SUPPORT_AE
// AE 版本：使用 PluginVersionData
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
#else
// SE 1.5.97：传统 Query 方式
// NOLINTBEGIN(readability-identifier-naming)
extern "C" [[maybe_unused]] __declspec(dllexport) bool SKSEPlugin_Query(const SKSE::QueryInterface* a_skse,
                                                                        SKSE::PluginInfo* a_info) {
    // NOLINTEND(readability-identifier-naming)
    a_info->infoVersion = SKSE::PluginInfo::kVersion;
    a_info->name = Plugin::kName.data();
    a_info->version = Plugin::kVersion.pack();

    if (a_skse->IsEditor()) {
        REX::CRITICAL("Loaded in editor, marking as incompatible");
        return false;
    }

    const auto ver = a_skse->RuntimeVersion();
    if (ver < SKSE::RUNTIME_SSE_1_5_39) {
        REX::CRITICAL("Unsupported runtime version {}", ver);
        return false;
    }

    return true;
}
#endif