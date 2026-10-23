# Cross-build with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
LLVM_MINGW ?= /opt/llvm-mingw
CC      := $(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang
CXX     := $(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang++
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -Isrc
LDFLAGS := -static -s

all: build/nvngx.dll build/harness.exe build/gpu.exe

build/ngx.o: src/ngx.c src/ngx_fsr4.h | build
	$(CC) $(CFLAGS) -c -o $@ src/ngx.c

build/fsr.o: src/fsr.cpp src/ngx_fsr4.h | build
	$(CXX) $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti -c -o $@ src/fsr.cpp

build/nvngx.dll: build/ngx.o build/fsr.o
	$(CXX) -shared -o $@ build/ngx.o build/fsr.o $(LDFLAGS)

build/harness.exe: test/harness.c | build
	$(CC) $(CFLAGS) -o $@ test/harness.c $(LDFLAGS)

build/gpu.exe: test/gpu.c | build
	$(CC) $(CFLAGS) -o $@ test/gpu.c $(LDFLAGS) -ld3d12

build:
	mkdir -p build

clean:
	rm -rf build

.PHONY: all clean
