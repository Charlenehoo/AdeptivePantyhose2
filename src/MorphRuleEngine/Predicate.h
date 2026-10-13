// src/MorphRuleEngine/Predicate.h
#pragma once

#include <memory>
#include <vector>

namespace RE {
class Actor;
class TESObjectARMO;
}  // namespace RE

namespace MorphRules::Engine {

// 谓词求值时的上下文。字段会在后续扩展中增加（如 level、isPlayer 等），
// 但保持为纯值语义的聚合。
struct PredicateContext {
    RE::Actor* actor = nullptr;
    RE::TESObjectARMO* armor = nullptr;
};

class Predicate {
public:
    virtual ~Predicate() = default;
    virtual bool Evaluate(const PredicateContext& a_ctx) const = 0;
};

using PredicatePtr = std::shared_ptr<Predicate>;

// ---------- 内置谓词 ----------

// 恒真，用于 "Default" 保留谓词。
class AlwaysTruePredicate final : public Predicate {
public:
    bool Evaluate(const PredicateContext& /*a_ctx*/) const override { return true; }
};

// 短路：任一子谓词为真则真。
class AnyOfPredicate final : public Predicate {
public:
    explicit AnyOfPredicate(std::vector<PredicatePtr> a_children) : m_children(std::move(a_children)) {}

    bool Evaluate(const PredicateContext& a_ctx) const override {
        for (const auto& child : m_children) {
            if (child->Evaluate(a_ctx)) {
                return true;
            }
        }
        return false;
    }

private:
    std::vector<PredicatePtr> m_children;
};

// 短路：任一子谓词为假则假。
class AllOfPredicate final : public Predicate {
public:
    explicit AllOfPredicate(std::vector<PredicatePtr> a_children) : m_children(std::move(a_children)) {}

    bool Evaluate(const PredicateContext& a_ctx) const override {
        for (const auto& child : m_children) {
            if (!child->Evaluate(a_ctx)) {
                return false;
            }
        }
        return true;
    }

private:
    std::vector<PredicatePtr> m_children;
};

class NotPredicate final : public Predicate {
public:
    explicit NotPredicate(PredicatePtr a_child) : m_child(std::move(a_child)) {}

    bool Evaluate(const PredicateContext& a_ctx) const override { return !m_child->Evaluate(a_ctx); }

private:
    PredicatePtr m_child;
};

}  // namespace MorphRules::Engine