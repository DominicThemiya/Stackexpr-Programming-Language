# Freestanding and kernels

Stackexpr can build code that runs without an operating system, such as a kernel. This page shows how to build one, boot it in QEMU or with GRUB, handle interrupts, and what state the machine is in when your code starts.

- [Quick start](#quick-start)
- [What freestanding mode changes](#what-freestanding-mode-changes)
- [Booting with asm<multiboot>](#booting-with-asmmultiboot)
- [Running in QEMU](#running-in-qemu)
- [Interrupts](#interrupts)
- [Booting with GRUB](#booting-with-grub)
- [Using your own boot setup](#using-your-own-boot-setup)
- [Limitations](#limitations)

## Quick start

There are two example kernels in `examples/kernel/`:

| Kernel | What it does |
| --- | --- |
| `vga.sxp` | Writes "Hi" to the screen and halts |
| `interrupts.sxp` | Installs an interrupt table, handles a breakpoint exception and three timer interrupts, and reports on the debug console |

Here is `vga.sxp`:

```
grab asm<multiboot>

const VGA 0xb8000 ~ptr     // the text screen: 80x25 cells of (character, colour)
const WHITE_ON_BLACK 0x0f00

stackexpr vga_put (char: int index: int --)
	2 * VGA +
	swap WHITE_ON_BLACK or
	swap !16
end

stackexpr main
	72 0 call vga_put    // 'H'
	105 1 call vga_put   // 'i'
end
```

Build and boot it:

```sh
make run-kernel                                   # vga.sxp
make run-kernel KERNEL=examples/kernel/interrupts # interrupts.sxp
```

which runs these two commands:

```sh
./build/stackexpr examples/kernel/vga.sxp --freestanding --entry kmain -T lib/multiboot.ld -o build/kernel
qemu-system-x86_64 -kernel build/kernel -debugcon stdio
```

A QEMU window opens with "Hi" in the top-left corner, over the BIOS messages. For `interrupts.sxp`, the messages appear in your terminal.

## What freestanding mode changes

`--freestanding` tells the compiler not to assume Linux:

- When `main` ends, the CPU disables interrupts and halts in a loop (`cli; hlt; jmp`), instead of calling the `exit` system call.
- The linker is run with `-nostdlib` and 4 KiB page alignment.
- `asm<io>` uses Linux system calls, so don't grab it in a kernel.

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
| Segments | A GDT with a 64-bit code segment at selector `0x08` and a data segment at `0x10` |
| Data stack | `rsp` points to a 16 KiB stack, which starts empty |
| Return stack | `r15` points to a 64 KiB return stack set up by the compiler; see [internals](internals.md#calling-convention) |
| Boot information | `call multiboot_info` pushes the address of the Multiboot information structure, `call multiboot_magic` the value `0x2BADB002` |
| Screen | VGA text mode, 80x25, at address `0xb8000` |

Identity mapping means hardware addresses, such as `0xb8000` for the VGA screen, can be used directly with loads and stores.

## Running in QEMU

```sh
qemu-system-x86_64 -kernel build/kernel
```

If QEMU prints `VNC server running on ::1:5900` instead of opening a window, it was installed without a display backend. `qemu-system-x86_64 -display help` lists the backends you have. On Arch Linux, install one:

| Package | Run with | Shows the screen |
| --- | --- | --- |
| `qemu-ui-gtk` | `qemu-system-x86_64 -kernel build/kernel` | In a window |
| `qemu-ui-curses` | `qemu-system-x86_64 -kernel build/kernel -display curses` | In the terminal (text mode only). Quit with Esc then 2, then type `quit`. |

You can also connect any VNC viewer to `localhost:5900` without installing anything.

Useful additions:

| Option | Use |
| --- | --- |
| `-debugcon stdio` | Print every byte the kernel writes to port `0xe9`, the easiest way to get output |
| `-display none -monitor stdio` | No window; type QEMU monitor commands in the terminal |
| `-d int -no-reboot` | Print CPU exceptions and stop instead of rebooting, to debug crashes |
| `-s -S` | Wait for a debugger: `gdb build/kernel -ex 'target remote :1234'` |

In the monitor, `xp /2xh 0xb8000` shows the first two characters of the screen, and `info registers` shows the CPU state. `HLT=1` means the kernel reached the halt loop. `make test` uses the monitor to check that `vga.sxp` prints "Hi", and the debug console to check the output of `interrupts.sxp`.

Both ELF and flat binary (`--format bin`) kernels can be booted this way.

## Interrupts

`examples/kernel/interrupts.sxp` walks through the whole process. In short:

1. **Write handlers** with `interrupt`. Each saves every register, runs its body and returns with `iretq`:

   ```
   var ticks

   interrupt on_timer
   	ticks @64 ++ ticks !64
   	0x20 0x20 outb        // end of interrupt, for the PIC
   end
   ```

   For CPU exceptions that push an error code (8, 10–14, 17, 21, 29, 30), declare `(code: int -- )` and take the code off the stack.

2. **Build the interrupt descriptor table (IDT).** It is 256 16-byte entries. A struct describes one, `reserve` makes room for the table, and `&handler` gives each handler's address:

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

   reserve idt 4096 align 16
   ```

   The example's `set_gate (handler: ptr vector: int --)` fills in an entry, with selector `0x08` (the code segment from `asm<multiboot>`) and flags `0x8e` (present, ring 0, interrupt gate).

3. **Load it** with `lidt`, which takes the address of a 10-byte limit-and-base structure:

   ```
   struct IdtPointer
   	limit u16
   	base u64
   end
   ```

4. **Remap the PIC** so hardware interrupts use vectors 32–47 instead of clashing with CPU exceptions, and unmask the ones you want (`pic_init` in the example uses `outb` to do this).

5. **Enable interrupts** with `sti`, and wait for them with `hlt`.

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

`tests/fixtures/flat.ld` is a minimal linker script without a boot stub. Your entry code must provide a valid `rsp`; the compiler's entry sets up `r15` itself.

## Limitations

- **Flat binaries have no `.bss`.** `reserve` buffers, the return stack and the boot stub's page tables live there. Whatever loads a flat binary must provide that memory, zeroed; Multiboot loaders do this using the header.
- **The return stack is a fixed 64 KiB.** Deep recursion overflows it without any check.
- **No user mode yet.** Everything runs in ring 0, and there are no words for system calls, task state segments or switching privilege levels.
- **Types don't follow memory.** Loads always give an `int`, so a pointer read back from memory needs `~ptr`, and the compiler can't check what's stored where.
