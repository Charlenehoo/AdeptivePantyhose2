// src/EventProcessor/EventProcessor.cpp
#include "PCH.h"
#include "EventProcessor.h"

auto EventProcessor::GetSingleton() -> EventProcessor& {
    static EventProcessor s_instance;
    return s_instance;
}

auto EventProcessor::ProcessEvent(const RE::TESEquipEvent* ,
                                  RE::BSTEventSource<RE::TESEquipEvent>*)
    -> RE::BSEventNotifyControl {
    return RE::BSEventNotifyControl::kContinue;
}

auto EventProcessor::ProcessEvent(const RE::TESObjectLoadedEvent* ,
                                  RE::BSTEventSource<RE::TESObjectLoadedEvent>*)
    -> RE::BSEventNotifyControl {
    return RE::BSEventNotifyControl::kContinue;
}