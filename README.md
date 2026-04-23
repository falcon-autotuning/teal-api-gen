# Teal Instrument API Generator

A CLI tool for generating compliant Teal files from an Instrument API.

This project provides automation for converting Instrument APIs into typed Teal files that can be used for validation and in the writing of measurement scripts.

## Table of Contents

- [Overview](#overview)
- [Dependencies](#dependencies)
- [Building](#building)
- [Usage](#usage)
- [Testing](#testing)
- [Development](#development)

## Overview

The Teal API Generator transforms Instrument API specifications into strongly-typed Teal files. This enables developers to leverage type safety and static analysis when writing measurement scripts.

## Dependencies

### Required

- **Teal** - The typed Lua dialect
  - **argparse** - Required Teal library for command-line argument parsing
  - **compat53** - Required Teal library for Lua 5.3 compatibility

- **C++ Compiler** - Supports C++17 or later (clang, GCC, or MSVC)
- **CMake** - Version 3.20 or later
- **Make** - For building and testing

### Optional Build Dependencies

- **yaml-cpp** - For API specification parsing (installed via vcpkg)
- **GoogleTest** - For running unit tests
- **sol2** - Lua C++ binding library
- **Lua** - Lua runtime for C++ integration

These dependencies are automatically managed through `vcpkg` when using the provided build system.

### System-Level Dependencies

- **Ninja** - Build system (configured in Makefile)
- **clang/clang++** or compatible C++ compiler
- On Linux: `lld` linker (optional, for faster linking)

## Building

### Quick Start

The project can be built using the Makefile:

```bash
# Build release version (default)
make build-release

# Build debug version
make build-debug

# Configure both debug and release builds
make configure

# Clean all build artifacts
make clean
```

### Detailed Build Process

1. **Bootstrap vcpkg dependencies** (if not already installed):

   ```bash
   make check-vcpkg
   ```

2. **Configure the build**:

   ```bash
   make configure
   ```

3. **Build the project**:

   ```bash
   make build-release    # for optimized release build
   make build-debug      # for debug build with symbols
   ```

The build system automatically:

- Detects your platform (Linux, Windows, macOS)
- Configures the appropriate compiler and build tools
- Manages dependencies through vcpkg
- Generates compilation database for IDE integration

### Build Output

- **CLI Executable**: `teal-api-gen-cli` - The main command-line tool
- **Shared Library**: `libteal-api-gen.so` (Linux) or `teal-api-gen.dll` (Windows)
- **Build Directories**:
  - `build/debug/` - Debug build artifacts
  - `build/release/` - Release build artifacts

## Usage

### CLI Tool

The main CLI tool is `teal-api-gen-cli`, built during the build process.

#### Basic Usage

```bash
./teal-api-gen-cli [OPTIONS] [INPUT_FILE]
```

#### Examples

```bash
# Generate Teal file from an API specification
./teal-api-gen-cli api-spec.yaml

# Display help information
./teal-api-gen-cli --help
```

The tool uses `argparse` for flexible command-line argument handling, making it easy to extend with additional options.

### Output

The CLI generates typed Teal files that can be immediately used in your measurement scripts. Generated files include:

- Type definitions for the Instrument API
- Function signatures with proper typing
- Documentation from the original API specification

## Testing

Run the test suite using the Makefile:

```bash
# Run tests (release build)
make test

# Run tests (debug build)
make test-debug
```

The test suite includes:

- Unit tests for core functionality
- API conversion validation tests
- Teal file generation correctness tests

Tests are automatically built when you run `make test` or `make test-debug` and are executed with proper library paths configured.

## Development

### Project Structure

```
.
├── src/                    # Source code
│   ├── cli.cpp            # CLI entry point
│   └── convert.cpp        # Core conversion logic
├── include/               # Public headers
├── tests/                 # Test suite
│   ├── test_generator.cpp
│   └── test_utils/
├── cmake/                 # CMake configuration files
├── Makefile               # Build system
├── CMakeLists.txt         # CMake build configuration
└── vcpkg.json             # Dependency manifest
```

### Build System Features

- Cross-platform support (Linux, Windows, macOS)
- Automatic dependency resolution through vcpkg
- Support for ccache/sccache for faster incremental builds
- Clangd integration with compilation database generation
- Configurable compiler and linker options

### Extending the Tool

1. **Add new CLI options**: Modify `src/cli.cpp` and update argument parser
2. **Extend API support**: Add conversion logic to `src/convert.cpp`
3. **Add tests**: Create test files in `tests/` directory
4. **Rebuild**: Use `make build-release` to compile changes

## License

See LICENSE file for details.
