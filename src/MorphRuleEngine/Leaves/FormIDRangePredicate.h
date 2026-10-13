// src/MorphRuleEngine/Leaves/FormIDRangePredicate.h
#pragma once

#include "MorphRuleEngine/CompileError.h"
#include "MorphRuleEngine/Predicate.h"

#include <expected>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>


namespace MorphRules::Engine {

// 匹配 armor 的来源插件名与 local FormID 是否落在给定范围内。
// 多条 Range 之间是 OR 关系。
class FormIDRangePredicate final : public Predicate {
public:
    struct Range {
        std::string plugin;
        RE::FormID min = 0;
        RE::FormID max = 0;
    };

    explicit FormIDRangePredicate(std::vector<Range> a_ranges) : m_ranges(std::move(a_ranges)) {}

    bool Evaluate(const PredicateContext& a_ctx) const override;

private:
    std::vector<Range> m_ranges;
};

// 解析 "ByFormIDRange" 载荷：{ Plugin, Min, Max } 对象数组，
// Min / Max 为十六进制字符串（如 "0xDDF"）。
auto MakeFormIDRangePredicate(const nlohmann::json& a_payload) -> std::expected<PredicatePtr, LeafError>;

}  // namespace MorphRules::Engine