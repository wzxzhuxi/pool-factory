# poolfactory

[English](README_EN.md)

一个 C++20 函数式资源池库。通过工厂 lambda 创建连接池、线程池、内存池或任意池化资源。

## 设计理念

把工厂模式和函数式编程结合起来：

```
Factory Lambda (原料) → Pool (工厂) → Pooled Resources (产品)
```

- **纯工厂函数**：相同的 `(lambda, config)` → 相同的池行为
- **Result monad**：显式错误处理，不抛异常
- **RAII 无处不在**：资源离开作用域自动归还池
- **可配置线程安全**：单线程池或线程安全池

## 快速开始

```cpp
#include "poolfactory/pool_factory.hpp"

using namespace poolfactory;

// 定义你的资源类型
struct Connection {
    std::string host;
    bool connected{true};
};

// 用工厂 lambda 创建池
auto pool_result = PoolFactory::create<Connection>(
    []() -> Result<Connection> {
        return Result<Connection>::ok(Connection{"localhost:5432"});
    },
    default_config.with_max_size(10)
);

// 用 bracket 模式安全使用资源
pool_result.match(
    [](auto pool) {
        pool->with_resource([](Connection& conn) {
            // 使用连接 - 完成后自动归还
            std::cout << "已连接到 " << conn.host << "\n";
        });
    },
    [](const auto& err) {
        std::cerr << "失败: " << err << "\n";
    }
);
```

## 功能特性

### 池类型

```cpp
// 单线程池（无锁，最快）
auto pool = PoolFactory::create<T>(factory, config);

// 线程安全池（mutex + 条件变量）
auto pool = PoolFactory::create_thread_safe<T>(factory, config);
```

### 配置

```cpp
// 不可变配置 + builder 模式
auto config = default_config
    .with_min_size(4)           // 预热池
    .with_max_size(20)          // 硬性上限
    .with_acquire_timeout(30s)  // 等待超时
    .with_validation(true, false); // 获取/归还时验证

// 预定义配置
thread_pool_config      // min=4, max=16, 无验证
connection_pool_config  // min=2, max=20, 双向验证
memory_pool_config      // min=8, max=64, 无验证
```

### 生命周期钩子

```cpp
auto pool = PoolFactory::create_with_lifecycle<Connection>(
    // Factory: 创建新资源
    []() -> Result<Connection> {
        return Result<Connection>::ok(Connection{});
    },
    // Validator: 检查资源是否有效
    [](const Connection& c) -> bool {
        return c.connected;
    },
    // Resetter: 重置状态以便复用
    [](Connection& c) -> Result<Unit> {
        c.transaction_count = 0;
        return Result<Unit>::ok(unit);
    },
    config
);
```

### 资源使用方式

```cpp
// 方式 1: Bracket 模式（推荐）
pool->with_resource([](MyResource& r) {
    // 保证资源会被释放
    return r.doSomething();
});

// 方式 2: 显式 RAII 句柄
auto resource = pool->acquire();
if (resource.is_ok()) {
    resource.value()->doSomething();
}  // 这里自动释放

// 方式 3: Monadic 链式调用
make_pool<int>(factory, config)
    .and_then([](auto pool) {
        return pool->with_resource([](int& n) { return n * 2; });
    })
    .map([](int result) { return result + 1; })
    .or_else([](const std::string& err) {
        return Result<int>::err(err);
    });
```

### 统计信息

```cpp
auto stats = pool->stats();
// stats.available      - 空闲资源数
// stats.in_use         - 已借出数
// stats.total_created  - 累计创建数
// stats.max_size       - 配置上限
```

## 示例

### 连接池

```cpp
auto pool = PoolFactory::create_thread_safe_validated<DbConnection>(
    [connstr]() -> Result<DbConnection> {
        auto conn = DbConnection::connect(connstr);
        if (!conn) return Result<DbConnection>::err("连接失败");
        return Result<DbConnection>::ok(std::move(*conn));
    },
    [](const DbConnection& c) { return c.ping(); },
    connection_pool_config.with_max_size(50)
);
```

### 内存池

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
        // 归还时清零（安全考虑）
        std::fill(block.data.begin(), block.data.end(), std::byte{0});
        return Result<Unit>::ok(unit);
    },
    memory_pool_config
);
```

### 线程池

```cpp
auto pool = PoolFactory::create_thread_safe<Worker>(
    []() { return Result<Worker>::ok(Worker{}); },
    thread_pool_config
);

// 多线程安全共享池
std::vector<std::thread> threads;
for (int i = 0; i < 10; ++i) {
    threads.emplace_back([pool]() {
        pool->with_resource([](Worker& w) {
            w.execute_task();
        });
    });
}
```

## 构建

```bash
cmake -B build -G Ninja
cmake --build build

# 运行示例
./build/poolfactory
```

## 依赖

- C++20 编译器（GCC 10+、Clang 12+、MSVC 2019+）
- CMake 3.20+

## 许可证

MIT License
