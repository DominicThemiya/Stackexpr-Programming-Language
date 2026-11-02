# Stackexpr

```
  Jul 2022  ●  Nugly
            │  The first version, written in Python.
            │
            │
  Nov 2026  ●  Stackexpr
               Rewritten in C++ with LexEngine.
```

Stackexpr is a concatenative stack-based programming language that compiles to x86_64 Assembly.
Reverse Polish Notation is used extensively.

It can build Linux programs and freestanding code such as kernels, which boot straight from QEMU or GRUB.

**WARNING: The compiler and the programs it builds only run on Linux x86_64.**

```
grab asm<io>
grab asm<linux64>

stackexpr square
	dup *
end

stackexpr main
	7 call square
	call putI64        // prints 49
end
```

## Quick start

You need `g++` (or another C++17 compiler), `make`, `nasm` and `ld`. QEMU is optional, for booting kernels.

```sh
git clone --recurse-submodules https://github.com/DominicThemiya/Stackexpr-Programming-Language.git
cd Stackexpr-Programming-Language
make                                  # builds build/stackexpr
make test                             # runs the test suite

./build/stackexpr examples/math.sxp   # produces examples/math
./examples/math

make run-kernel                       # boots examples/kernel/vga.sxp in QEMU
```

If you cloned without `--recurse-submodules`, run `git submodule update --init` first.

## Documentation

| Document | Contents |
| --- | --- |
| [Language reference](docs/language.md) | Syntax, the entry point, every operation, memory, control flow |
| [Compiler usage](docs/compiler.md) | Command-line options, library lookup, error messages |
| [Freestanding and kernels](docs/freestanding.md) | Building and booting a kernel with QEMU or GRUB |
| [Assembly libraries](docs/libraries.md) | What `asm<io>`, `asm<linux64>`, `asm<generics>` and `asm<multiboot>` provide |
| [Compiler internals](docs/internals.md) | How the compiler is structured, for contributors |

Example programs are in [`examples/`](examples).

## Project layout

| Path | Contents |
| --- | --- |
| `src/` | The compiler: lexer, parser, code generator and command-line driver |
| `lib/` | Assembly libraries for `grab asm<...>`, and the Multiboot linker script |
| `examples/` | Example programs, including a bootable kernel in `examples/kernel/` |
| `tests/` | The test suite (`make test`) |
| `docs/` | Documentation |
| `third_party/LexEngine` | The [LexEngine](https://github.com/DominicThemiya/LexEngine) lexer library (git submodule) |
