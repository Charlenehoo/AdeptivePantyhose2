// src/Event/EventProcessor.cpp

#include "pch.h"  // IWYU pragma: keep

#include "Event/EventProcessor.h"

#include "MorphRuleEngine/MorphRuleEngine.h"

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

    MorphRules::Engine::MorphRuleEngine::GetSingleton().OnEquipEvent(actor, armor, a_event->equipped);

    return RE::BSEventNotifyControl::kContinue;
}