// src/Integration/SKEE/SKEEIntegration.h
#pragma once

#include <string_view>

namespace RE {
class Actor;
class NiAVObject;
class TESObjectARMO;
class TESObjectARMA;
class TESObjectREFR;
}  // namespace RE

// SKEE IPluginInterface.h 里的类型位于全局命名空间。
class IBodyMorphInterface;
class IActorUpdateManager;

// SKEE BodyMorph 接口的封装。
//
// 对外提供语义化的操作，屏蔽 SKEE 接口的以下细节：
//   - 接口交换协议与版本检查
//   - 命名空间 key（本插件在 SKEE 系统中的唯一标识）
//   - SetMorph 的"相对变化量"语义——绝对量由 Clear + Set 组合实现
//
// SKEE 未安装或接口版本过旧时，所有操作静默 no-op。调用方不需要
// 处理接口指针为空的场景。
class SKEEIntegration {
public:
    ~SKEEIntegration() = default;
    SKEEIntegration(const SKEEIntegration&) = delete;
    SKEEIntegration(SKEEIntegration&&) = delete;
    auto operator=(const SKEEIntegration&) -> SKEEIntegration& = delete;
    auto operator=(SKEEIntegration&&) -> SKEEIntegration& = delete;

    static auto GetSingleton() -> SKEEIntegration&;

    // 在 kPostPostLoad 调用。主动向 SKEE 发起接口交换请求，
    // 只有 SKEE 的 listener 会响应。SKEE 未安装时只打 WARN，
    // 不影响插件其他功能。
    void Initialize();

    [[nodiscard]] auto IsAvailable() const noexcept -> bool { return m_bodyMorph != nullptr; }

    // ---------- 语义化操作 ----------

    // 把某 morph 设为绝对值。
    //
    // 内部处理 SKEE SetMorph 的相对语义：
    //   ClearMorph(key) → SetMorph(key, value)
    // value == 0.0 时只 ClearMorph，跳过 SetMorph。
    //
    // 同时清理旧版本的 legacy key，兼容从 CH_ 前缀版本升级的存档。
    //
    // 形参用引用而非指针：契约要求调用方传入真实 actor，类型系统直接
    // 表达这一点。调用方在事件入口已做过 null 过滤。
    void SetAbsolute(RE::Actor& a_actor, std::string_view a_morphName, float a_value);

    // 让引擎重新计算该 actor 的模型权重。
    // 建议在一次事件里对多个 morph 做完 SetAbsolute 之后调用一次。
    void UpdateModelWeight(RE::Actor& a_actor);

    // 仅对当前正在 attach 的 mesh 以 isAttaching=true 调用 ApplyVertexDiff。
    // 用于 SKEE 自身 OnAttach 未初始化当前 mesh 时的兜底。
    void ApplyVertexDiffCurrentMesh(RE::TESObjectREFR& a_refr, RE::NiAVObject& a_object);

    // 对 actor 当前所有已 attach 的 armor addon mesh 以 isAttaching=false 调用 ApplyVertexDiff。
    // a_attachingArmor / a_attachingAddon / a_isFirstPerson 用于跳过当前 mesh 并输出日志。
    void ApplyVertexDiffToWornMeshes(RE::Actor& a_actor,
                                     RE::TESObjectARMO* a_attachingArmor,
                                     RE::TESObjectARMA* a_attachingAddon,
                                     bool a_isFirstPerson);

private:
    SKEEIntegration() = default;

    IBodyMorphInterface* m_bodyMorph = nullptr;
    IActorUpdateManager* m_updateManager = nullptr;
};
