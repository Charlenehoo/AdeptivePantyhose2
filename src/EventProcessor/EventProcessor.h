// src/EventProcessor/EventProcessor.h
#pragma once

class IEquipmentEffect;

class EventProcessor : public RE::BSTEventSink<RE::TESEquipEvent>,
                       public RE::BSTEventSink<RE::TESObjectLoadedEvent> {
   public:
    EventProcessor(const EventProcessor&) = delete;
    EventProcessor(EventProcessor&&) = delete;
    auto operator=(const EventProcessor&) -> EventProcessor& = delete;
    auto operator=(EventProcessor&&) -> EventProcessor& = delete;

    static auto GetSingleton() -> EventProcessor&;

    auto ProcessEvent(const RE::TESEquipEvent* event,
                      RE::BSTEventSource<RE::TESEquipEvent>* source)
        -> RE::BSEventNotifyControl override;
    auto ProcessEvent(const RE::TESObjectLoadedEvent* event,
                      RE::BSTEventSource<RE::TESObjectLoadedEvent>* source)
        -> RE::BSEventNotifyControl override;

   private:
    EventProcessor() = default;
    ~EventProcessor() = default;
};