# Language reference

Stackexpr is a concatenative, stack-based language. Every word either pushes values onto the stack or takes values off it and pushes results. Expressions are written in Reverse Polish Notation: `3 4 +` pushes 3, pushes 4, then replaces both with 7.

Every stack expression declares what it takes from the stack and what it leaves, and the compiler checks this before generating any code.

- [Program structure](#program-structure)
- [Types and stack effects](#types-and-stack-effects)
- [Stack expressions](#stack-expressions)
- [The entry point](#the-entry-point)
- [Values](#values)
- [Operations](#operations)
- [Locals](#locals)
- [Control flow](#control-flow)
- [Memory and data](#memory-and-data)
- [Structs](#structs)
- [Hardware and interrupts](#hardware-and-interrupts)
- [Multiple files and libraries](#multiple-files-and-libraries)
- [Inline assembly](#inline-assembly)
- [Comments](#comments)
- [Reserved words](#reserved-words)

## Program structure

A source file (by convention `.sxp`) contains only these at the top level, in any order:

| Item | Example | See |
| --- | --- | --- |
| Imports | `grab asm<io>`, `grab "util.sxp"` | [Multiple files](#multiple-files-and-libraries) |
| Stack expressions | `stackexpr square (n: int -- int) dup * end` | [Stack expressions](#stack-expressions) |
| Interrupt handlers | `interrupt on_timer ... end` | [Hardware](#hardware-and-interrupts) |
| Assembly declarations | `extern putI64 (int --)` | [Libraries](#multiple-files-and-libraries) |
| Constants | `const PAGE_SIZE 4096` | [Values](#constants) |
| Variables and buffers | `var count`, `data msg "hi"`, `reserve buf 4096` | [Memory and data](#memory-and-data) |
| Structs | `struct Point x int y int end` | [Structs](#structs) |

All code lives inside stack expressions. Whitespace and newlines only separate words.

## Types and stack effects

Every value on the stack has one of three types:

| Type | Values | Comes from |
| --- | --- | --- |
| `int` | 64-bit integers | literals, arithmetic, loads from memory |
| `bool` | true or false | `true`, `false`, comparisons |
| `ptr` | memory addresses | variables, buffers, strings, `&name`, `~ptr` |

A **stack effect** says what a stack expression takes from the top of the stack and what it leaves in their place:

```
(inputs -- outputs)
```

The rightmost type is the top of the stack. Each entry can have a name, which is only documentation:

```
stackexpr vga_put (char: int index: int --)     // takes two ints, leaves nothing
stackexpr point (i: int -- ptr)                 // takes an int, leaves a ptr
stackexpr odds ( -- )                           // takes and leaves nothing
```

A single lowercase letter stands for **any type**. Every use of the same letter in one stack effect is the same type:

```
inline stackexpr twice (a -- a a)
	dup
end
```

`7 call twice` leaves `int int`; `true call twice` leaves `bool bool`. A letter in the outputs must also appear in the inputs.

### What the compiler checks

The compiler follows the types on the stack through every word, and reports an error with the exact location when:

- a word gets the wrong types: `true 1 +` is an error because `+` expects `int int`, `ptr int` or `int ptr`
- a stack expression leaves something other than what it declares, at its end or at a `return`
- a `call` doesn't have the right inputs on the stack
- an `if` gets anything but a `bool`, or its branches leave different stacks
- a `while` condition doesn't push exactly one `bool`, or its body changes the stack
- code follows a `return`, `break` or `continue` and can never run

For example:

```
stackexpr half (n: int -- int)
	2 / 1
end
```

```
half.sxp:1:11: error: stackexpr 'half' is declared (int -- int) but leaves (int int) at its end
```

### Converting between types

`~int`, `~bool` and `~ptr` change the type of the top value. Nothing happens at run time; they only tell the type checker to treat the value differently from then on:

```
0xb8000 ~ptr          // an address from a number
buffer ~int 12 shr    // a number from an address
x 0 != ~int           // 1 or 0 from a bool
```

The type name goes right after the `~`, with no space. `~bool` is the one conversion that does some work: it turns 0 into `false` and anything else into `true`.

Conversions are the deliberate escape hatch from type checking, so use them only where you mean it. You need one when a value's real meaning differs from the type it was given:

| Situation | Example |
| --- | --- |
| A number that is really an address, such as a hardware address | `0xb8000 ~ptr` |
| An address you take apart as a number | `handler ~int 16 shr` |
| An address read back from memory (loads always give an `int`) | `slot @64 ~ptr` |
| A `bool` used as a number (to print one, `putBool` needs no conversion) | `ready ~int 2 *` |

## Stack expressions

A stack expression is a named, reusable sequence of words with a stack effect:

```
stackexpr <name> (<inputs> -- <outputs>)
	<words>
end
```

`call <name>` runs it:

```
stackexpr square (n: int -- int)
	dup *
end

stackexpr main
	7 call square    // 49
end
```

Stack expressions are **subroutines**, so they can call themselves and each other, in any order of definition:

```
stackexpr fact (n: int -- int)
	dup 1 <= if drop 1 return end
	dup 1 - call fact *
end
```

### Inline stack expressions

`inline stackexpr` pastes the body at each call site instead of calling it. This saves the cost of a call for short words. An inline stack expression can't recurse, use `let` or `return`, or have its address taken.

```
inline stackexpr cells (n: int -- int)
	8 *
end
```

### Addresses and `execute`

`&name` pushes the address of a stack expression or interrupt handler, as a `ptr`. `execute` calls the address on top of the stack. Because the compiler can't know what's at an address, `execute` states the stack effect it expects:

```
9 &square execute (int -- int)    // 81
```

If the address points to something with a different stack effect, the program misbehaves at run time; the compiler can't catch that.

## The entry point

The program starts at the stack expression named `main`. Every program needs exactly one. Its stack effect is always `( -- )`, so it can be left out, and it can't be called.

```
stackexpr main
	<program body>
end
```

When `main` finishes, a Linux program exits with status 0, and a [freestanding](freestanding.md) program halts the CPU. The `entry ... end` block from older versions no longer exists.

## Values

### Integers

| Literal | Example |
| --- | --- |
| Decimal | `42` |
| Negative | `-7` (no space between `-` and the digits) |
| Hexadecimal | `0xb8000` |
| Binary | `0b1010` |
| With separators | `1_000_000`, `0xffff_ffff` |

A `-` followed by a space is subtraction, so `10 -3` pushes 10 and -3, while `10 3 -` pushes 7. Positive literals may use the full unsigned 64-bit range, so addresses such as `0xffffffff80000000` work.

### Booleans

`true` and `false` push a `bool`. Comparisons also produce `bool`s. At run time they are 1 and 0.

### Strings

A string literal in code pushes the address (`ptr`) of a 0-terminated copy of its bytes:

```
"Hello\n" call putStr
```

Strings can use `"..."` or `'...'`, and support escapes such as `\n`, `\t` and `\0`.

### Constants

`const` defines a named compile-time value. Using the name pushes the value; nothing is stored in memory.

```
const PAGE_SIZE 4096
const DEBUG true
const VGA 0xb8000 ~ptr
```

A constant is an `int` unless it is `true`/`false` (a `bool`) or has a conversion such as `~ptr` after its value.

## Operations

In the tables, `a b -- r` means the word takes `a` and `b` (with `b` on top) and leaves `r`. Where a word accepts several combinations of types, they are all listed.

### Arithmetic

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `+` | `int int -- int`, `ptr int -- ptr`, `int ptr -- ptr` | `a + b` |
| `-` | `int int -- int`, `ptr int -- ptr`, `ptr ptr -- int` | `a - b`; two addresses give their distance |
| `*` | `int int -- int` | `a * b` |
| `/` | `int int -- int` | `a / b`, signed, rounds toward zero |
| `mod` | `int int -- int` | Remainder of `a / b`, signed (has the sign of `a`) |
| `udiv` | `int int -- int` | `a / b`, unsigned |
| `umod` | `int int -- int` | Remainder of `a / b`, unsigned |
| `++` | `int -- int`, `ptr -- ptr` | `a + 1` |

Dividing by zero crashes the program with a CPU exception.

### Comparisons

Each comparison leaves a `bool`.

| Signed (`int int`) | Unsigned (`int int` or `ptr ptr`) | True when |
| --- | --- | --- |
| `<` | `ult` | `a` is less than `b` |
| `>` | `ugt` | `a` is greater than `b` |
| `<=` | `ule` | `a` is less than or equal to `b` |
| `>=` | `uge` | `a` is greater than or equal to `b` |

`==` and `!=` compare any two values of the same type (`a a -- bool`).

Use the unsigned forms for addresses and sizes. For example, `-1 1 <` is true, but `-1 1 ult` is false because -1 is the largest unsigned value.

### Logic and bits

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `and` `or` `xor` | `int int -- int` | Bitwise AND, OR, exclusive OR |
| | `bool bool -- bool` | Logical AND, OR, exclusive OR |
| `not` | `int -- int` | Flip every bit |
| | `bool -- bool` | Logical NOT |
| `shl` | `int int -- int` | Shift `a` left by `b` bits |
| `shr` | `int int -- int` | Shift `a` right by `b` bits, filling with zeros |
| `sar` | `int int -- int` | Shift `a` right by `b` bits, keeping the sign |

Only the low 6 bits of the shift count are used, as on the CPU.

### Stack

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `dup` | `a -- a a` | Copy the top value |
| `drop` | `a --` | Discard the top value |
| `swap` | `a b -- b a` | Swap the top two values |
| `over` | `a b -- a b a` | Copy the second value to the top |
| `rot` | `a b c -- b c a` | Move the third value to the top |
| `nip` | `a b -- b` | Discard the second value |
| `tuck` | `a b -- b a b` | Copy the top value under the second |

For anything more involved, [locals](#locals) are clearer than stack shuffling.

### Memory

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `@8` `@16` `@32` `@64` | `ptr -- int` | Read 1, 2, 4 or 8 bytes. Smaller reads are zero-extended. |
| `!8` `!16` `!32` `!64` | `a ptr --` | Write the low 1, 2, 4 or 8 bytes of a value of any type |

Loads always give an `int`. To read a stored address back, convert it: `slot @64 ~ptr`.

## Locals

`let` takes values off the stack and gives them names for the rest of its block. The last name gets the top of the stack:

```
stackexpr sum_of_squares (a: int b: int -- int)
	let a b in
		a a * b b * +
	end
end
```

- Using a local's name pushes its value. Its type is the type the value had when it was bound.
- `set <name>` takes a value of the same type off the stack and stores it in the local.
- A local can't have the same name as another local in scope, or as a top-level definition.
- The stack flows through a `let` block as through any other code, so it can take and leave values.

```
stackexpr pow (base: int exp: int -- int)
	let base exp in
		1
		while exp 0 > do
			base *
			exp 1 - set exp
		end
	end
end
```

## Control flow

### if / else

`if` takes a `bool` and runs its body when it is true. With `else`, the other branch runs when it is false.

```
<condition> if
	<body>
end

<condition> if
	<body>
else
	<other body>
end
```

Both branches must leave the same types on the stack. Without `else`, the body must leave the stack as it found it, because when the condition is false nothing runs.

```
stackexpr sign (n: int -- int)
	dup 0 < if
		drop -1
	else
		0 > if 1 else 0 end
	end
end
```

### while

`while` runs the condition, then `do` takes a `bool` and runs the body if it is true. After the body it goes back to the condition.

```
while <condition> do
	<body>
end
```

The condition must push exactly one `bool` and otherwise leave the stack alone, and the body must leave the stack as it found it. This guarantees the stack is the same on every trip around the loop.

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

### break and continue

Inside a `while` body, `break` leaves the loop and `continue` goes back to the condition. At either, the stack must be as it was at the start of the loop.

```
0
while dup 10 < do
	++
	dup 2 mod 0 == if continue end
	dup 7 > if break end
	dup call putI64
end
drop
```

### return

`return` leaves the current stack expression immediately. The stack must hold exactly its declared outputs. It is not allowed in inline stack expressions.

## Memory and data

These top-level definitions reserve memory. Using the name pushes its address, a `ptr`.

| Definition | Creates | Initial contents |
| --- | --- | --- |
| `var name` | An 8-byte cell | 0 |
| `var name 41` | An 8-byte cell | 41 |
| `data name "text"` | The bytes of the string, then a 0 byte | the string |
| `reserve name 4096` | A buffer of that many bytes | zeros |
| `reserve name 4096 align 4096` | A buffer aligned to a power of two | zeros |

```
var counter

stackexpr main
	41 counter !64                 // counter = 41
	counter @64 ++ counter !64     // counter = counter + 1
end
```

Addresses are ordinary values, so address arithmetic uses `+` and `-`:

```
data bytes "ABCD"

stackexpr main
	bytes 2 + @8 drop    // 67, 'C'
end
```

Freestanding programs loaded as flat binaries get no zeroed memory for `reserve` unless the loader provides it; see [Freestanding](freestanding.md#limitations).

## Structs

A struct names the fields of a block of memory. Fields are laid out in order with **no padding**, which matches the hardware structures an operating system needs.

```
struct IdtEntry
	offset_low u16
	selector u16
	ist u8
	flags u8
	offset_mid u16
	offset_high u32
	reserved u32
end
```

| Field type | Size in bytes |
| --- | --- |
| `u8` | 1 |
| `u16` | 2 |
| `u32` | 4 |
| `u64`, `int`, `ptr`, `bool` | 8 |

| Word | Stack effect | Meaning |
| --- | --- | --- |
| `Struct.field` | `ptr -- ptr` | Add the field's offset to an address |
| `sizeof Struct` | `-- int` | The struct's size in bytes |

Fields are read and written with the memory words, using the width that matches the field:

```
0x08 entry IdtEntry.selector !16
entry IdtEntry.flags @8
```

## Hardware and interrupts

These words give direct access to the CPU and devices. They are meant for [freestanding](freestanding.md) code; most of them crash a Linux program, which isn't allowed to use them.

| Word | Stack effect | Instruction |
| --- | --- | --- |
| `inb` `inw` `ind` | `port: int -- int` | Read 1, 2 or 4 bytes from an I/O port |
| `outb` `outw` `outd` | `value: int port: int --` | Write 1, 2 or 4 bytes to an I/O port |
| `cli` `sti` | `--` | Disable / enable interrupts |
| `hlt` | `--` | Wait for the next interrupt |
| `lgdt` `lidt` | `ptr --` | Load the GDT / IDT register from memory |
| `invlpg` | `ptr --` | Flush one page from the TLB |
| `rdmsr` | `msr: int -- int` | Read a model-specific register |
| `wrmsr` | `value: int msr: int --` | Write a model-specific register |
| `rdcr0` `rdcr2` `rdcr3` `rdcr4` | `-- int` | Read a control register |
| `wrcr0` `wrcr3` `wrcr4` | `int --` | Write a control register |

### Interrupt handlers

`interrupt` defines a handler for the CPU to run when an interrupt or exception happens. It saves every register, runs its body, restores them and returns with `iretq`.

```
var ticks

interrupt on_timer
	ticks @64 ++ ticks !64
	0x20 0x20 outb        // tell the interrupt controller we're done
end
```

- A handler's stack effect is `( -- )`, which can be left out. For CPU exceptions that push an error code (8, 10–14, 17, 21, 29 and 30), use `(code: int -- )`: the error code is on top of the stack, and the handler must take it off.
- A handler can't be called. `&name` gives its address, to put in the interrupt descriptor table.
- Handlers can call stack expressions and use locals.

[Freestanding and kernels](freestanding.md#interrupts) shows how to install handlers.

## Multiple files and libraries

`grab "path.sxp"` loads another source file. The path is relative to the file that grabs it, and each file is only loaded once, however many times it is grabbed.

```
grab "drivers/vga.sxp"
```

All files share one set of names, so two files can't define the same name.

`grab asm<name>` includes an assembly library from the library directory. If the library has a declarations file (`name.sxp` next to `name.asm`), that is loaded too. [Assembly libraries](libraries.md) describes the ones included.

### extern

Assembly macros are declared with `extern` and the stack effect they promise, then used with `call` like any stack expression:

```
extern putI64 (n: int --)
```

The compiler trusts the declared stack effect; it can't see inside the assembly.

## Inline assembly

`invoke` inserts one line of raw assembly (NASM syntax). It can be followed by its stack effect; without one, it is assumed to leave the stack alone:

```
invoke "int3"                          // ( -- )
invoke "not qword [rsp]" (int -- int)  // flips the bits of the top value
```

As with `extern`, the compiler trusts the stated stack effect. The text is copied as written; escapes such as `\n` are not decoded, so each `invoke` is one line. The data stack is the CPU stack (`rsp`), and `r15` must not be changed; see [Compiler internals](internals.md#generated-code).

## Comments

```
// From two slashes to the end of the line
```

## Reserved words

Keywords:

```
grab stackexpr inline interrupt extern end call
if else while do break continue return
const var data reserve struct sizeof let in set execute invoke true false
```

Built-in words, which can't be used as names:

```
dup drop swap over rot nip tuck
mod udiv umod ult ugt ule uge and or xor not shl shr sar
inb inw ind outb outw outd cli sti hlt lgdt lidt invlpg rdmsr wrmsr
rdcr0 rdcr2 rdcr3 rdcr4 wrcr0 wrcr3 wrcr4
```

The type names `int`, `bool` and `ptr` can't be used as names either.
