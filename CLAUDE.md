# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
# Configure and build (Debug)
cmake -B build -G Ninja
cmake --build build

# Build with tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build

# Build Release
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Build with coverage
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DCODE_COVERAGE=ON -DBUILD_TESTING=ON
cmake --build build

# Run tests
ctest --test-dir build --output-on-failure

# Format check
find src include -name '*.cpp' -o -name '*.hpp' | xargs clang-format --dry-run --Werror

# Apply formatting
find src include -name '*.cpp' -o -name '*.hpp' | xargs clang-format -i
```

## Architecture

C++20 project using CMake. Follows functional programming principles.

- `include/poolfactory/` - Headers. `Result<T>` monad for error handling (no exceptions).
- `src/` - Implementation. Pure functions with side effects isolated to `main()`.
- `tests/` - Test files (currently empty, add with `BUILD_TESTING=ON`).

## Code Style

- **Result<T>** over exceptions for error handling
- **Pure functions** - same input → same output, no side effects
- **Side effects at edges** - IO only in main or explicit boundary functions
- LLVM-based formatting (`.clang-format`): 4-space indent, 100 column limit
- clang-tidy enabled for bugprone, cppcoreguidelines, modernize, performance, readability checks
