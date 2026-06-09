.PHONY: help configure build test clean install vcpkg-bootstrap 

# Default to using standard system shell  
SHELL ?= /bin/sh

# Build preset (user can override: make build PRESET=linux-gcc-release)
PRESET ?= linux-clang-release
CMAKE_BUILD_DIR := build/$(PRESET)
ifeq ($(OS),Windows_NT)
  SUDO :=
else
  SUDO := sudo
endif

help:
	@echo "Teal API Gen Build System"
	@echo "============================="
	@echo ""
	@echo "Available presets:"
	@cmake --list-presets=all
	@echo ""
	@echo "Usage:"
	@echo "  make configure PRESET=<preset>  - Configure build (default: $(PRESET))"
	@echo "  make build PRESET=<preset>      - Build (default: $(PRESET))"
	@echo "  make test PRESET=<preset>       - Run tests (default: $(PRESET))"
	@echo "  make install PRESET=<preset>    - Install to system"
	@echo "  make clean                      - Clean all build artifacts"
	@echo ""
	@echo "Examples:"
	@echo "  make build                                      # Build with clang (default)"
	@echo "  make build PRESET=linux-gcc-release             # Build with gcc"
	@echo "  make test PRESET=linux-clang-release            # Run tests"
	@echo "  make install PRESET=linux-clang-release         # Install"

vcpkg-bootstrap:
	@echo "Bootstrapping vcpkg..."
	MAKELEVEL=0 cmake -P cmake/bootstrap/bootstrap-vcpkg.cmake

configure: vcpkg-bootstrap
	@echo "Configuring $(PRESET)..."
	MAKELEVEL=0 cmake --preset $(PRESET)

build: configure
	@echo "Building $(PRESET)..."
	cmake --build --preset $(PRESET)

# use POSIX . instead of source to ensure compatibility with /bin/sh on all platforms
test: build
	ctest --preset $(PRESET) --output-on-failure

install: build
	@echo "Installing $(PRESET) to system..."
	$(SUDO) cmake --install $(CMAKE_BUILD_DIR)

clean:
	@echo "Cleaning all build artifacts..."
	rm -rf build vcpkg_installed
	@echo "✓ Clean complete"

