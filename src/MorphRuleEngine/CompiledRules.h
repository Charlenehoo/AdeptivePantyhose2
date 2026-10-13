// src/MorphRuleEngine/CompiledRules.h
#pragma once

#include "MorphRuleEngine/Predicate.h"

#include <RE/B/BGSBipedObjectForm.h>
#include <string>
#include <unordered_map>
#include <vector>


namespace MorphRules::Engine {

// 一条 morph 规则：满足 when 时把 morph 设为 value。
struct CompiledMorphEntry {
    float value = 1.0F;
    PredicatePtr when;
};

using CompiledMorphRuleList = std::vector<CompiledMorphEntry>;

struct CompiledMorph {
    RE::BGSBipedObjectForm::BipedObjectSlot triggeredBy{};
    CompiledMorphRuleList rules;
};

// 编译产物：只有 morphs，因为谓词已内联为指针，不需要单独保留名字表。
struct CompiledRules {
    std::unordered_map<std::string, CompiledMorph> morphs;
};

}  // namespace MorphRules::Engine