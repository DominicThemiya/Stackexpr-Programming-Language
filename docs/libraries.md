# Assembly libraries

Assembly libraries live in `lib/` and are included with `grab asm<name>`. Each is a NASM file (`name.asm`) and, optionally, a declarations file (`name.sxp`) that is loaded automatically and declares its macros with `extern`, so the type checker knows their stack effects.

| Library | For | Provides |
| --- | --- | --- |
| [`asm<io>`](#asmio) | Linux | Printing numbers and strings |
| [`asm<linux64>`](#asmlinux64) | Linux | System call numbers and file constants |
| [`asm<generics>`](#asmgenerics) | Linux | `exit` |
| [`asm<multiboot>`](#asmmultiboot) | Kernels | A boot stub for GRUB and QEMU |

## asm\<io\>

Needs `asm<linux64>` as well.

| Word | Stack effect | Effect |
| --- | --- | --- |
| `call putI64` | `n: int --` | Prints a signed integer, followed by a newline |
| `call putStr` | `s: ptr --` | Prints the 0-terminated string at an address, such as a `data` or a string literal |

```
grab asm<io>
grab asm<linux64>

data greeting "Hello\n"

stackexpr main
	greeting call putStr
	"world\n" call putStr
	-42 call putI64
end
```

`io.asm` also has two older macros that take NASM arguments rather than stack values, `putS label` and `puti value`. They aren't declared, so they can only be used through `invoke`.

The compiler adds the buffers this library needs (`digitSpace`, `digitSpacePos`, `negative_symbol`) when it is grabbed.

## asm\<linux64\>

Constants for Linux system calls, usable in `invoke` lines:

| Constants | Examples |
| --- | --- |
| System call numbers | `SYS_READ`, `SYS_WRITE`, `SYS_OPEN`, `SYS_EXIT`, ... |
| File descriptors | `STDIN`, `STDOUT`, `STDERR` |
| `open` flags | `O_RDONLY`, `O_WRONLY`, `O_CREAT`, `O_TRUNC`, ... |

It contains no code and has no declarations file.

## asm\<generics\>

| Word | Stack effect | Effect |
| --- | --- | --- |
| `call exit` | `--` | Exits the program with status 0. Needs `asm<linux64>`. |

## asm\<multiboot\>

Makes a freestanding program bootable by Multiboot loaders such as GRUB and QEMU's `-kernel`. It switches the CPU from 32-bit to 64-bit mode, sets up memory mapping, a GDT and a stack, then jumps to the entry symbol.

| Word | Stack effect | Effect |
| --- | --- | --- |
| `call multiboot_info` | `-- ptr` | Address of the Multiboot information structure from the bootloader |
| `call multiboot_magic` | `-- int` | `0x2BADB002` when booted by a Multiboot loader |

Build with `--freestanding --entry <name> -T lib/multiboot.ld`. [Freestanding and kernels](freestanding.md#booting-with-asmmultiboot) describes the machine state when `main` starts.

## Writing a library

A library is a `.asm` file in the library directory, plus an optional `.sxp` file with the same name for its declarations.

The `.asm` file is included near the top of the generated assembly, in the `.text` section, before the entry symbol:

- Name labels so they won't clash with the compiler's, which start with `_sx_` or `sx_`.
- Code in a library sits before the entry point and never runs unless called, so it is safe to define subroutines.
- A library may switch sections (`section .data`) or modes (`bits 32`). The compiler switches back to `section .text` and `bits 64` afterwards.
- The entry symbol's name is available as `SX_ENTRY`.

A macro used from Stackexpr takes no NASM arguments. It takes its inputs from the data stack with `pop` and leaves its outputs with `push`:

```nasm
%macro double 0
    pop rax
    add rax, rax
    push rax
%endmacro
```

It may use any register except `rsp` (beyond its own pushes and pops) and `r15`, which holds the return stack. Real subroutines (`call`/`ret`) inside a macro are fine, as long as they leave `rsp` balanced.

Declare it in the `.sxp` file with the stack effect it actually has:

```
extern double (n: int -- int)
```

The compiler trusts this declaration, so a wrong one leads to a broken program rather than a compile error.
