# Language reference

Stackexpr is a concatenative, stack-based language. Every word either pushes values onto the stack or takes values off it and pushes results. Expressions are written in Reverse Polish Notation: `3 4 +` pushes 3, pushes 4, then replaces both with 7.

- [Program structure](#program-structure)
- [Stack expressions](#stack-expressions)
- [The entry point](#the-entry-point)
- [Integers](#integers)
- [Operations](#operations)
- [Memory](#memory)
- [Conditionals](#conditionals)
- [Loops](#loops)
- [Constants](#constants)
- [Assembly libraries](#assembly-libraries)
- [Inline assembly](#inline-assembly)
- [Comments](#comments)
- [Keywords](#keywords)

## Program structure

A source file (by convention `.sxp`) contains only these at the top level:

| Item | Example |
| --- | --- |
| Library imports | `grab asm<io>` |
| Constants | `const greeting "hi"` |
| Stack expressions | `stackexpr name ... end` |

All code lives inside stack expressions. Whitespace and newlines only separate words, so `stackexpr two 1 1 + end` on one line is fine.

## Stack expressions

A stack expression is a named, reusable sequence of words.

```
stackexpr <name>
	<words>
end
```

`call <name>` runs it:

```
stackexpr square
	dup *
end

stackexpr main
	7 call square    // 49
end
```

Stack expressions can be defined in any order and can call each other. They are **inlined** at each call site, so a stack expression can't call itself, directly or through others. The compiler reports this as an error and shows the chain of calls.

If the name after `call` is not a stack expression, it is treated as a macro from a grabbed assembly library, such as `call putI64` from `asm<io>`. See [Assembly libraries](#assembly-libraries).

## The entry point

The program starts at the stack expression named `main`:

```
stackexpr main
	<program body>
end
```

Every program needs exactly one `main`, and it cannot be called. When `main` finishes, a Linux program exits with status 0, and a [freestanding](freestanding.md) program halts the CPU.

The `entry ... end` block from older versions no longer exists. Using `entry` gives an error that points to `stackexpr main`.

## Integers

All values are 64-bit integers.

| Literal | Example |
| --- | --- |
| Decimal | `42` |
| Negative | `-7` (no space between `-` and the digits) |
| Hexadecimal | `0xb8000` |
| Binary | `0b1010` |
| With separators | `1_000_000`, `0xffff_ffff` |

A `-` followed by a space is subtraction, so `10 -3` pushes 10 and -3, while `10 3 -` pushes 7.

Positive literals may use the full unsigned 64-bit range, so addresses such as `0xffffffff80000000` work. Whether a value is treated as signed or unsigned depends on the operation used on it.

## Operations

The stack effect column shows the stack before and after, top on the right. `a b -- r` means the operation takes `a` and `b` (with `b` on top) and leaves `r`.

### Arithmetic

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `+` | `a b -- r` | `a + b` |
| `-` | `a b -- r` | `a - b` |
| `*` | `a b -- r` | `a * b` |
| `/` | `a b -- r` | `a / b`, signed, rounds toward zero |
| `mod` | `a b -- r` | Remainder of `a / b`, signed (has the sign of `a`) |
| `udiv` | `a b -- r` | `a / b`, unsigned |
| `umod` | `a b -- r` | Remainder of `a / b`, unsigned |
| `++` | `a -- r` | `a + 1` |

Dividing by zero crashes the program with a CPU exception.

### Comparisons

Each comparison takes two values and pushes 1 if it holds, otherwise 0.

| Signed | Unsigned | Holds when |
| --- | --- | --- |
| `==` | `==` | `a` equals `b` |
| `!=` | `!=` | `a` differs from `b` |
| `<` | `ult` | `a` is less than `b` |
| `>` | `ugt` | `a` is greater than `b` |
| `<=` | `ule` | `a` is less than or equal to `b` |
| `>=` | `uge` | `a` is greater than or equal to `b` |

Use the unsigned forms for addresses and sizes. For example, `-1 1 <` is 1, but `-1 1 ult` is 0 because -1 is the largest unsigned value.

### Bitwise

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `and` | `a b -- r` | Bitwise AND |
| `or` | `a b -- r` | Bitwise OR |
| `xor` | `a b -- r` | Bitwise exclusive OR |
| `not` | `a -- r` | Flip every bit |
| `shl` | `a n -- r` | Shift `a` left by `n` bits |
| `shr` | `a n -- r` | Shift `a` right by `n` bits, filling with zeros |
| `sar` | `a n -- r` | Shift `a` right by `n` bits, keeping the sign |

Only the low 6 bits of `n` are used, as on the CPU.

### Stack

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `dup` | `a -- a a` | Duplicate the top value |
| `swap` | `a b -- b a` | Swap the top two values |
| `drop` | `a --` | Discard the top value |

## Memory

Loads read memory at an address; stores write to it.

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `@8` `@16` `@32` `@64` | `addr -- value` | Read 1, 2, 4 or 8 bytes. Smaller reads are zero-extended. |
| `!8` `!16` `!32` `!64` | `value addr --` | Write the low 1, 2, 4 or 8 bytes of `value` |

Addresses are ordinary integers, so address arithmetic is just `+`:

```
const bytes "ABCD"

stackexpr main
	bytes @8            // 65, 'A'
	bytes 2 + @8        // 67, 'C'
end
```

A constant's name pushes its address, so constants can be used as variables:

```
const counter 0

stackexpr main
	41 counter !64                // counter = 41
	counter @64 ++ counter !64    // counter = counter + 1
end
```

In a kernel, loads and stores reach hardware directly. This writes a white-on-black "H" to the top-left corner of the VGA text screen:

```
72 0x0f00 or 0xb8000 !16
```

## Conditionals

`if` takes the top value and runs its body if the value is not 0.

```
<condition> if
	<body>
end
```

```
stackexpr main
	2 2 == if
		1 call putI64
	end
end
```

## Loops

`while` runs the condition, then `do` takes the top value and runs the body if it is not 0. After the body, it goes back to the condition.

```
while <condition> do
	<body>
end
```

```
// Prints 5 down to 1.
stackexpr main
	5
	while dup 0 > do
		dup call putI64
		1 -
	end
	drop
end
```

## Constants

`const` defines data at the top level. Using its name pushes its address.

| Form | Creates |
| --- | --- |
| `const name 42` | An 8-byte integer |
| `const name "text"` | The bytes of the string followed by a 0 byte |

Strings can use `"..."` or `'...'`, and support escapes such as `\n` and `\t`. Constants are writable; see [Memory](#memory).

## Assembly libraries

`grab` includes an assembly library from the `lib/` directory:

```
grab asm<io>
grab asm<linux64>
```

Libraries provide macros that can be used with `call`, such as `call putI64`. They are described in [Assembly libraries](libraries.md), and [Compiler usage](compiler.md#libraries) explains where they are looked up.

## Inline assembly

`invoke` inserts one line of raw assembly (NASM syntax). This is useful for library macros that take arguments, or instructions the language doesn't have yet:

```
const greeting "Hello, world!\n"

stackexpr main
	invoke "putS greeting"
end
```

The data stack is the CPU stack (`rsp`). Inline assembly that pushes or pops changes the Stackexpr stack too; see [Compiler internals](internals.md#generated-code).

## Comments

```
// From two slashes to the end of the line
```

## Keywords

These words are reserved and can't be used as names:

```
grab stackexpr end call if while do const invoke
dup swap drop mod udiv umod ult ugt ule uge
and or xor not shl shr sar
```
