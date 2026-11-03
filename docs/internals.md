# Compiler internals

This page is for working on the compiler itself.

## Pipeline

```
files ──► lexer ──► parser ──► Program ──► type checker ──► codegen ──► optimiser ──► NASM assembly ──► nasm ──► ld
         (LexEngine)  (+ loader)  (AST)     (resolves names)            (peephole)
```

| File | Role |
| --- | --- |
| `src/lexer.hpp`, `src/lexer.cpp` | The `Tok` enum and the LexEngine language definition: keywords, symbols, numbers, strings, comments |
| `src/types.hpp` | `Type` and `Signature` (stack effects), and how they print |
| `src/ast.hpp` | `Program`, the definitions, `Op`, and `CompileError` |
| `src/parser.hpp`, `src/parser.cpp` | The loader, which follows `grab`s, and a recursive-descent parser per file |
| `src/typecheck.hpp`, `src/typecheck.cpp` | Checks every stack expression against its stack effect and resolves names |
| `src/codegen.hpp`, `src/codegen.cpp` | Generates NASM assembly from a checked `Program`, including run-time checks |
| `src/optimize.hpp`, `src/optimize.cpp` | The peephole optimiser, which rewrites the generated assembly |
| `src/main.cpp` | Command-line driver: prints errors with source lines, runs `nasm` and `ld` |

## Lexing

The lexer is a LexEngine `LangDef<Tok>` built once in `language()`. LexEngine tries, in order: comments, strings, numbers, words (keywords and identifiers), then symbols. Consequences:

- A symbol can't start with an identifier character, which is why unsigned comparisons are `ult` rather than `u<`.
- `-7` is lexed as `-` then `7`. The parser joins them into a negative literal when they are adjacent (no space), using token offsets.
- Identifiers may contain `.`, so `Point.x` is one token.

Only structural words are keywords. Built-in words such as `dup` and `outb` are identifiers that the parser looks up in `builtin_words()`; operator symbols all lex as `Tok::Op` and are looked up by text.

## Loading and parsing

`load_program` in `src/parser.cpp` loads the first file, then every file it grabs:

- `grab "path"` loads a file relative to the grabbing one. Files are keyed by canonical path, so each is loaded once.
- `grab asm<name>` records the library and loads `lib/name.sxp` if it exists.

Each file is parsed by its own `Parser`, which adds definitions straight into the shared `Program`. All top-level names go into `Program::names`, which catches duplicates across files. Every `Loc` carries the index of its file in the `SourceFile` list, which keeps each file's text so `main` can print the offending line under an error.

## Type checking

`typecheck` in `src/typecheck.cpp` runs after all files are loaded, so definitions can be used before they appear.

It checks each stack expression by walking its body with a **stack of types**, starting from the declared inputs:

- Literals and names push their type.
- Built-in words have one or more stack effects in `builtin_overloads`. The first whose inputs match the top of the stack is used, and its index is stored in `Op::overload` for the code generator (for example, `not` on a `bool` is a logical not).
- `call` applies the callee's declared stack effect. Type variables (single letters) are bound to the types they match, and a variable used twice must match the same type both times.
- `if` checks both branches from the same stack and requires them to end the same. `while` requires the condition to push one `bool` and the body to leave the stack unchanged.
- `return`, `break` and `continue` end the block. The checker represents that as "no stack" (`std::nullopt`), so a branch that returns doesn't have to match the other one.

Inside a body, type variables from the stack expression's own signature are **rigid**: they only equal themselves, so a body declared `(a -- a)` can't, for example, add 1 to its input.

Before checking bodies, the checker resolves each `reserve`: a const used as its count, and its element type's size, either of which may be defined in another file.

Errors are collected rather than thrown straight away: each definition is checked inside its own `try`, so one mistake is reported per stack expression, and `typecheck` throws a single `CompileErrors` with all of them, sorted by location. A stack expression whose signature is invalid is skipped, since checking its body would only produce confusing follow-on errors.

An array index directly preceded by a number or a const is checked against the array's length here, and the op is marked `checked` so the code generator leaves out the run-time check.

The checker also resolves every name. It records whether a name is a local, a constant, a global or a struct field in `Op::ref`, and stores the value, offset or local slot in `Op::value`. Each `let` gets fresh slots, and the total is stored in `StackExprDef::frame_slots`.

## Calling convention

The data stack **is** the CPU stack (`rsp`). Pushing a value is `push`, and operations `pop` their inputs and `push` results. Scratch registers are `rax`, `rbx`, `rcx` and `rdx`; nothing is kept in registers between operations.

Return addresses and locals live on a separate **return stack** pointed to by `r15`, a 64 KiB buffer set up by the entry code. A stack expression's code is:

```nasm
sx_fn_name:
    sub r15, 8*(N+1)             ; room for N locals and the return address
    pop qword [r15 + 8*N]        ; move the return address off the data stack
    <body>                       ; locals are at [r15 + 8*i]
sx_ret_name:                     ; `return` jumps here
    push qword [r15 + 8*N]
    add r15, 8*(N+1)
    ret
```

Moving the return address off `rsp` keeps the caller's values directly under the callee's, so a stack expression sees its inputs on top of the stack. It also means assembly macros that `pop` and `push` work unchanged, and that interrupts, which push their frame on `rsp`, simply land on top of the data stack.

An interrupt handler saves 14 registers (everything except `rsp` and `r15`) into its return-stack frame instead of a return address, and ends with `iretq`. If the CPU pushed an error code, it is on top of `rsp`, which is why such a handler is declared `(code: int -- )`.

