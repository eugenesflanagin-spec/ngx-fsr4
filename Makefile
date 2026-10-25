# Cross-build with llvm-mingw (https://github.com/mstorsjo/llvm-mingw).
LLVM_MINGW ?= /opt/llvm-mingw
CC      := $(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang
CXX     := $(LLVM_MINGW)/bin/x86_64-w64-mingw32-clang++
VERSION := $(shell cat VERSION)
CFLAGS  := -O2 -Wall -Wextra -Wno-unused-parameter -Isrc -DNGX_FSR4_VERSION='"$(VERSION)"'
LDFLAGS := -static -s
DIST    := ngx-fsr4-$(VERSION)-proton

all: build/nvngx.dll build/harness.exe build/gpu.exe

build/ngx.o: src/ngx.c src/ngx_fsr4.h VERSION | build
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

# checks that need no GPU and no Wine: the DLL's exports, the helper script
check: all
	test/check-dll.sh build/nvngx.dll $(LLVM_MINGW)/bin/llvm-objdump
	test/tool-test.sh tools/ngx-fsr4

# the prebuilt tarball for any distribution
dist: all
	rm -rf build/$(DIST)
	mkdir -p build/$(DIST)/tools
	cp build/nvngx.dll build/harness.exe build/gpu.exe build/$(DIST)/
	cp tools/ngx-fsr4 build/$(DIST)/tools/
	cp README.md CHANGELOG.md CREDITS.md LICENSE packaging/INSTALL.txt build/$(DIST)/
	cp docs/USER-GUIDE.md build/$(DIST)/
	cp third_party/ffx/docs/license.md build/$(DIST)/FidelityFX-LICENSE.md
	tar -C build --owner=0 --group=0 --numeric-owner --sort=name \
	    --mtime=@$$(git log -1 --format=%ct 2>/dev/null || echo 0) \
	    -czf build/$(DIST).tar.gz $(DIST)
	@echo build/$(DIST).tar.gz

clean:
	rm -rf build

.PHONY: all check dist clean
