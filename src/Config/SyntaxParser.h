// src/Config/SyntaxParser.h
#pragma once

#include "Config/Types.h"

#include <filesystem>

namespace MorphRules::Syntax {

// 从磁盘读取并解析。
// IO 错误、JSON 语法错误、结构错误均以 ParseError 返回，不做恢复。
auto ParseFile(const std::filesystem::path& a_path) -> ParseResult;

// 从已解析的 nlohmann::json 对象解析。便于单测。
auto ParseJson(const nlohmann::json& a_json) -> ParseResult;

}  // namespace MorphRules::Syntax