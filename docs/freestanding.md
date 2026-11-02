# Freestanding and kernels

Stackexpr can build code that runs without an operating system, such as a kernel. This page shows how to build one, boot it in QEMU or with GRUB, and what state the machine is in when your code starts.

- [Quick start](#quick-start)
- [What freestanding mode changes](#what-freestanding-mode-changes)
- [Booting with asm<multiboot>](#booting-with-asmmultiboot)
- [Running in QEMU](#running-in-qemu)
- [Booting with GRUB](#booting-with-grub)
- [Using your own boot setup](#using-your-own-boot-setup)
- [Limitations](#limitations)

## Quick start

The example kernel in `examples/kernel/vga.sxp` writes "Hi" to the screen and halts:

```
grab asm<multiboot>

stackexpr vga_put    // char index --   (white on black)
	2 * 0xb8000 +
	swap 0x0f00 or
	swap !16
end

stackexpr main
	72 0 call vga_put    // 'H'
	105 1 call vga_put   // 'i'
end
```

Build and boot it:

```sh
make run-kernel
```

which runs these two commands:

```sh
./build/stackexpr examples/kernel/vga.sxp --freestanding --entry kmain -T lib/multiboot.ld -o build/kernel
qemu-system-x86_64 -kernel build/kernel
```

A QEMU window opens with "Hi" in the top-left corner, over the BIOS messages.

## What freestanding mode changes

`--freestanding` tells the compiler not to assume Linux:

- When `main` ends, the CPU disables interrupts and halts in a loop (`cli; hlt; jmp`), instead of calling the `exit` system call.
- The linker is run with `-nostdlib` and 4 KiB page alignment.
- Storage for `asm<io>` is only added when that library is grabbed. `asm<io>` itself uses Linux system calls, so don't grab it in a kernel.

The other options from [Compiler usage](compiler.md#freestanding-options) control linking: `--entry` names the entry symbol, `-T` uses a linker script, `--base` sets the load address, and `--format bin` outputs a flat binary.

## Booting with asm<multiboot>

Bootloaders that follow the Multiboot standard, including GRUB and QEMU's `-kernel` option, start a kernel in 32-bit mode. `asm<multiboot>` is a small boot stub that switches the CPU to 64-bit mode and then jumps to your entry symbol.

To use it:

1. Add `grab asm<multiboot>` to your program.
2. Build with `--freestanding --entry kmain -T lib/multiboot.ld`. Any entry name works, as long as `--entry` is given.

`lib/multiboot.ld` loads the kernel at 1 MiB and places the Multiboot header first.

When `main` starts:

| State | Value |
| --- | --- |
| CPU mode | 64-bit long mode, ring 0, interrupts disabled |
| Memory | The first 1 GiB is identity-mapped (virtual address = physical address), using 2 MiB pages |
| Stack | `rsp` points to a 16 KiB stack. This is also the Stackexpr data stack, and it starts empty. |
| Boot information | `rdi` holds the address of the Multiboot information structure, `rsi` the Multiboot magic value `0x2BADB002` |
| Screen | VGA text mode, 80x25, at address `0xb8000` |

Identity mapping means hardware addresses, such as `0xb8000` for the VGA screen, can be used directly with loads and stores.

## Running in QEMU

```sh
qemu-system-x86_64 -kernel build/kernel
```

Useful additions:

| Option | Use |
| --- | --- |
| `-display none -monitor stdio` | No window; type QEMU monitor commands in the terminal |
| `-d int -no-reboot` | Print CPU exceptions and stop instead of rebooting, to debug crashes |
| `-s -S` | Wait for a debugger: `gdb build/kernel -ex 'target remote :1234'` |

In the monitor, `xp /2xh 0xb8000` shows the first two characters of the screen, and `info registers` shows the CPU state. `HLT=1` means the kernel reached the halt loop. `make test` uses this to check that the example kernel prints "Hi".

Both ELF and flat binary (`--format bin`) kernels can be booted this way.

## Booting with GRUB

GRUB can load the same kernel with its `multiboot` command (not `multiboot2`). To make a bootable ISO with `grub-mkrescue` (which needs `xorriso` installed):

```sh
mkdir -p iso/boot/grub
cp build/kernel iso/boot/kernel
cat > iso/boot/grub/grub.cfg <<'EOF'
menuentry "Stackexpr" {
    multiboot /boot/kernel
}
EOF
grub-mkrescue -o stackexpr.iso iso
qemu-system-x86_64 -cdrom stackexpr.iso
```

This route isn't covered by the test suite yet; booting through QEMU's `-kernel` is.

## Using your own boot setup

`asm<multiboot>` is optional. Without it, the output is plain 64-bit code starting at your entry symbol, for loaders that already start in 64-bit mode, such as Limine. Use your own linker script to place it:

```sh
./build/stackexpr kernel.sxp --freestanding --entry kmain -T my-kernel.ld
./build/stackexpr kernel.sxp --freestanding --entry kmain --base 0xffffffff80000000
```

`tests/fixtures/flat.ld` is a minimal linker script without a boot stub.

## Limitations

These are planned but not yet in the language:

- **No real functions.** Stack expressions are inlined, so there are no function addresses for interrupt handlers or the IDT.
- **No special instructions.** Port I/O, control registers, `lgdt`/`lidt` and similar need `invoke`.
- **No interrupt handlers.** There is no way to define a stack expression that ends with `iretq`.
- **No buffers.** Large zeroed areas, such as page tables, can't be reserved yet except through assembly.
- **Flat binaries have no `.bss`.** Whatever loads a flat binary must zero that memory; Multiboot loaders do this using the header.
