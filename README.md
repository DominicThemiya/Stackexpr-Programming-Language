# Stackexpr

```
  Jul 2022  ●  Nugly
            │  The first version, written in Python.
            │
            │
  Nov 2026  ●  Stackexpr
               Rewritten in C++ with LexEngine.
               `entry` replaced by `stackexpr main`.
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
| `+` `-` `*` `/` | `a b -- r` | Arithmetic: `a + b`, `a - b`, `a * b`, `a / b` |
| `++` | `a -- a+1` | Increment |
| `==` `!=` `<` `>` `<=` `>=` | `a b -- flag` | Compare `a` with `b`, push 1 or 0 |
| `dup` | `a -- a a` | Duplicate |
| `swap` | `a b -- b a` | Swap |
| `drop` | `a --` | Discard |

`-7` (no space) is a negative number; a `-` on its own is subtraction.

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
