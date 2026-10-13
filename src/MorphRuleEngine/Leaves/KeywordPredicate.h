// src/MorphRuleEngine/Leaves/KeywordPredicate.h
#pragma once

#include "MorphRuleEngine/CompileError.h"
#include "MorphRuleEngine/Predicate.h"

#include <expected>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <vector>


namespace MorphRules::Engine {

// 匹配 armor 是否拥有任意一个关键字。
class KeywordPredicate final : public Predicate {
public:
    explicit KeywordPredicate(std::vector<std::string> a_keywords) : m_keywords(std::move(a_keywords)) {}

    bool Evaluate(const PredicateContext& a_ctx) const override;

private:
    std::vector<std::string> m_keywords;
};

// 解析 "ByKeywords" 载荷：JSON 字符串数组。
auto MakeKeywordPredicate(const nlohmann::json& a_payload) -> std::expected<PredicatePtr, LeafError>;

}  // namespace MorphRules::Engine