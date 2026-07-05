#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <memory>
#include <optional>

#include "type_system.h"
#include "common/source_location.h"

namespace femto::sema {

enum class SymbolKind {
    Variable,
    Constant,
    Function,
    Struct,
    Enum,
    Union,
    Namespace,
    TypeParam,
};

struct Symbol {
    std::string name;
    SymbolKind kind;
    TypePtr type;
    SourceSpan span;
    bool is_exported = false;
    bool is_const = false;
};

class Scope {
public:
    explicit Scope(Scope* parent = nullptr) : parent_(parent) {}

    bool insert(const Symbol& sym);
    const Symbol* lookup(const std::string& name) const;
    const Symbol* lookup_local(const std::string& name) const;
    Scope* parent() const { return parent_; }

    const std::unordered_map<std::string, Symbol>& symbols() const { return symbols_; }

private:
    Scope* parent_;
    std::unordered_map<std::string, Symbol> symbols_;
};

class SymbolTable {
public:
    SymbolTable();

    void push_scope();
    void pop_scope();

    bool insert(const Symbol& sym);
    const Symbol* lookup(const std::string& name) const;
    const Symbol* lookup_local(const std::string& name) const;

    Scope& current_scope() { return *scopes_.back(); }
    std::size_t scope_depth() const { return scopes_.size(); }

private:
    std::vector<std::unique_ptr<Scope>> scopes_;
};

} // namespace femto::sema
