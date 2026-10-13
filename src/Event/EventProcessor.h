// src/Event/EventProcessor.h
#pragma once

#include <RE/B/BSTEvent.h>
#include <RE/T/TESEquipEvent.h>

class EventProcessor : public RE::BSTEventSink<RE::TESEquipEvent> {
public:
    ~EventProcessor() = default;
    EventProcessor(const EventProcessor&) = delete;
    EventProcessor(EventProcessor&&) = delete;
    auto operator=(const EventProcessor&) -> EventProcessor& = delete;
    auto operator=(EventProcessor&&) -> EventProcessor& = delete;

    static auto GetSingleton() -> EventProcessor&;

    auto ProcessEvent(const RE::TESEquipEvent* a_event, RE::BSTEventSource<RE::TESEquipEvent>* a_source)
        -> RE::BSEventNotifyControl override;

private:
    EventProcessor() = default;
};