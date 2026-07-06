#pragma once

#include "ast.h"

namespace femto::ast {

void dump_type(const TypeNode& node, int indent = 0);
void dump_expr(const Expr& expr, int indent = 0);
void dump_stmt(const Stmt& stmt, int indent = 0);
void dump_decl(const Decl& decl, int indent = 0);
void dump_module(const Module& mod);

} // namespace femto::ast