CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Werror
INCLUDES := -Isrc -Ithird_party/LexEngine

SOURCES := $(wildcard src/*.cpp)
HEADERS := $(wildcard src/*.hpp) $(wildcard third_party/LexEngine/*.hpp)

KERNEL ?= examples/kernel/vga
KERNEL_FLAGS := --freestanding --entry kmain -T lib/multiboot.ld

.PHONY: all test clean kernel run-kernel

all: build/stackexpr

build/stackexpr: $(SOURCES) $(HEADERS)
	@test -f third_party/LexEngine/lexengine.hpp || { echo "LexEngine missing: run 'git submodule update --init'"; exit 1; }
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(WARNINGS) $(INCLUDES) $(SOURCES) -o $@

test: build/stackexpr
	./tests/run.sh

kernel: build/stackexpr
	./build/stackexpr $(KERNEL).sxp $(KERNEL_FLAGS) -o build/kernel

# Boots the kernel in QEMU, showing the debug console (port 0xe9) in the terminal.
# Close the window (or Ctrl+C) to quit. Pick another kernel with KERNEL=examples/kernel/<name>.
run-kernel: kernel
	qemu-system-x86_64 -kernel build/kernel -debugcon stdio

clean:
	rm -rf build
