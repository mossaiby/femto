#include "symbol_table.h"

namespace femto::sema {

bool Scope::insert(const Symbol& sym) {
    auto [it, ok] = symbols_.try_emplace(sym.name, sym);
    return ok;
}

const Symbol* Scope::lookup(const std::string& name) const {
    auto it = symbols_.find(name);
    if (it != symbols_.end()) return &it->second;
    if (parent_) return parent_->lookup(name);
    return nullptr;
}

const Symbol* Scope::lookup_local(const std::string& name) const {
    auto it = symbols_.find(name);
    return (it != symbols_.end()) ? &it->second : nullptr;
}

SymbolTable::SymbolTable() {
    scopes_.push_back(std::make_unique<Scope>());
}

void SymbolTable::push_scope() {
    scopes_.push_back(std::make_unique<Scope>(scopes_.back().get()));
}

void SymbolTable::pop_scope() {
    if (scopes_.size() > 1) scopes_.pop_back();
}

bool SymbolTable::insert(const Symbol& sym) {
    return scopes_.back()->insert(sym);
}

const Symbol* SymbolTable::lookup(const std::string& name) const {
    return scopes_.back()->lookup(name);
}

const Symbol* SymbolTable::lookup_local(const std::string& name) const {
    return scopes_.back()->lookup_local(name);
}

} // namespace femto::sema
