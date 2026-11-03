# Compiler usage

```
stackexpr <file> [options]
```

By default the compiler turns `file` into a Linux executable: it generates assembly, runs `nasm` to assemble it, and `ld` to link it. The commands it runs are printed to stderr, prefixed with `+`.

```sh
./build/stackexpr examples/math.sxp          # produces examples/math
./build/stackexpr program.sxp -o app         # produces ./app
./build/stackexpr program.sxp -S             # only writes program.asm
```

## Options

| Option | Meaning |
| --- | --- |
| `-o <output>` | Output path. Default: the input path without its extension. |
| `-S` | Only generate assembly, written to `<output>.asm`. Needs neither `nasm` nor `ld`. |
| `--keep-asm` | Keep the generated `<output>.asm` after building. |
| `-L <lib-dir>` | Directory to look for assembly libraries in. |
| `-h`, `--help` | Show usage. |

### Freestanding options

These are for kernels and other code that runs without an operating system. See [Freestanding and kernels](freestanding.md).

| Option | Meaning |
| --- | --- |
| `--freestanding` | When `main` ends, halt the CPU (`cli; hlt`) instead of calling Linux's `exit`. |
| `--entry <symbol>` | Name of the entry symbol that runs `main`. Default: `_start`. |
| `-T <script>` | Link with this linker script. The script's `ENTRY` is then used as the ELF entry point. |
| `--base <address>` | Load address of the code segment, such as `0x100000` or `0xffffffff80000000`. |
| `--format elf\|bin` | Output an ELF file (default) or a flat binary with no headers. |

### Code generation options

| Option | Meaning |
| --- | --- |
| `--no-checks` | Leave out the [run-time checks](language.md#run-time-checks) for array bounds and return stack overflow. Faster, but those mistakes then corrupt memory silently. |
| `--no-optimize` | Leave out the [optimiser](#optimisation). Useful for reading the assembly a word produces, or to rule the optimiser out when chasing a bug. |

## Optimisation

The compiler generates code one word at a time, which leaves many values pushed to the stack only to be popped straight back. A cleanup pass afterwards (a peephole optimiser) removes them. For example, `10 3 -` becomes:

| Optimised (the default) | With `--no-optimize` |
| --- | --- |
| `mov rbx, 3`<br>`mov rax, 10`<br>`sub rax, rbx`<br>`push rax` | `mov rax, 10`<br>`push rax`<br>`mov rax, 3`<br>`push rax`<br>`pop rbx`<br>`pop rax`<br>`sub rax, rbx`<br>`push rax` |

A comparison followed by `if` or `while` also becomes a single conditional jump. Across the examples, the optimiser removes about a quarter of all instructions, and around a third in loops. [Compiler internals](internals.md#optimisation) lists its rules.

## Libraries

`grab asm<name>` includes `name.asm`. The compiler looks for it in the first of these that applies:

1. The directory given with `-L`
2. The `STACKEXPR_LIB` environment variable
3. `lib/` next to the compiler's directory (`build/../lib`), or `lib/` inside it
4. `lib/` in the current directory

The compiler checks that each grabbed library exists before generating code, and passes the directory to `nasm` with `-i`.

## Errors

Each error shows its location, the message, and the source line with a `^` under the problem:

```
half.sxp:4:11: error: stackexpr 'half' is declared (int -- int) but leaves (int int) at its end
 4 | stackexpr half (n: int -- int)
   |           ^

half.sxp:9:9: error: '+' expects (int int) or (ptr int) or (int ptr) on top of the stack, but found (bool int)
 9 | 	true 1 +
   | 	       ^

2 errors
```

- When the error is in a grabbed file, that file's path is shown.
- On a terminal the output is coloured. Set the `NO_COLOR` environment variable to turn colour off.
- Type errors are all reported at once, in source order, with at most one per stack expression. Syntax errors stop the compiler at the first one, since the rest of the file can't be read reliably after it.
- The compiler exits with status 1. If `nasm` or `ld` fails, their messages are shown and the compiler exits with their status.

Every stack expression is type checked, whether or not anything calls it. The [language reference](language.md#what-the-compiler-checks) lists what is checked.

## Exit status

| Status | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Compile error, an unreadable input, or `nasm`/`ld` failed (their own status is used when non-zero) |

A compiled program exits with status 1 when a [run-time check](language.md#run-time-checks) fails.
| 2 | Bad command-line arguments |
