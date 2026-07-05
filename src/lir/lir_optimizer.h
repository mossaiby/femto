#pragma once

#include "lir.h"

namespace femto::lir {

class LIROptimizer {
public:
    void optimize(LIRModule& mod);

private:
    void optimize_function(LIRFunction& func);
    void optimize_block(LIRBasicBlock& block);
    void dead_code_elimination(LIRFunction& func);
    void peephole_optimize(LIRBasicBlock& block);
};

} // namespace femto::lir
