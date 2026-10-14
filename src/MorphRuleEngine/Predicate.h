// src/MorphRuleEngine/Predicate.h
#pragma once

#include <algorithm>
#include <memory>
#include <vector>

namespace RE {
class Actor;
class TESObjectARMO;
}  // namespace RE

namespace MorphRules::Engine {

// 谓词求值时的上下文。
struct PredicateContext {
    RE::Actor* actor = nullptr;
    RE::TESObjectARMO* armor = nullptr;
};

class Predicate {
public:
    Predicate() = default;
    virtual ~Predicate() = default;

    Predicate(const Predicate&) = delete;
    Predicate(Predicate&&) = delete;
    auto operator=(const Predicate&) -> Predicate& = delete;
    auto operator=(Predicate&&) -> Predicate& = delete;

    [[nodiscard]] virtual auto Evaluate(const PredicateContext& a_ctx) const -> bool = 0;

    // "恒真" 标记，仅由 AlwaysTruePredicate 返回 true。
    // MorphRuleEngine 在 equipped == false 时用它识别 Default 分支。
    [[nodiscard]] virtual auto IsAlwaysTrue() const -> bool { return false; }
};

using PredicatePtr = std::shared_ptr<Predicate>;

// ---------- 内置谓词 ----------

// 恒真，编译层为 "Default" 保留谓词创建的实例。
class AlwaysTruePredicate final : public Predicate {
public:
    [[nodiscard]] auto Evaluate(const PredicateContext& /*a_ctx*/) const -> bool override { return true; }

    [[nodiscard]] auto IsAlwaysTrue() const -> bool override { return true; }
};

// 短路：任一子谓词为真则真。
class AnyOfPredicate final : public Predicate {
public:
    explicit AnyOfPredicate(std::vector<PredicatePtr> a_children) : m_children(std::move(a_children)) {}

    [[nodiscard]] auto Evaluate(const PredicateContext& a_ctx) const -> bool override {
        return std::ranges::any_of(m_children,
                                   [&a_ctx](const PredicatePtr& a_child) { return a_child->Evaluate(a_ctx); });
    }

private:
    std::vector<PredicatePtr> m_children;
};

// 短路：任一子谓词为假则假。
class AllOfPredicate final : public Predicate {
public:
    explicit AllOfPredicate(std::vector<PredicatePtr> a_children) : m_children(std::move(a_children)) {}

    [[nodiscard]] auto Evaluate(const PredicateContext& a_ctx) const -> bool override {
        return std::ranges::all_of(m_children,
                                   [&a_ctx](const PredicatePtr& a_child) { return a_child->Evaluate(a_ctx); });
    }

private:
    std::vector<PredicatePtr> m_children;
};

class NotPredicate final : public Predicate {
public:
    explicit NotPredicate(PredicatePtr a_child) : m_child(std::move(a_child)) {}

    [[nodiscard]] auto Evaluate(const PredicateContext& a_ctx) const -> bool override {
        return !m_child->Evaluate(a_ctx);
    }

private:
    PredicatePtr m_child;
};

}  // namespace MorphRules::Engine