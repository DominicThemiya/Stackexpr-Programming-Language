#pragma once

#include "ast.hpp"

#include <string>

namespace stackexpr {

struct CodegenOptions {
    // Hosted (default): a Linux program; when `main` finishes it calls exit(0).
    // Freestanding: no operating system is assumed; when `main` finishes the CPU
    // disables interrupts and halts forever. Use this for kernels and boot code.
    bool freestanding = false;
    // Name of the global entry symbol that runs `main`.
    std::string entry = "_start";
};

// Generates NASM x86_64 assembly whose entry symbol runs `stackexpr main`.
//
// Stackexprs are inlined at each call site. A `call` to a name that is not a
// stackexpr is emitted as an invocation of an assembly library macro (from `grab`).
std::string generate(const Program& prog, const CodegenOptions& options = {});

}  // namespace stackexpr
