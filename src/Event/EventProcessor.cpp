// src/Event/EventProcessor.cpp

#include "pch.h"  // IWYU pragma: keep

#include "Event/EventProcessor.h"

#include "MorphRuleEngine/MorphRuleEngine.h"
#include "Support/FormName.h"

#include <cstdint>
#include <string_view>

auto EventProcessor::GetSingleton() -> EventProcessor& {
    static EventProcessor s_singleton;
    return s_singleton;
}

auto EventProcessor::ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* /*a_source*/)
    -> RE::BSEventNotifyControl {
    if (a_event == nullptr) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* actor = a_event->actor != nullptr ? a_event->actor->As<RE::Actor>() : nullptr;
    if (actor == nullptr) {
        return RE::BSEventNotifyControl::kContinue;
    }

    auto* form = a_event->baseObject != 0 ? RE::TESForm::LookupByID(a_event->baseObject) : nullptr;
    auto* armor = form != nullptr ? form->As<RE::TESObjectARMO>() : nullptr;
    if (armor == nullptr) {
        return RE::BSEventNotifyControl::kContinue;
    }

    // 事件入口日志。用户"装备没反应"的定位第一步：日志里有没有这条。
    // 没有 → 引擎没触发 equip 事件（脚本改装备等 edge case）。
    const RE::TESFile* file = armor->GetFile(0);
    const std::string_view fileName =
        file != nullptr ? std::string_view{file->GetFilename()} : std::string_view{"<null>"};

    LOG_DEBUG("[Equip] actor='{}'({:08X}) armor='{}'({:08X}) equipped={}",
               Support::SafeName(actor),
               actor->GetFormID(),
               Support::SafeName(armor),
               armor->GetFormID(),
               a_event->equipped);
    LOG_DEBUG("        file='{}' local={:08X} slotMask={:08X}",
               fileName,
               armor->GetLocalFormID(),
               static_cast<std::uint32_t>(armor->GetSlotMask()));

    MorphRules::Engine::MorphRuleEngine::GetSingleton().OnEquipEvent(actor, armor, a_event->equipped);

    return RE::BSEventNotifyControl::kContinue;
}