# poolfactory

[中文文档](README_CN.md)

A C++20 functional resource pool library. Create connection pools, thread pools, memory pools, or any pooled resource using factory lambdas.

## Philosophy

Think of it as a factory pattern meets functional programming:

```
Factory Lambda (原料) → Pool (工厂) → Pooled Resources (产品)
```

- **Pure factory functions**: Same `(lambda, config)` → same pool behavior
- **Result monad**: Explicit error handling, no exceptions
- **RAII everywhere**: Resources auto-return to pool on scope exit
- **Configurable thread safety**: Single-threaded or thread-safe pools

## Quick Start

```cpp
#include "poolfactory/pool_factory.hpp"

using namespace poolfactory;

// Define your resource
struct Connection {
    std::string host;
    bool connected{true};
};

// Create a pool with a factory lambda
auto pool_result = PoolFactory::create<Connection>(
    []() -> Result<Connection> {
        return Result<Connection>::ok(Connection{"localhost:5432"});
    },
    default_config.with_max_size(10)
);

// Use resources safely with bracket pattern
pool_result.match(
    [](auto pool) {
        pool->with_resource([](Connection& conn) {
            // Use connection - auto-released when done
            std::cout << "Connected to " << conn.host << "\n";
        });
    },
    [](const auto& err) {
        std::cerr << "Failed: " << err << "\n";
    }
);
```

## Features

### Pool Types

```cpp
// Single-threaded pool (no locks, fastest)
auto pool = PoolFactory::create<T>(factory, config);

// Thread-safe pool (mutex + condition variable)
auto pool = PoolFactory::create_thread_safe<T>(factory, config);
```

### Configuration

```cpp
// Immutable config with builder pattern
auto config = default_config
    .with_min_size(4)           // Pre-warm pool
    .with_max_size(20)          // Hard limit
    .with_acquire_timeout(30s)  // Wait timeout
    .with_validation(true, false); // Validate on acquire/release

// Predefined configs
thread_pool_config      // min=4, max=16, no validation
connection_pool_config  // min=2, max=20, validate both
memory_pool_config      // min=8, max=64, no validation
```

### Lifecycle Hooks

```cpp
auto pool = PoolFactory::create_with_lifecycle<Connection>(
    // Factory: create new resources
    []() -> Result<Connection> {
        return Result<Connection>::ok(Connection{});
    },
    // Validator: check if resource is still valid
    [](const Connection& c) -> bool {
        return c.connected;
    },
    // Resetter: reset state before reuse
    [](Connection& c) -> Result<Unit> {
        c.transaction_count = 0;
        return Result<Unit>::ok(unit);
    },
    config
);
```

### Resource Usage

```cpp
// Option 1: Bracket pattern (recommended)
pool->with_resource([](MyResource& r) {
    // Resource guaranteed to be released
    return r.doSomething();
});

// Option 2: Explicit RAII handle
auto resource = pool->acquire();
if (resource.is_ok()) {
    resource.value()->doSomething();
}  // Auto-released here

// Option 3: Monadic chaining
make_pool<int>(factory, config)
    .and_then([](auto pool) {
        return pool->with_resource([](int& n) { return n * 2; });
    })
    .map([](int result) { return result + 1; })
    .or_else([](const std::string& err) {
        return Result<int>::err(err);
    });
```

### Statistics

```cpp
auto stats = pool->stats();
// stats.available      - idle resources
// stats.in_use         - checked out
// stats.total_created  - lifetime count
// stats.max_size       - config limit
```

## Examples

### Connection Pool

```cpp
auto pool = PoolFactory::create_thread_safe_validated<DbConnection>(
    [connstr]() -> Result<DbConnection> {
        auto conn = DbConnection::connect(connstr);
        if (!conn) return Result<DbConnection>::err("Connection failed");
        return Result<DbConnection>::ok(std::move(*conn));
    },
    [](const DbConnection& c) { return c.ping(); },
    connection_pool_config.with_max_size(50)
);
```

### Memory Pool

```cpp
template <std::size_t Size>
struct MemoryBlock {
    std::array<std::byte, Size> data{};
    auto ptr() -> void* { return data.data(); }
};

auto pool = PoolFactory::create_with_lifecycle<MemoryBlock<4096>>(
    []() { return Result<MemoryBlock<4096>>::ok({}); },
    [](const auto&) { return true; },
    [](auto& block) {
        std::fill(block.data.begin(), block.data.end(), std::byte{0});
        return Result<Unit>::ok(unit);
    },
    memory_pool_config
);
```

### Thread Pool

```cpp
auto pool = PoolFactory::create_thread_safe<Worker>(
    []() { return Result<Worker>::ok(Worker{}); },
    thread_pool_config
);

// Multiple threads safely share the pool
std::vector<std::thread> threads;
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([pool]() {
        pool->with_resource([](Worker& w) {
            w.execute_task();
        });
    });
}
```

## Build

```bash
cmake -B build -G Ninja
cmake --build build

# Run demo
./build/poolfactory
```

## Requirements

- C++20 compiler (GCC 10+, Clang 12+, MSVC 2019+)
- CMake 3.20+

## Tutorial

For a detailed tutorial, see [docs/tutorial.md](docs/tutorial.md) (Chinese), covering:
- Design philosophy and core concepts
- Step-by-step build guide
- Usage patterns and best practices
- Real-world examples
- Design decisions and trade-offs

## License

MIT License
