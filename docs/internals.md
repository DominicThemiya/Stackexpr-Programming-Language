# Compiler internals

This page is for working on the compiler itself.

## Pipeline

```
files ──► lexer ──► parser ──► Program ──► type checker ──► codegen ──► NASM assembly ──► nasm ──► ld
         (LexEngine)  (+ loader)  (AST)     (resolves names)
```

| File | Role |
| --- | --- |
| `src/lexer.hpp`, `src/lexer.cpp` | The `Tok` enum and the LexEngine language definition: keywords, symbols, numbers, strings, comments |
| `src/types.hpp` | `Type` and `Signature` (stack effects), and how they print |
| `src/ast.hpp` | `Program`, the definitions, `Op`, and `CompileError` |
| `src/parser.hpp`, `src/parser.cpp` | The loader, which follows `grab`s, and a recursive-descent parser per file |
| `src/typecheck.hpp`, `src/typecheck.cpp` | Checks every stack expression against its stack effect and resolves names |
| `src/codegen.hpp`, `src/codegen.cpp` | Generates NASM assembly from a checked `Program` |
| `src/main.cpp` | Command-line driver: reports errors, runs `nasm` and `ld` |

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

Each file is parsed by its own `Parser`, which adds definitions straight into the shared `Program`. All top-level names go into `Program::names`, which catches duplicates across files. Every `Loc` carries the index of its file in the list `main` prints errors from.

## Type checking

`typecheck` in `src/typecheck.cpp` runs after all files are loaded, so definitions can be used before they appear.

It checks each stack expression by walking its body with a **stack of types**, starting from the declared inputs:

- Literals and names push their type.
- Built-in words have one or more stack effects in `builtin_overloads`. The first whose inputs match the top of the stack is used, and its index is stored in `Op::overload` for the code generator (for example, `not` on a `bool` is a logical not).
- `call` applies the callee's declared stack effect. Type variables (single letters) are bound to the types they match, and a variable used twice must match the same type both times.
- `if` checks both branches from the same stack and requires them to end the same. `while` requires the condition to push one `bool` and the body to leave the stack unchanged.
- `return`, `break` and `continue` end the block. The checker represents that as "no stack" (`std::nullopt`), so a branch that returns doesn't have to match the other one.

Inside a body, type variables from the stack expression's own signature are **rigid**: they only equal themselves, so a body declared `(a -- a)` can't, for example, add 1 to its input.

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

1. Compiles every `examples/*.sxp`, runs it and compares its output with `tests/expected/<name>.txt`. Without `nasm`, it only generates assembly.
2. Builds `tests/fixtures/halt.sxp` as a freestanding ELF and flat binary, and checks the entry address and the halt loop.
3. If QEMU is installed, boots `examples/kernel/vga.sxp` and checks VGA memory for "Hi", then boots `examples/kernel/interrupts.sxp` and compares its debug console output with `tests/expected/kernel-interrupts.txt`.
4. Checks that a list of invalid programs give the expected error messages, including every kind of type error.

To add a test, add an example to `examples/` with its expected output in `tests/expected/`, or add an `expect_error` line to `tests/run.sh`.

## Adding a built-in word

1. Add an `OpKind` in `src/ast.hpp`.
2. Map its name to the `OpKind` in `builtin_words()` (or its symbol in `symbol_ops()`) in `src/parser.cpp`, and add the symbol to `build_language()` in `src/lexer.cpp` if it is one.
3. Give it a stack effect in `builtin_overloads` in `src/typecheck.cpp`.
4. Emit its assembly in `Generator::emit` in `src/codegen.cpp`.
5. Document it in `docs/language.md` and add a test.
