// src/MorphRuleEngine/Leaves/KeywordPredicate.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/Leaves/KeywordPredicate.h"

#include <format>
#include <nlohmann/json.hpp>

namespace MorphRules::Engine {

bool KeywordPredicate::Evaluate(const PredicateContext& a_ctx) const {
    if (a_ctx.armor == nullptr) {
        return false;
    }
    for (const auto& kw : m_keywords) {
        if (a_ctx.armor->HasKeywordString(kw)) {
            return true;
        }
    }
    return false;
}

auto MakeKeywordPredicate(const nlohmann::json& a_payload) -> std::expected<PredicatePtr, LeafError> {
    if (!a_payload.is_array()) {
        return std::unexpected(LeafError{"ByKeywords", "must be an array of strings"});
    }
    if (a_payload.empty()) {
        return std::unexpected(LeafError{"ByKeywords", "must not be empty"});
    }

    std::vector<std::string> keywords;
    keywords.reserve(a_payload.size());

    for (std::size_t i = 0; i < a_payload.size(); ++i) {
        const auto& item = a_payload.at(i);
        const auto itemPath = std::format("ByKeywords[{}]", i);

        if (!item.is_string()) {
            return std::unexpected(LeafError{itemPath, "keyword must be a string"});
        }
        auto kw = item.get<std::string>();
        if (kw.empty()) {
            return std::unexpected(LeafError{itemPath, "keyword must not be empty"});
        }
        keywords.push_back(std::move(kw));
    }

    return std::make_shared<KeywordPredicate>(std::move(keywords));
}

}  // namespace MorphRules::Engine