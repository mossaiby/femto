#pragma once

#include "lir.h"

namespace femto::lir {

class LIROptimizer {
public:
    void optimize(LIRModule& mod);

private:
    void dead_code_elimination(LIRFunction& func, const LIRModule& mod);
    void peephole_optimize(LIRBasicBlock& block);
};

} // namespace femto::lir
