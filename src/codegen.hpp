#pragma once

#include "ast.hpp"

#include <string>

namespace stackexpr {

// Generates NASM x86_64 assembly for a Linux executable whose entry point (_start)
// runs `stackexpr main` and then exits with status 0.
//
// Stackexprs are inlined at each call site. A `call` to a name that is not a
// stackexpr is emitted as an invocation of an assembly library macro (from `grab`).
std::string generate(const Program& prog);

}  // namespace stackexpr
