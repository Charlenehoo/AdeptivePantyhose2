// src/Integration/SKEE/SKEEIntegration.cpp

#include "pch.h"  // IWYU pragma: keep

#include "Integration/SKEE/SKEEIntegration.h"

#include "Integration/SKEE/AttachListener.h"

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
constexpr skee_u32 kBodyMorphMinVersion = 4;

// 本插件在 SKEE BodyMorph 系统中的命名空间 key。
// 用于隔离本插件设置的 morph，避免与其他插件冲突。
// 修改此值会让已存档的 morph 状态失效。
//
// 变量名不带 "Morph"：SKEE 的 SetMorph / ClearMorph 第二形参是
// morphName、第三是 morphKey，两者相邻且都是 const char*。若常量名
// 含 "Morph"，clang-tidy 的 readability-suspicious-call-argument
// 会把实参名与 morphName 形参做词根匹配，误报"可能传反"。
constexpr const char* kNamespaceKey = "AdeptivePantyhoseMorphKey";

// 旧版本使用的 key。每次 SetAbsolute 时顺手清理，兼容从 CH_ 前缀
// 版本升级的存档，避免老值和新值叠加。一次性兼容代码，足够久之后
// 可以移除。
constexpr const char* kLegacyNamespaceKey = "CH_AdeptivePantyhoseMorphKey";

}  // namespace

namespace {

[[nodiscard]] auto HasShapeData(RE::NiAVObject* a_object) -> bool {
    if (a_object == nullptr) {
        return false;
    }
    return a_object->GetExtraData("SHAPEDATA") != nullptr;
}

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

    InterfaceExchangeMessage exchange{};
    messaging->Dispatch(static_cast<std::uint32_t>(InterfaceExchangeMessage::kMessage_ExchangeInterface),
                        &exchange,
                        sizeof(exchange),
                        kSKEEPluginName);

    if (exchange.interfaceMap == nullptr) {
        REX::WARN("SKEEIntegration: SKEE not available, integration disabled");
        return;
    }

    // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast)
    auto* bodyMorph = static_cast<IBodyMorphInterface*>(exchange.interfaceMap->QueryInterface(kBodyMorphInterfaceName));
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

    // NOLINTBEGIN(cppcoreguidelines-pro-type-static-cast-downcast)
    auto* updateManager =
        static_cast<IActorUpdateManager*>(exchange.interfaceMap->QueryInterface("ActorUpdateManager"));
    // NOLINTEND(cppcoreguidelines-pro-type-static-cast-downcast)
    if (updateManager != nullptr) {
        m_updateManager = updateManager;
        updateManager->AddInterface(&AttachListener::GetSingleton());
        REX::INFO("SKEEIntegration: ActorUpdateManager found, AttachListener installed");
    } else {
        REX::WARN("SKEEIntegration: ActorUpdateManager not found, AttachListener not installed");
    }
}

// ---------- 语义化操作 ----------

void SKEEIntegration::SetAbsolute(RE::Actor& a_actor, std::string_view a_morphName, float a_value) {
    if (m_bodyMorph == nullptr) {
        // 用户"日志里规则命中了但没效果"的常见根因。SKEE 未装或版本
        // 过旧时 Initialize 已打过 WARN，这里再标一次是让日志顺序上
        // 紧跟在规则命中之后，不用翻回加载期找。
        REX::DEBUG("    [SKEE] SetAbsolute: interface null, skip morph='{}'", a_morphName);
        return;
    }

    REX::DEBUG("    [SKEE] SetAbsolute actor={:08X} morph='{}' value={}", a_actor.GetFormID(), a_morphName, a_value);

    // std::string_view 不保证 null 终止，转 string 之后取 c_str()。
    const std::string morphName{a_morphName};
    const char* morphNameCStr = morphName.c_str();

    // 旧版遗留 key 清理。幂等——该 key 下没有记录时 ClearMorph 是空调用。
    m_bodyMorph->ClearMorph(&a_actor, morphNameCStr, kLegacyNamespaceKey);

    // SKEE 的 SetMorph 是相对变化量，要得到绝对值 v 必须先 Clear 再 Set。
    m_bodyMorph->ClearMorph(&a_actor, morphNameCStr, kNamespaceKey);
    if (a_value != 0.0F) {
        m_bodyMorph->SetMorph(&a_actor, morphNameCStr, kNamespaceKey, a_value);
    }
}

