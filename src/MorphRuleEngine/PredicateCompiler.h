// src/MorphRuleEngine/PredicateCompiler.h
#pragma once

#include "Config/Types.h"
#include "MorphRuleEngine/CompileError.h"
#include "MorphRuleEngine/CompiledRules.h"

#include <expected>
#include <functional>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace MorphRules::Engine {

// 叶子谓词工厂：把 ParsedPredicate::leafPayload 编译成 Predicate。
// 失败时返回 LeafError，其 location 只描述叶子内部（如 "ByFormIDRange[2]"），
// 由 PredicateCompiler 拼接上所在的谓词名。
using LeafFactory = std::function<std::expected<PredicatePtr, LeafError>(const nlohmann::json& a_payload)>;

// 把语法层的 ParsedConfig 编译成可执行的 CompiledRules。
//
// 编译过程中检测谓词引用环——语法层只验证"名字有定义"，不检查循环。
// 任何失败都以 CompileError 返回，不做部分编译。
//
// 不可重入：编译期间 m_currentConfig / m_compiled / m_inProgress 是共享
// 状态。当前唯一调用点是 main.cpp 的 LoadMorphRules（在 kDataLoaded 消息
// 里单线程执行），无并发需求。将来若引入"热重载配置"，需要给 Compile
// 加锁或改成每次新建一个 compiler 实例。
class PredicateCompiler {
public:
    void RegisterLeaf(std::string a_type, LeafFactory a_factory);

    auto Compile(const Syntax::ParsedConfig& a_config) -> std::expected<CompiledRules, CompileError>;

private:
    std::unordered_map<std::string, LeafFactory> m_factories;

    // 编译期间的状态。Compile 进入时绑定、返回前清空。
    // 语义上是"本次编译视图"，不是编译器持有的配置。
    const Syntax::ParsedConfig* m_currentConfig = nullptr;
    std::unordered_map<std::string, PredicatePtr> m_compiled;
    std::unordered_set<std::string> m_inProgress;

    // 实际的编译逻辑。只依赖 m_currentConfig / m_compiled / m_inProgress，
    // 不负责这两个的初始化与清理——那是 Compile 的职责。
    auto CompileImpl(const Syntax::ParsedConfig& a_config) -> std::expected<CompiledRules, CompileError>;

    auto BuildPredicate(const std::string& a_name) -> std::expected<PredicatePtr, CompileError>;

    auto BuildFromParsed(const Syntax::ParsedPredicate& a_parsed, const std::string& a_path)
        -> std::expected<PredicatePtr, CompileError>;
};

}  // namespace MorphRules::Engine