`execute` is `pop rax; call rax`, and enters the callee the same way as `call`.

## Run-time checks

Unless `--no-checks` is given, the code generator adds two kinds of check. Each jumps to a small stub after the definition on failure, so the normal path falls straight through:

- **Return stack overflow**, in every prologue, right after `sub r15, ...`: `cmp r15, [_sx_rstack_limit]` / `jb`. Comparing with memory leaves every register untouched, which matters in an interrupt handler, whose registers aren't saved yet at that point.
- **Array bounds**, in every `name[]` not proven safe at compile time: an unsigned compare of the index with the length, so negative indexes fail too.

Each stub loads the address of a message with the source location into `rsi` and jumps to `_sx_check_fail`. That routine uses neither stack, since either may be what overflowed. On Linux it writes the message to stderr and exits with status 1; freestanding, it writes the message to port `0xe9` and halts.

## Optimisation

`optimize` in `src/optimize.cpp` works on the generated assembly text, line by line. It treats only lines indented by four spaces as instructions, skips comment lines, and treats everything else (labels, directives, macro calls) as a barrier. It repeats these rewrites until none applies:

| Before | After | Why it's safe |
| --- | --- | --- |
| `mov rax, 5` / `push rax` | `push 5` | The value fits in 32 bits; nothing reads `rax` after a word ends |
| `push X` / `pop X` | (nothing) | |
| `push X` / `pop Y` | `mov Y, X` | |
| `push X` / `add rsp, 8` | (nothing) | A value pushed and then dropped |
| `push X` / moves / `pop Y` | moves / `mov Y, X` | The moves only write registers that `X` doesn't use, and don't touch `rsp` or memory |
| `setl al` / `movzx eax, al` / `test rax, rax` / `jz L` | `jge L` | The bool was only used to branch |

`X` and `Y` may use `rsp` as an address (`push qword [rsp]` for `dup`): once the push and pop are both removed, `rsp` at the `mov` has the same value it had at the `push`, so the address means the same slot.

The rules rely on one property of the code generator: **no value is kept in a register from one word to the next**. Every word starts by loading what it needs from the stack. Two rules change what is left in a register or the flags afterwards (`push 5` no longer leaves 5 in `rax`, and a fused branch no longer leaves the bool in `rax`). So `invoke` code that expects a register to still hold a value from an earlier `invoke` can break. Pass values through the stack instead, or build with `--no-optimize`.

`make test` runs every example both optimised and with `--no-optimize`, and compares both outputs with the expected one.

## Generated code

A binary operation such as `-` compiles to:

```nasm
pop rbx        ; b (top of stack)
pop rax        ; a
sub rax, rbx
push rax
```

Integers use `mov rax, imm64` then `push rax`, so any 64-bit value works.

`inline` stack expressions are pasted at each call site. The generator keeps a list of the inline expansions in progress and reports recursion if one appears twice. An `extern` call becomes a line with just the macro's name.

### Names in the output

| Label | For |
| --- | --- |
| `sx_fn_<name>`, `sx_ret_<name>` | A stack expression and its return point |
| `sx_int_<name>` | An interrupt handler |
| `sx_g_<name>` | A `var`, `data` or `reserve` |
| `_sx_str_N` | A string literal |
| `_sx_if_N`, `_sx_while_N`, ... | Control flow, numbered so they stay unique |
| `_sx_rstack`, `_sx_rstack_top` | The return stack |
| `_sx_halt` | The freestanding halt loop |

### Output layout

```nasm
bits 64
default rel
%define SX_ENTRY <entry>
section .text
global <entry>
%include "<lib>.asm"           ; for each grab asm<...>
section .text
bits 64
<entry>:
    lea r15, [_sx_rstack_top]
    call sx_fn_main
    <exit(0) syscall, or the halt loop when freestanding>
<every stackexpr and interrupt handler>
section .rodata                ; string literals
section .data                  ; var and data
section .bss                   ; reserve, and the return stack
```

Use `-S` to see the output for any program.

## Tests

`make test` runs `tests/run.sh`, which:

1. Compiles every `examples/*.sxp`, runs it and compares its output with `tests/expected/<name>.txt`, both optimised and with `--no-optimize`. Without `nasm`, it only generates assembly.
2. Builds `tests/fixtures/halt.sxp` as a freestanding ELF and flat binary, and checks the entry address and the halt loop.
3. If QEMU is installed, boots `examples/kernel/vga.sxp` and checks VGA memory for "Hi", then boots `examples/kernel/interrupts.sxp` and compares its debug console output with `tests/expected/kernel-interrupts.txt`.
4. Runs programs that break a run-time check and checks their message and exit status, and that `--no-checks` removes the check.
5. Checks that a list of invalid programs give the expected error messages, including every kind of type error.

To add a test, add an example to `examples/` with its expected output in `tests/expected/`, or add an `expect_error` line to `tests/run.sh`.

## Adding a built-in word

1. Add an `OpKind` in `src/ast.hpp`.
2. Map its name to the `OpKind` in `builtin_words()` (or its symbol in `symbol_ops()`) in `src/parser.cpp`, and add the symbol to `build_language()` in `src/lexer.cpp` if it is one.
3. Give it a stack effect in `builtin_overloads` in `src/typecheck.cpp`.
4. Emit its assembly in `Generator::emit` in `src/codegen.cpp`.
5. Document it in `docs/language.md` and add a test.
