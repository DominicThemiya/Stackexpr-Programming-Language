#pragma once

#include "ast.hpp"

namespace stackexpr {

// Checks every stackexpr against its stack effect and resolves names, filling in the
// fields of Op, StackExprDef and GlobalDef that the code generator needs.
//
// Throws CompileErrors listing every problem found, at most one per definition.
void typecheck(Program& prog);

}  // namespace stackexpr
