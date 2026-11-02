; Multiboot (v1) boot stub for freestanding Stackexpr kernels.
;
; A Multiboot loader (GRUB, or QEMU's -kernel) starts the kernel in 32-bit
; protected mode. This stub switches the CPU to 64-bit long mode and jumps to
; the program's entry symbol (SX_ENTRY, defined by the compiler from --entry).
;
; On entry to SX_ENTRY:
;   - long mode, interrupts disabled
;   - the first 1 GiB of memory is identity-mapped with 2 MiB pages
;   - rsp points to a 16 KiB stack
;   - rdi holds the Multiboot info pointer, rsi the Multiboot magic (0x2BADB002)
;
; Link with lib/multiboot.ld, which defines the symbols used by the header and
; places the header at the start of the image.

MB_MAGIC     equ 0x1BADB002
MB_FLAGS     equ 0x00010000          ; bit 16: addresses below are valid ("a.out kludge"),
                                     ; which lets the loader accept a 64-bit image
extern _sx_load_start, _sx_load_end, _sx_bss_end
global _sx_mb_entry

section .multiboot
align 4
_sx_mb_header:
    dd MB_MAGIC
    dd MB_FLAGS
    dd -(MB_MAGIC + MB_FLAGS)
    dd _sx_mb_header                 ; header_addr
    dd _sx_load_start                ; load_addr
    dd _sx_load_end                  ; load_end_addr
    dd _sx_bss_end                   ; bss_end_addr (loader zeroes up to here)
    dd _sx_mb_entry                  ; entry_addr

section .data
align 8
_sx_gdt:
    dq 0                             ; null descriptor
    dq 0x00209A0000000000            ; 0x08: 64-bit code, present, ring 0
    dq 0x0000920000000000            ; 0x10: data, present, writable
_sx_gdt_ptr:
    dw $ - _sx_gdt - 1
    dq _sx_gdt

section .bss
align 4096
_sx_pml4:  resb 4096
_sx_pdpt:  resb 4096
_sx_pd:    resb 4096
_sx_stack: resb 16384
_sx_stack_top:

section .text
bits 32
_sx_mb_entry:
    cli
    mov esp, _sx_stack_top
    mov edi, ebx                     ; Multiboot info pointer
    mov esi, eax                     ; Multiboot magic

    ; Page tables: PML4[0] -> PDPT, PDPT[0] -> PD, PD[i] -> 2 MiB page i
    mov eax, _sx_pdpt
    or eax, 0x3                      ; present | writable
    mov [_sx_pml4], eax
    mov eax, _sx_pd
    or eax, 0x3
    mov [_sx_pdpt], eax
    xor ecx, ecx
.map:
    mov eax, ecx
    shl eax, 21                      ; i * 2 MiB
    or eax, 0x83                     ; present | writable | huge
    mov [_sx_pd + ecx * 8], eax
    inc ecx
    cmp ecx, 512
    jne .map

    mov eax, _sx_pml4
    mov cr3, eax
    mov eax, cr4
    or eax, 1 << 5                   ; PAE
    mov cr4, eax
    mov ecx, 0xC0000080              ; EFER
    rdmsr
    or eax, 1 << 8                   ; long mode enable
    wrmsr
    mov eax, cr0
    or eax, (1 << 31) | 1            ; paging | protected mode
    mov cr0, eax

    lgdt [_sx_gdt_ptr]
    jmp 0x08:_sx_long_mode

bits 64
_sx_long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    xor eax, eax
    mov fs, ax
    mov gs, ax
    jmp SX_ENTRY
