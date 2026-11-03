#pragma once

#include <cstddef>
#include <string>

namespace stackexpr {

// Statistics from one run of the optimiser, for reporting.
struct OptimizeStats {
    std::size_t before = 0;  // instruction lines in the input
    std::size_t after = 0;   // instruction lines in the output
};

// A peephole pass over generated assembly. The code generator works one word at a
// time, so it often pushes a value only for a following word to pop it straight back.
// This pass rewrites such code without the round trip through memory:
//
//   mov rax, 5 / push rax           ->  push 5             (5 fits in 32 bits)
//   push X / pop X                  ->  (nothing)
//   push X / pop Y                  ->  mov Y, X
//   push X / add rsp, 8             ->  (nothing)          a value pushed then dropped
//   push X / <moves> / pop Y        ->  <moves> / mov Y, X when the moves only write
//                                       registers that X doesn't use
//   setl al / movzx eax, al /       ->  jge L              a comparison used only by
//   test rax, rax / jz L                                   an if or while
//
// It relies on the code generator never keeping a value in a register from one word to
// the next. A label, jump, call or any other instruction keeps instructions apart, so
// control flow is never changed.
std::string optimize(const std::string& assembly, OptimizeStats* stats = nullptr);

}  // namespace stackexpr
