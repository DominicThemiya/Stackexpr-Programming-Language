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

## Libraries

`grab asm<name>` includes `name.asm`. The compiler looks for it in the first of these that applies:

1. The directory given with `-L`
2. The `STACKEXPR_LIB` environment variable
3. `lib/` next to the compiler's directory (`build/../lib`), or `lib/` inside it
4. `lib/` in the current directory

The compiler checks that each grabbed library exists before generating code, and passes the directory to `nasm` with `-i`.

## Errors

Errors are reported as `file:line:column: error: message`, and the compiler exits with status 1:

```
math.sxp:8:1: error: 'entry' is no longer supported; define the entry point with 'stackexpr main ... end'
math.sxp:3:13: error: recursive stackexpr call: main -> a -> b -> a (stackexprs are inlined, so recursion is not supported)
```

The compiler stops at the first error. If `nasm` or `ld` fails, their messages are shown and the compiler exits with their status.

Errors inside a stack expression are only found when it is compiled, which happens when `main` calls it, directly or through other stack expressions. A stack expression that is never called is checked for syntax but not for unknown names.

## Exit status

| Status | Meaning |
| --- | --- |
| 0 | Success |
| 1 | Compile error, an unreadable input, or `nasm`/`ld` failed (their own status is used when non-zero) |
| 2 | Bad command-line arguments |
