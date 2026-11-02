# Assembly libraries

Assembly libraries live in `lib/` and are included with `grab asm<name>`. They are written in NASM syntax and can contain macros, constants and code. A macro that takes no arguments can be used with `call`; one that takes arguments needs [`invoke`](language.md#inline-assembly).

| Library | For | Provides |
| --- | --- | --- |
| [`asm<io>`](#asmio) | Linux | Printing numbers and strings |
| [`asm<linux64>`](#asmlinux64) | Linux | System call numbers and file constants |
| [`asm<generics>`](#asmgenerics) | Linux | `exit` |
| [`asm<multiboot>`](#asmmultiboot) | Kernels | A boot stub for GRUB and QEMU |

## asm\<io\>

Needs `asm<linux64>` as well.

| Macro | Use | Effect |
| --- | --- | --- |
| `putI64` | `call putI64` | Takes the top of the stack and prints it as a signed integer, followed by a newline |
| `putS` | `invoke "putS label"` | Prints the 0-terminated string at `label`, such as a `const` |
| `puti` | `invoke "puti rax"` | Prints a register or value as an unsigned integer, followed by a newline |

```
grab asm<io>
grab asm<linux64>

const greeting "Hello\n"

stackexpr main
	invoke "putS greeting"
	-42 call putI64
end
```

The compiler adds the buffers this library needs (`digitSpace`, `digitSpacePos`, `negative_symbol`) when it is grabbed.

## asm\<linux64\>

Constants for Linux system calls, usable in `invoke` lines:

| Constants | Examples |
| --- | --- |
| System call numbers | `SYS_READ`, `SYS_WRITE`, `SYS_OPEN`, `SYS_EXIT`, ... |
| File descriptors | `STDIN`, `STDOUT`, `STDERR` |
| `open` flags | `O_RDONLY`, `O_WRONLY`, `O_CREAT`, `O_TRUNC`, ... |

It contains no code.

## asm\<generics\>

| Macro | Use | Effect |
| --- | --- | --- |
| `exit` | `call exit` | Exits the program with status 0. Needs `asm<linux64>`. |

## asm\<multiboot\>

Makes a freestanding program bootable by Multiboot loaders such as GRUB and QEMU's `-kernel`. It switches the CPU from 32-bit to 64-bit mode, sets up memory mapping and a stack, then jumps to the entry symbol.

Build with `--freestanding --entry <name> -T lib/multiboot.ld`. [Freestanding and kernels](freestanding.md#booting-with-asmmultiboot) describes the machine state when `main` starts.

## Writing a library

A library is any `.asm` file in the library directory. It is included near the top of the generated assembly, in the `.text` section, before the entry symbol. Keep in mind:

- Name labels so they won't clash with the compiler's, which start with `_sx_`.
- Code in a library sits before the entry point and never runs unless called, so it is safe to define subroutines.
- A library may switch sections (`section .data`) or modes (`bits 32`). The compiler switches back to `section .text` and `bits 64` afterwards.
- The entry symbol's name is available as `SX_ENTRY`.