void SKEEIntegration::UpdateModelWeight(RE::Actor& a_actor) {
    if (m_bodyMorph == nullptr) {
        REX::DEBUG("    [SKEE] UpdateModelWeight: interface null, skip actor={:08X}", a_actor.GetFormID());
        return;
    }

    REX::DEBUG("    [SKEE] UpdateModelWeight actor={:08X}", a_actor.GetFormID());
    m_bodyMorph->UpdateModelWeight(&a_actor);
}

void SKEEIntegration::ApplyVertexDiffCurrentMesh(RE::TESObjectREFR& a_refr, RE::NiAVObject& a_object) {
    if (m_bodyMorph == nullptr) {
        return;
    }
    REX::DEBUG("[ApplyCurrent] refr={:08X} mesh='{}'", a_refr.GetFormID(), a_object.name.c_str());
    m_bodyMorph->ApplyVertexDiff(&a_refr, &a_object, true);
}

void SKEEIntegration::ApplyVertexDiffToWornMeshes(RE::Actor& a_actor,
                                                  RE::TESObjectARMO* a_attachingArmor,
                                                  RE::TESObjectARMA* a_attachingAddon,
                                                  bool a_isFirstPerson) {
    if (m_bodyMorph == nullptr) {
        return;
    }

    for (std::uint32_t k = 0; k <= 1; ++k) {
        auto weightModel = a_actor.GetBiped(k);
        if (!weightModel) {
            continue;
        }

        const bool isFirstPerson = (k == 1);

        auto processArray = [&](auto& a_array) {
            for (std::uint32_t i = 0; i < RE::BIPED_OBJECTS::kTotal; ++i) {
                auto& data = a_array[i];
                if (!data.partClone) {
                    continue;
                }

                auto* armor = data.item ? data.item->template As<RE::TESObjectARMO>() : nullptr;
                auto* addon = data.addon;

                const bool isAttaching =
                    (armor != nullptr && a_attachingArmor != nullptr &&
                     armor->GetFormID() == a_attachingArmor->GetFormID() && addon != nullptr &&
                     a_attachingAddon != nullptr && addon->GetFormID() == a_attachingAddon->GetFormID() &&
                     isFirstPerson == a_isFirstPerson);

                if (isAttaching) {
                    REX::DEBUG("[ApplyOther] SKIP current mesh refr={:08X} mesh='{}' isFP={}",
                               a_actor.GetFormID(),
                               data.partClone->name.c_str(),
                               isFirstPerson);
                    continue;
                }

                const bool hasShapeData = HasShapeData(data.partClone.get());
                REX::DEBUG("[ApplyOther] refr={:08X} mesh='{}' armor={:08X} addon={:08X} isFP={} hasShapeData={}",
                           a_actor.GetFormID(),
                           data.partClone->name.c_str(),
                           armor != nullptr ? armor->GetFormID() : 0,
                           addon != nullptr ? addon->GetFormID() : 0,
                           isFirstPerson,
                           hasShapeData);

                if (!hasShapeData) {
                    REX::DEBUG("[ApplyOther] mesh='{}' has no SHAPEDATA, skip (will be handled by its own OnAttach)",
                               data.partClone->name.c_str());
                    continue;
                }
                m_bodyMorph->ApplyVertexDiff(&a_actor, data.partClone.get(), false);
            }
        };

        processArray(weightModel->objects);
        processArray(weightModel->bufferedObjects);
    }
}
