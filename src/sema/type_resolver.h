#pragma once

#include "type_system.h"
#include "parser/ast.h"

namespace femto::sema {

// Resolve AST type node to sema type
TypePtr resolve_ast_type(const ast::TypeNode& node);

} // namespace femto::sema
