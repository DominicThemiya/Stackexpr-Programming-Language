CXX ?= g++
CXXFLAGS ?= -std=c++17 -O2 -g
WARNINGS := -Wall -Wextra -Wpedantic -Werror
INCLUDES := -Isrc -Ithird_party/LexEngine

SOURCES := $(wildcard src/*.cpp)
HEADERS := $(wildcard src/*.hpp) $(wildcard third_party/LexEngine/*.hpp)

.PHONY: all test clean

all: build/stackexpr

build/stackexpr: $(SOURCES) $(HEADERS)
	@test -f third_party/LexEngine/lexengine.hpp || { echo "LexEngine missing: run 'git submodule update --init'"; exit 1; }
	@mkdir -p build
	$(CXX) $(CXXFLAGS) $(WARNINGS) $(INCLUDES) $(SOURCES) -o $@

test: build/stackexpr
	./tests/run.sh

clean:
	rm -rf build
