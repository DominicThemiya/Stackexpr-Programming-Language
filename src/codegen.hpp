#pragma once

#include "ast.hpp"

#include <string>
#include <vector>

namespace stackexpr {

struct CodegenOptions {
    // Hosted (default): a Linux program; when `main` finishes it calls exit(0).
    // Freestanding: no operating system is assumed; when `main` finishes the CPU
    // disables interrupts and halts forever. Use this for kernels and boot code.
    bool freestanding = false;
    // Name of the global entry symbol that runs `main`.
    std::string entry = "_start";
    // Run-time checks: return stack overflow on entry to every stackexpr and interrupt
    // handler, and the index of every array access not proven safe at compile time.
    // A failed check prints where it happened and stops the program.
    bool checks = true;
    // Source files, for the locations in run-time check messages.
    const std::vector<SourceFile>* files = nullptr;
};

// Generates NASM x86_64 assembly from a type-checked program.
//
// The data stack is the CPU stack (rsp). Return addresses and locals live on a
// separate return stack pointed to by r15, so subroutines don't disturb the data stack.
std::string generate(const Program& prog, const CodegenOptions& options = {});

}  // namespace stackexpr
