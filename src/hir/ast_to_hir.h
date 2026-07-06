#pragma once

#include "hir.h"
#include "parser/ast.h"
#include "sema/type_system.h"
#include "common/diagnostic.h"

namespace femto::hir {

class ASTToHIR {
public:
    ASTToHIR(DiagnosticEngine& diag);

    HIRModule lower(ast::Module& mod);

private:
    DiagnosticEngine& diag_;
    std::unordered_map<std::string, femto::sema::TypePtr> func_return_types_;

    DeclPtr lower_decl(ast::Decl& decl);
    HIRFunction lower_function(ast::FunctionDecl& func);
    HIRStruct lower_struct(ast::StructDecl& str);
    HIREnum lower_enum(ast::EnumDecl& en);
    HIRConstant lower_constant(ast::ConstantDecl& con);

    std::unique_ptr<HIRBlock> lower_block(ast::Block& block);
    StmtPtr lower_stmt(ast::Stmt& stmt);
    ExprPtr lower_expr(ast::Expr& expr);
};

} // namespace femto::hir
