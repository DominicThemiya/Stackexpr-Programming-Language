#pragma once

#include "ast.hpp"
#include "lexer.hpp"

#include <vector>

namespace stackexpr {

// Parses a token stream into a Program. Throws CompileError on the first problem.
// Requires exactly one `stackexpr main ... end`, which is the program's entry point.
Program parse(const std::vector<Token>& tokens);

}  // namespace stackexpr
