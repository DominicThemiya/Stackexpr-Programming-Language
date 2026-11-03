#!/usr/bin/env bash
# Compiles every example and, when nasm is installed, runs it and compares
# its output with tests/expected/<name>.txt. Also checks freestanding output
# and some compile errors.
set -u
cd "$(dirname "$0")/.."
SX=./build/stackexpr
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
fail=0

have_nasm=1
command -v nasm >/dev/null || { have_nasm=0; echo "note: nasm not found, only generating assembly"; }

for src in examples/*.sxp; do
    name=$(basename "$src" .sxp)
    if [ $have_nasm = 1 ]; then
        if ! $SX "$src" -o "$TMP/$name" 2>"$TMP/err"; then
            echo "FAIL $name: compile"; cat "$TMP/err"; fail=1; continue
        fi
        if ! diff -u "tests/expected/$name.txt" <("$TMP/$name"); then
            echo "FAIL $name: output"; fail=1; continue
        fi
    elif ! $SX -S "$src" -o "$TMP/$name" 2>"$TMP/err"; then
        echo "FAIL $name: compile"; cat "$TMP/err"; fail=1; continue
    fi
    echo "ok   $name"
done

# Freestanding: an ELF with a custom entry and linker script, and a flat binary.
kernel=tests/fixtures/halt.sxp
kflags=(--freestanding --entry kmain -T tests/fixtures/flat.ld)
if [ $have_nasm = 1 ]; then
    if $SX "$kernel" "${kflags[@]}" -o "$TMP/kernel.elf" 2>"$TMP/err" &&
       readelf -h "$TMP/kernel.elf" | grep -q 'Entry point address: *0x100000$' &&
       nm "$TMP/kernel.elf" | grep -q '^0000000000100000 T kmain$'; then
        echo "ok   freestanding elf"
    else
        echo "FAIL freestanding elf"; cat "$TMP/err"; fail=1
    fi
    # The binary must be raw code (no ELF header) containing the halt loop: cli; hlt; jmp -4
    if $SX "$kernel" "${kflags[@]}" --format bin -o "$TMP/kernel.bin" 2>"$TMP/err" &&
       [ "$(head -c 4 "$TMP/kernel.bin" | od -An -tx1 | tr -d ' \n')" != "7f454c46" ] &&
       od -An -tx1 -v "$TMP/kernel.bin" | tr -d ' \n' | grep -q "faf4ebfc"; then
        echo "ok   freestanding bin"
    else
        echo "FAIL freestanding bin"; cat "$TMP/err"; fail=1
    fi
elif ! $SX -S "$kernel" "${kflags[@]}" -o "$TMP/kernel" 2>"$TMP/err"; then
    echo "FAIL freestanding: compile"; cat "$TMP/err"; fail=1
fi

# Boot the example kernel in QEMU and check that "Hi" (white on black) is in VGA memory.
if [ $have_nasm = 1 ] && command -v qemu-system-x86_64 >/dev/null; then
    if $SX examples/kernel/vga.sxp --freestanding --entry kmain -T lib/multiboot.ld -o "$TMP/vga" 2>"$TMP/err"; then
        screen=$( (sleep 2; echo "xp /2xh 0xb8000"; echo quit) |
                  timeout 30 qemu-system-x86_64 -kernel "$TMP/vga" -display none -monitor stdio 2>&1 )
        if [[ "$screen" == *"0x0f48 0x0f69"* ]]; then
            echo "ok   qemu boot"
        else
            echo "FAIL qemu boot: VGA memory did not contain 'Hi'"; fail=1
        fi
    else
        echo "FAIL qemu boot: compile"; cat "$TMP/err"; fail=1
    fi
    # Boot the interrupts kernel and compare what it writes to the debug console.
    if $SX examples/kernel/interrupts.sxp --freestanding --entry kmain -T lib/multiboot.ld -o "$TMP/irq" 2>"$TMP/err"; then
        (sleep 3; echo quit) | timeout 30 qemu-system-x86_64 -kernel "$TMP/irq" -display none -monitor stdio \
            -debugcon "file:$TMP/irq.out" >/dev/null 2>&1
        if diff -u tests/expected/kernel-interrupts.txt "$TMP/irq.out"; then
            echo "ok   qemu interrupts"
        else
            echo "FAIL qemu interrupts: debug console output differs"; fail=1
        fi
    else
        echo "FAIL qemu interrupts: compile"; cat "$TMP/err"; fail=1
    fi
else
    echo "note: qemu-system-x86_64 not found, skipping boot tests"
fi

# expect_error <source> <message substring>
expect_error() {
    printf '%b' "$1" > "$TMP/e.sxp"
    if out=$($SX -S "$TMP/e.sxp" -o "$TMP/e" 2>&1); then
        echo "FAIL expected error: $2"; fail=1
    elif [[ "$out" != *"$2"* ]]; then
        echo "FAIL wrong error, wanted '$2', got: $out"; fail=1
    else
        echo "ok   error: $2"
    fi
}

# Syntax and structure
expect_error 'entry\n1\nend\n' "'entry' is no longer supported"
expect_error 'stackexpr foo ( -- ) end\n' 'no entry point'
expect_error 'stackexpr main 1\n' "missing 'end'"
expect_error 'stackexpr f 1 end\nstackexpr main end\n' "needs a stack effect"
expect_error 'stackexpr f (int int) end\nstackexpr main end\n' "missing '--'"
expect_error 'stackexpr f (itn -- ) drop end\nstackexpr main end\n' "unknown type 'itn'"
expect_error 'stackexpr main call main end\n' 'cannot be called'
expect_error 'stackexpr main call nope end\n' "unknown stackexpr 'nope'"
expect_error 'stackexpr main end\nstackexpr main end\n' 'already defined'
expect_error 'stackexpr dup ( -- ) end\nstackexpr main end\n' 'built-in word'
expect_error 'stackexpr main -9223372036854775809 end\n' 'does not fit in 64 bits'
expect_error 'stackexpr main 0x1ffffffffffffffff end\n' 'does not fit in 64 bits'
expect_error 'grab "missing.sxp"\nstackexpr main end\n' "cannot open"
expect_error 'inline stackexpr a ( -- ) call a end\nstackexpr main call a end\n' 'recursive inline stackexpr'

# Stack effects
expect_error 'stackexpr main 1 end\n' "stackexpr 'main' is declared ( -- ) but leaves (int)"
expect_error 'stackexpr main (int -- ) drop end\n' "'main' must have the stack effect ( -- )"
expect_error 'stackexpr f (int -- bool) end\nstackexpr main end\n' "is declared (int -- bool) but leaves (int)"
expect_error 'stackexpr f (a -- b) end\nstackexpr main end\n' "outputs type 'b'"
expect_error 'stackexpr f (int -- int) end\nstackexpr main call f drop end\n' "'call f' expects (int) on top of the stack, but found (nothing)"
expect_error 'extern put (int --)\nstackexpr main true call put end\n' "'call put' expects (int)"
expect_error 'stackexpr main 1 return end\n' "'return' in stackexpr 'main' must leave (nothing)"
expect_error 'stackexpr main return 1 drop end\n' 'unreachable code'

# Types
expect_error 'stackexpr main true 1 + drop end\n' "'+' expects (int int) or (ptr int) or (int ptr) on top of the stack, but found (bool int)"
expect_error 'var v\nstackexpr main v v + drop end\n' "found (ptr ptr)"
expect_error 'stackexpr main 5 @8 drop end\n' "'@8' expects (ptr)"
expect_error 'stackexpr main 1 true == drop end\n' "'==' expects (a a)"
expect_error 'stackexpr main 1 if end end\n' "'if' expects a bool"
expect_error 'stackexpr main true if 1 end end\n' "'if' without 'else' must leave the stack as it found it"
expect_error 'stackexpr main true if 1 else true end drop end\n' 'leave different stacks: (int) and (bool)'
expect_error 'stackexpr main while 1 do end end\n' "'while' condition must push one bool"
expect_error 'stackexpr main 0 while true do 1 end drop end\n' "'while' body must leave the stack as it found it"
expect_error 'stackexpr main break end\n' "only allowed inside a 'while' body"
expect_error 'stackexpr main 0 while true do 1 break end drop end\n' "'break' must leave the stack as it was"
expect_error 'stackexpr main 1 let x in true set x end end\n' "'set x' expects a int"
expect_error 'stackexpr main let x in end end\n' "'let' binds 1 values but the stack has (nothing)"
expect_error 'var x\nstackexpr main 1 let x in end end\n' "would hide a var"
expect_error 'stackexpr main 1 execute (int -- int) drop end\n' "'execute' expects the address to call (ptr)"
expect_error 'struct P\n x int\nend\nvar v\nstackexpr main v P.z drop end\n' "has no field 'z'"
expect_error 'stackexpr f (int -- int) end\nstackexpr main f end\n' "use 'call f' to run it"

# Interrupts
expect_error 'interrupt h (int int --) drop drop end\nstackexpr main end\n' 'interrupt handlers have the stack effect'
expect_error 'interrupt h end\nstackexpr main call h end\n' "can't be called"
expect_error 'inline stackexpr f ( -- ) end\nstackexpr main &f drop end\n' 'has no address'

exit $fail
