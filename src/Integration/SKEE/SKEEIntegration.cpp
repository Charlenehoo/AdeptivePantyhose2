// src/Integration/SKEE/SKEEIntegration.cpp

#include "pch.h"  // IWYU pragma: keep

#include "Integration/SKEE/SKEEIntegration.h"

#include <IPluginInterface.h>
#include <cstdint>
#include <string>

namespace {

// SKEE 内部注册名，非 C++ 类名。换 RaceMenu 版本若接口取不到，
// 优先核对这里的字符串。
constexpr const char* kSKEEPluginName = "SKEE";
constexpr const char* kBodyMorphInterfaceName = "BodyMorph";

// 我们目前用到的方法的最低版本要求。上游 BodyMorph 接口当前最新为
// kPluginVersion5，取 4 留余量，后续按需调整。
constexpr SKEE::skee_u32 kBodyMorphMinVersion = 4;

// 本插件在 SKEE BodyMorph 系统中的命名空间 key。
// 用于隔离本插件设置的 morph，避免与其他插件冲突。
// 修改此值会让已存档的 morph 状态失效。
constexpr const char* kMorphNamespace = "AdeptivePantyhoseMorphKey";

// 旧版本使用的 key。每次 SetAbsolute 时顺手清理，兼容从 CH_ 前缀
// 版本升级的存档，避免老值和新值叠加。一次性兼容代码，足够久之后
// 可以移除。
constexpr const char* kLegacyMorphNamespace = "CH_AdeptivePantyhoseMorphKey";

}  // namespace

auto SKEEIntegration::GetSingleton() -> SKEEIntegration& {
    static SKEEIntegration s_singleton;
    return s_singleton;
}

void SKEEIntegration::Initialize() {
    const auto* messaging = SKSE::GetMessagingInterface();
    if (messaging == nullptr) {
        REX::WARN("SKEEIntegration: GetMessagingInterface returned nullptr");
        return;
    }

    SKEE::InterfaceExchangeMessage exchange{};
    messaging->Dispatch(static_cast<std::uint32_t>(SKEE::InterfaceExchangeMessage::kMessage_ExchangeInterface),
                        &exchange,
                        sizeof(exchange),
                        kSKEEPluginName);

    if (exchange.interfaceMap == nullptr) {
        REX::WARN("SKEEIntegration: SKEE not available, integration disabled");
        return;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast)
    auto* bodyMorph =
        static_cast<SKEE::IBodyMorphInterface*>(exchange.interfaceMap->QueryInterface(kBodyMorphInterfaceName));
    // NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast)
    // SKEE 接口查询协议，name→type 契约确定，跨 DLL 无 RTTI
    if (bodyMorph == nullptr) {
        REX::WARN("SKEEIntegration: BodyMorph interface not found");
        return;
    }

    const auto version = bodyMorph->GetVersion();
    if (version < kBodyMorphMinVersion) {
        REX::WARN("SKEEIntegration: BodyMorph version too old (need >= {}, got {})", kBodyMorphMinVersion, version);
        return;
    }

    m_bodyMorph = bodyMorph;
    REX::INFO("SKEEIntegration: initialized (bodyMorphVersion={})", version);
}

// ---------- 语义化操作 ----------

void SKEEIntegration::SetAbsolute(RE::Actor* a_actor, std::string_view a_morphName, float a_value) {
    if (m_bodyMorph == nullptr) {
        return;
    }

    // std::string_view 不保证 null 终止，转 string 之后取 c_str()。
    const std::string name{a_morphName};
    const char* cstr = name.c_str();

    // 旧版遗留 key 清理。幂等——该 key 下没有记录时 ClearMorph 是空调用。
    m_bodyMorph->ClearMorph(a_actor, cstr, kLegacyMorphNamespace);

    // SKEE 的 SetMorph 是相对变化量，要得到绝对值 v 必须先 Clear 再 Set。
    m_bodyMorph->ClearMorph(a_actor, cstr, kMorphNamespace);
    if (a_value != 0.0F) {
        m_bodyMorph->SetMorph(a_actor, cstr, kMorphNamespace, a_value);
    }
}

void SKEEIntegration::UpdateModelWeight(RE::Actor* a_actor) {
    if (m_bodyMorph == nullptr) {
        return;
    }
    m_bodyMorph->UpdateModelWeight(a_actor);
}