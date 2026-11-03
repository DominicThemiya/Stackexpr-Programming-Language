#pragma once

#include "ast.hpp"

namespace stackexpr {

// Checks every stackexpr against its stack effect and resolves names, filling in the
// fields of Op and StackExprDef that the code generator needs. Throws CompileError on
// the first problem.
void typecheck(Program& prog);

}  // namespace stackexpr
