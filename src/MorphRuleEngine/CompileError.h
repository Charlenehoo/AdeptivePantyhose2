// src/MorphRuleEngine/CompileError.h
#pragma once

#include <string>

namespace MorphRules::Engine {

// 叶子工厂内部报错用。location 相对该叶子，如 "ByFormIDRange[2]"。
// 不含所在的谓词名——由 PredicateCompiler 拼接上。
struct LeafError {
    std::string location;
    std::string message;
};

// 编译整份配置时报错用。location 是完整路径，如
// "Predicates.CosplayPackHeels.ByFormIDRange[2]"。
struct CompileError {
    std::string location;
    std::string message;
};

}  // namespace MorphRules::Engine