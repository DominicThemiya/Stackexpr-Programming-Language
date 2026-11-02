#!/usr/bin/env bash
# Compiles every example and, when nasm is installed, runs it and compares
# its output with tests/expected/<name>.txt. Also checks some compile errors.
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

exit $fail
