#include "PCH.h"

#include "EventProcessor/EventProcessor.h"

SKSE_PLUGIN_LOAD(const SKSE::LoadInterface* skse) {
    SKSE::Init(skse);

    auto& processor = EventProcessor::GetSingleton();
    auto* holder = RE::ScriptEventSourceHolder::GetSingleton();
    holder->AddEventSink<RE::TESEquipEvent>(&processor);
    holder->AddEventSink<RE::TESObjectLoadedEvent>(&processor);

    return true;
}