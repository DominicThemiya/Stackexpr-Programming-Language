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

**WARNING: This language is only intended to be run on Linux x86_64.**

The compiler is written in C++17 and uses [LexEngine](https://github.com/DominicThemiya/LexEngine) as its lexer.

## Building

You need `g++` (or another C++17 compiler), `make`, `nasm` and `ld`.

```sh
git clone --recurse-submodules https://github.com/DominicThemiya/Stackexpr-Programming-Language.git
cd Stackexpr-Programming-Language
make            # builds build/stackexpr
make test       # compiles the examples, and runs them if nasm is installed
```

If you cloned without `--recurse-submodules`, run `git submodule update --init` first.

## Usage

```sh
./build/stackexpr examples/math.sxp          # produces examples/math
./examples/math

./build/stackexpr program.sxp -o app         # choose the executable name
./build/stackexpr program.sxp -S             # only write program.asm
./build/stackexpr program.sxp --keep-asm     # keep the .asm next to the executable
```

Libraries for `grab asm<...>` are looked up in `-L <dir>`, then `$STACKEXPR_LIB`, then the `lib/` directory next to the compiler.

### Freestanding (kernels and boot code)

`--freestanding` compiles a program that doesn't assume an operating system. When `main` finishes, the CPU disables interrupts and halts instead of calling Linux's `exit`.

```sh
./build/stackexpr examples/kernel/vga.sxp --freestanding --entry kmain -T examples/kernel/linker.ld
```

| Option | Meaning |
| --- | --- |
| `--freestanding` | Halt (`cli; hlt`) when `main` ends instead of calling `exit` |
| `--entry <symbol>` | Name of the entry symbol (default `_start`) |
| `-T <script>` | Link with your own linker script |
| `--base <address>` | Load address of the code, e.g. `0x100000` or `0xffffffff80000000` |
| `--format elf\|bin` | Output an ELF file (default) or a flat binary |

A flat binary contains no `.bss`, so whatever loads it must zero that memory. The `io` library uses Linux system calls, so it can't be used in freestanding programs.

## The language

Linux x86_64 Assembly libraries.
```
grab asm<io>
grab asm<linux64>
grab asm<generics>
```

Stack expressions to organise and re-use.
```
stackexpr <identifier>
	<expression>
end
```
```
stackexpr push_num
	10 8 +
end
```

Stack expressions can be called. This performs the set of instructions defined in the specified stack expression.
A `call` to a name that isn't a stack expression calls a macro from a grabbed assembly library, such as `putI64`.
```
call push_num
```

### Entry point

The program starts at the stack expression named `main`. Every program needs exactly one, and it cannot be called.
When `main` finishes, the program exits with status 0.
```
stackexpr main
	<program body>
end
```

Example program that pushes two items to the stack, adds them and prints them.
```
grab asm<io>
grab asm<linux64>

stackexpr math
	3 4 +
end

stackexpr main
	call math
	call putI64
end
```

### Operations

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `42`, `-7`, `0xff`, `0b101`, `1_000` | `-- n` | Push an integer |
| `+` `-` `*` `/` `mod` | `a b -- r` | Signed arithmetic: `a + b`, `a - b`, `a * b`, `a / b`, `a % b` |
| `udiv` `umod` | `a b -- r` | Unsigned division and remainder |
| `++` | `a -- a+1` | Increment |
| `==` `!=` `<` `>` `<=` `>=` | `a b -- flag` | Signed compare of `a` with `b`, push 1 or 0 |
| `ult` `ugt` `ule` `uge` | `a b -- flag` | Unsigned compare (use these for addresses) |
| `and` `or` `xor` | `a b -- r` | Bitwise operations |
| `not` | `a -- ~a` | Bitwise complement |
| `shl` `shr` `sar` | `a n -- r` | Shift `a` by `n` bits: left, right (logical), right (arithmetic) |
| `@8` `@16` `@32` `@64` | `addr -- value` | Load 1, 2, 4 or 8 bytes from memory (zero-extended) |
| `!8` `!16` `!32` `!64` | `value addr --` | Store the low 1, 2, 4 or 8 bytes of `value` to memory |
| `dup` | `a -- a a` | Duplicate |
| `swap` | `a b -- b a` | Swap |
| `drop` | `a --` | Discard |

`-7` (no space) is a negative number; a `-` on its own is subtraction.
Integers are 64-bit. Positive literals may use the full unsigned range, so addresses like `0xffffffff80000000` work.

### Memory
Loads and stores read and write memory at any address. A const's name pushes its address, so consts can be used as variables.
```
const counter 0

stackexpr main
	41 counter !64              // counter = 41
	counter @64 ++ counter !64  // counter = counter + 1
	counter @64 call putI64     // 42
end
```
Writing a white-on-black "H" to the top-left corner of the VGA text screen:
```
72 0x0f00 or 0xb8000 !16
```

### Conditionals
```
<expression> if
	<body>
end
```
The following program pushes two numbers to the stack and checks whether they are equal, if so it prints 1.
```
grab asm<io>
grab asm<linux64>

stackexpr push_num
	2 2
end

stackexpr main
	call push_num
	== if
		1 call putI64
	end
end
```

### Loops
```
while <condition> do
	<body>
end
```
```
stackexpr main
	5
	while dup 0 > do
		dup call putI64
		1 -
	end
	drop
end
```

### Constants and inline assembly
`const` defines data at top level. Using a const's name pushes its address.
`invoke` emits a line of raw assembly.
```
const greeting "Hello, world!\n"

stackexpr main
	invoke "putS greeting"
end
```

### Comments
```
// everything after two slashes is ignored
```

More examples are in [`examples/`](examples).
