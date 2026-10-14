// src/MorphRuleEngine/Leaves/FormIDRangePredicate.cpp

#include "pch.h"  // IWYU pragma: keep

#include "MorphRuleEngine/Leaves/FormIDRangePredicate.h"

#include <absl/strings/numbers.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <format>
#include <nlohmann/json.hpp>
#include <string_view>

namespace MorphRules::Engine {

namespace {

// 解析 "0x1234" 形式的十六进制字符串。a_path 是出错的 JSON 路径，
// 直接用作 LeafError::location，不额外接收字段名——字段身份已包含在
// path 尾部（如 "ByFormIDRange[0].Min"），错误消息里重复一份纯属冗余。
//
// 用 absl::SimpleHexAtoi 而非 std::from_chars：后者不识别 0x / 0X
// 前缀，遇到 "0xDDF" 会在 'x' 处停下并"成功"返回 0；要支持前缀得手
// 动剥离。absl::SimpleHexAtoi 按文档忽略可选的 0x / 0X 前缀，且拒绝
// 尾随垃圾，语义正好。
auto ParseHexFormID(std::string_view a_text, std::string a_path) -> std::expected<RE::FormID, LeafError> {
    if (a_text.empty()) {
        return std::unexpected(LeafError{
            .location = std::move(a_path),
            .message = "must not be empty",
        });
    }

    std::uint32_t value = 0;
    if (!absl::SimpleHexAtoi(a_text, &value)) {
        return std::unexpected(LeafError{
            .location = std::move(a_path),
            .message = "must be a hexadecimal string (e.g. \"0xDDF\")",
        });
    }

    return static_cast<RE::FormID>(value);
}

auto ParseRange(const nlohmann::json& a_json, std::size_t a_index)
    -> std::expected<FormIDRangePredicate::Range, LeafError> {
    const auto basePath = std::format("ByFormIDRange[{}]", a_index);

    if (!a_json.is_object()) {
        return std::unexpected(LeafError{
            .location = basePath,
            .message = "must be an object",
        });
    }

    if (!a_json.contains("Plugin") || !a_json.contains("Min") || !a_json.contains("Max")) {
        return std::unexpected(LeafError{
            .location = basePath,
            .message = "missing one of required fields 'Plugin' / 'Min' / 'Max'",
        });
    }

    // Plugin
    const auto& pluginJson = a_json.at("Plugin");
    if (!pluginJson.is_string()) {
        return std::unexpected(LeafError{
            .location = std::format("{}.Plugin", basePath),
            .message = "must be a string",
        });
    }
    auto plugin = pluginJson.get<std::string>();
    if (plugin.empty()) {
        return std::unexpected(LeafError{
            .location = std::format("{}.Plugin", basePath),
            .message = "must not be empty",
        });
    }

    // Min
    const auto& minJson = a_json.at("Min");
    if (!minJson.is_string()) {
        return std::unexpected(LeafError{
            .location = std::format("{}.Min", basePath),
            .message = "must be a hex string (e.g. \"0xDDF\")",
        });
    }
    auto minResult = ParseHexFormID(minJson.get<std::string>(), std::format("{}.Min", basePath));
    if (!minResult) {
        return std::unexpected(std::move(minResult.error()));
    }

    // Max
    const auto& maxJson = a_json.at("Max");
    if (!maxJson.is_string()) {
        return std::unexpected(LeafError{
            .location = std::format("{}.Max", basePath),
            .message = "must be a hex string (e.g. \"0xDDF\")",
        });
    }
    auto maxResult = ParseHexFormID(maxJson.get<std::string>(), std::format("{}.Max", basePath));
    if (!maxResult) {
        return std::unexpected(std::move(maxResult.error()));
    }

    if (*maxResult < *minResult) {
        return std::unexpected(LeafError{
            .location = basePath,
            .message = std::format("Max ({:#x}) must be >= Min ({:#x})", *maxResult, *minResult),
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

    return std::ranges::any_of(m_ranges, [&](const Range& a_range) {
        return fileName == a_range.plugin && localID >= a_range.min && localID <= a_range.max;
    });
}

auto MakeFormIDRangePredicate(const nlohmann::json& a_payload) -> std::expected<PredicatePtr, LeafError> {
    if (!a_payload.is_array()) {
        return std::unexpected(LeafError{
            .location = "ByFormIDRange",
            .message = "must be an array of range objects",
        });
    }
    if (a_payload.empty()) {
        return std::unexpected(LeafError{
            .location = "ByFormIDRange",
            .message = "must not be empty",
        });
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