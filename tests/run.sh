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
    # The binary must be raw code ending in the halt loop: cli; hlt; jmp -4
    if $SX "$kernel" "${kflags[@]}" --format bin -o "$TMP/kernel.bin" 2>"$TMP/err" &&
       [ "$(tail -c 4 "$TMP/kernel.bin" | od -An -tx1 | tr -d ' \n')" = "faf4ebfc" ]; then
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
else
    echo "note: qemu-system-x86_64 not found, skipping boot test"
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

expect_error 'entry\n1\nend\n' "'entry' is no longer supported"
expect_error 'stackexpr foo 1 end\n' 'no entry point'
expect_error 'stackexpr main 1\n' "missing 'end'"
expect_error 'stackexpr main call a end\nstackexpr a call a end\n' 'recursive stackexpr call'
expect_error 'stackexpr main call main end\n' 'cannot be called'
expect_error 'stackexpr main call nope end\n' "unknown stackexpr 'nope'"
expect_error 'stackexpr main end\nstackexpr main end\n' 'already defined'
expect_error 'stackexpr main -9223372036854775809 end\n' 'does not fit in 64 bits'
expect_error 'stackexpr main 0x1ffffffffffffffff end\n' 'does not fit in 64 bits'

exit $fail
