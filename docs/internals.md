# Compiler internals

This page is for working on the compiler itself.

## Pipeline

```
source ──► lexer ──► tokens ──► parser ──► Program ──► codegen ──► NASM assembly ──► nasm ──► ld
          (LexEngine)                     (AST)
```

| File | Role |
| --- | --- |
| `src/lexer.hpp`, `src/lexer.cpp` | The `Tok` enum and the LexEngine language definition: keywords, symbols, numbers, strings, comments |
| `src/ast.hpp` | `Program`, `StackExprDef`, `ConstDef`, `Op`, and `CompileError` |
| `src/parser.hpp`, `src/parser.cpp` | Recursive-descent parser from tokens to a `Program` |
| `src/codegen.hpp`, `src/codegen.cpp` | Generates NASM assembly from a `Program` |
| `src/main.cpp` | Command-line driver: reads the file, reports errors, runs `nasm` and `ld` |

## Lexing

The lexer is a LexEngine `LangDef<Tok>` built once in `language()`. LexEngine tries, in order: comments, strings, numbers, words (keywords and identifiers), then symbols. Two consequences:

- A symbol can't start with an identifier character, which is why unsigned comparisons are `ult` rather than `u<`.
- `-7` is lexed as `-` then `7`. The parser joins them into a negative literal when they are adjacent (no space), using token offsets.

## Parsing

`Parser` in `src/parser.cpp` walks the tokens once:

- At the top level it accepts only `grab`, `const` and `stackexpr`.
- Inside a stack expression, `parse_op` turns each word into an `Op`. `if` and `while` hold nested `body` (and `cond`) vectors.
- It checks for duplicate stack expressions and a missing `main`.

Every error is a `CompileError` carrying a line and column. The parser stops at the first one.

## Generated code

The data stack **is** the CPU stack: pushing a value is `push`, and operations `pop` their inputs and `push` results. Scratch registers are `rax`, `rbx`, `rcx` and `rdx`; nothing is kept in registers between operations.

A binary operation such as `-` compiles to:

```nasm
pop rbx        ; b (top of stack)
pop rax        ; a
sub rax, rbx
push rax
```

Integers use `mov rax, imm64` then `push rax`, so any 64-bit value works.

### Stack expressions are inlined

`call name` pastes the body of `name` at the call site. The generator keeps a list of the stack expressions it is currently expanding and reports recursion if one appears twice. A stack expression is only generated when reached from `main`.

Inlining avoids the problem of `call`/`ret` putting return addresses on the data stack. Replacing it with real subroutines will need a separate data stack (for example in `r15`), leaving `rsp` for return addresses.

A `call` to an unknown name becomes a line containing just that name, which NASM expands as a library macro. If no library is grabbed, it is an error instead.

### Labels

Control flow uses global labels with a counter, `_sx_if_N_end`, `_sx_while_N` and `_sx_while_N_end`, so they are unique even when the same stack expression is inlined many times. The halt loop is `_sx_halt`. All compiler labels start with `_sx_`.

### Output layout

```nasm
bits 64
default rel
section .text
global <entry>
%define SX_ENTRY <entry>
%include "<lib>.asm"           ; for each grab
section .text
bits 64
<entry>:
    <main, with calls inlined>
    <exit(0) syscall, or the halt loop when freestanding>
section .bss                   ; only when asm<io> is grabbed
section .data
    <consts>
```

Use `-S` to see the output for any program.

## Tests

`make test` runs `tests/run.sh`, which:

1. Compiles every `examples/*.sxp`, runs it and compares its output with `tests/expected/<name>.txt`. Without `nasm`, it only generates assembly.
2. Builds `tests/fixtures/halt.sxp` as a freestanding ELF and flat binary, and checks the entry address and the halt loop bytes.
3. Boots `examples/kernel/vga.sxp` in QEMU, if installed, and checks VGA memory for "Hi".
4. Checks that a list of invalid programs give the expected error messages.

To add a test, add an example to `examples/` with its expected output in `tests/expected/`, or add an `expect_error` line to `tests/run.sh`.

## Adding an operation

1. Add a token to `Tok` in `src/lexer.hpp`, register it in `build_language()` and name it in `describe()` (`src/lexer.cpp`).
2. Add an `OpKind` in `src/ast.hpp`.
3. Map the token to the `OpKind` in `Parser::parse_op` (`src/parser.cpp`).
4. Emit its assembly in `Generator::emit` (`src/codegen.cpp`).
5. Document it in `docs/language.md` and add a test.
