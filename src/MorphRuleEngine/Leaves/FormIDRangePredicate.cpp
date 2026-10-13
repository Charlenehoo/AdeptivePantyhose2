// src/MorphRuleEngine/Leaves/FormIDRangePredicate.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/Leaves/FormIDRangePredicate.h"

#include <charconv>
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

namespace MorphRules::Engine {

namespace {

// 解析 "0x1234" 形式的十六进制字符串。std::from_chars 不识别 0x 前缀，
// 手动剥离。
auto ParseHexFormID(std::string_view a_text, std::string_view a_path, std::string_view a_field)
    -> std::expected<RE::FormID, LeafError> {
    if (a_text.empty()) {
        return std::unexpected(LeafError{std::string(a_path), std::format("'{}' must not be empty", a_field)});
    }

    if (a_text.size() >= 2 && a_text[0] == '0' && (a_text[1] == 'x' || a_text[1] == 'X')) {
        a_text.remove_prefix(2);
    }

    if (a_text.empty()) {
        return std::unexpected(LeafError{
            std::string(a_path),
            std::format("'{}' must contain hex digits after the '0x' prefix", a_field),
        });
    }

    std::uint32_t value = 0;
    const auto result = std::from_chars(a_text.data(), a_text.data() + a_text.size(), value, 16);

    if (result.ec != std::errc{} || result.ptr != a_text.data() + a_text.size()) {
        return std::unexpected(LeafError{
            std::string(a_path),
            std::format("'{}' must be a hexadecimal string (e.g. \"0xDDF\")", a_field),
        });
    }

    return static_cast<RE::FormID>(value);
}

auto ParseRange(const nlohmann::json& a_json, std::size_t a_index)
    -> std::expected<FormIDRangePredicate::Range, LeafError> {
    const auto basePath = std::format("ByFormIDRange[{}]", a_index);

    if (!a_json.is_object()) {
        return std::unexpected(LeafError{basePath, "must be an object"});
    }

    if (!a_json.contains("Plugin") || !a_json.contains("Min") || !a_json.contains("Max")) {
        return std::unexpected(LeafError{basePath, "missing one of required fields 'Plugin' / 'Min' / 'Max'"});
    }

    // Plugin
    const auto& pluginJson = a_json.at("Plugin");
    if (!pluginJson.is_string()) {
        return std::unexpected(LeafError{std::format("{}.Plugin", basePath), "must be a string"});
    }
    auto plugin = pluginJson.get<std::string>();
    if (plugin.empty()) {
        return std::unexpected(LeafError{std::format("{}.Plugin", basePath), "must not be empty"});
    }

    // Min
    const auto& minJson = a_json.at("Min");
    if (!minJson.is_string()) {
        return std::unexpected(LeafError{std::format("{}.Min", basePath), "must be a hex string (e.g. \"0xDDF\")"});
    }
    auto minResult = ParseHexFormID(minJson.get<std::string>(), std::format("{}.Min", basePath), "Min");
    if (!minResult) {
        return std::unexpected(std::move(minResult.error()));
    }

    // Max
    const auto& maxJson = a_json.at("Max");
    if (!maxJson.is_string()) {
        return std::unexpected(LeafError{std::format("{}.Max", basePath), "must be a hex string (e.g. \"0xDDF\")"});
    }
    auto maxResult = ParseHexFormID(maxJson.get<std::string>(), std::format("{}.Max", basePath), "Max");
    if (!maxResult) {
        return std::unexpected(std::move(maxResult.error()));
    }

    if (*maxResult < *minResult) {
        return std::unexpected(LeafError{
            basePath,
            std::format("Max ({:#x}) must be >= Min ({:#x})", *maxResult, *minResult),
        });
    }

    FormIDRangePredicate::Range range;
    range.plugin = std::move(plugin);
    range.min = *minResult;
    range.max = *maxResult;
    return range;
}

}  // namespace

bool FormIDRangePredicate::Evaluate(const PredicateContext& a_ctx) const {
    if (a_ctx.armor == nullptr) {
        return false;
    }

    const RE::TESFile* file = a_ctx.armor->GetFile(0);
    if (file == nullptr) {
        return false;
    }

    const std::string_view fileName = file->GetFilename();
    const RE::FormID localID = a_ctx.armor->GetLocalFormID();

    for (const auto& range : m_ranges) {
        if (fileName == range.plugin && localID >= range.min && localID <= range.max) {
            return true;
        }
    }
    return false;
}

auto MakeFormIDRangePredicate(const nlohmann::json& a_payload) -> std::expected<PredicatePtr, LeafError> {
    if (!a_payload.is_array()) {
        return std::unexpected(LeafError{"ByFormIDRange", "must be an array of range objects"});
    }
    if (a_payload.empty()) {
        return std::unexpected(LeafError{"ByFormIDRange", "must not be empty"});
    }

    std::vector<FormIDRangePredicate::Range> ranges;
    ranges.reserve(a_payload.size());

    for (std::size_t i = 0; i < a_payload.size(); ++i) {
        auto range = ParseRange(a_payload.at(i), i);
        if (!range) {
            return std::unexpected(std::move(range.error()));
        }
        ranges.push_back(std::move(*range));
    }

    return std::make_shared<FormIDRangePredicate>(std::move(ranges));
}

}  // namespace MorphRules::Engine