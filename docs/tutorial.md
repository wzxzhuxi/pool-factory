# PoolFactory 教程：用函数式思维构建资源池

> 本教程将带你从零理解如何用 C++20 和函数式编程思想构建一个通用的资源池库。

---

## 目录

1. [设计哲学](#1-设计哲学)
2. [核心概念](#2-核心概念)
3. [从零构建](#3-从零构建)
   - [Unit 类型](#31-unit-类型)
   - [Result Monad](#32-result-monad)
   - [Concepts 约束](#33-concepts-约束)
   - [不可变配置](#34-不可变配置)
   - [RAII 资源句柄](#35-raii-资源句柄)
   - [资源池实现](#36-资源池实现)
   - [池工厂](#37-池工厂)
4. [使用模式](#4-使用模式)
5. [实战案例](#5-实战案例)
6. [设计决策与权衡](#6-设计决策与权衡)

---

## 1. 设计哲学

### 1.1 工厂思维看函数式编程

函数式编程的本质可以用工厂模式来理解：

```
原料 (参数) → 工厂 (函数) → 产品 (返回值)
```

这种类比揭示了函数式编程的核心：
- **纯函数** = 稳定的工厂流水线：相同原料必然产出相同产品
- **不可变数据** = 产品出厂后不能被篡改
- **组合** = 工厂之间通过传送带连接，形成生产链

### 1.2 池化工厂的设计理念

基于这种思维，我们设计了 PoolFactory：

```
Factory Lambda (原料) → PoolFactory (工厂) → Pool (产品工厂) → Resources (产品)
```

关键设计原则：

1. **纯工厂函数**：`create(factory, config)` 是声明式的 —— 你描述"要什么"，而不是"怎么做"
2. **Result Monad**：所有可能失败的操作都返回 `Result<T>`，显式处理错误
3. **RAII 无处不在**：资源离开作用域自动归还，无法忘记释放
4. **副作用在边界**：核心逻辑是纯的，副作用被推到程序边缘

---

## 2. 核心概念

### 2.1 为什么需要资源池？

资源池解决的核心问题：**创建成本高昂的资源需要复用**。

典型场景：
- **数据库连接池**：建立TCP连接、认证需要几十毫秒
- **线程池**：创建线程涉及系统调用，开销大
- **内存池**：频繁分配/释放导致内存碎片

池化的本质：**用空间换时间**，预先创建资源，按需借出/归还。

### 2.2 函数式 vs 面向对象

传统 OOP 方式（Java 风格）：
```java
class ConnectionPool {
    private List<Connection> available;
    private int maxSize;

    public Connection acquire() { ... }  // 可能返回 null
    public void release(Connection c) { ... }  // 可能忘记调用
}
```

问题：
- `acquire()` 返回 null 或抛异常，调用者必须小心处理
- `release()` 是手动调用，容易忘记
- 内部状态可变，并发时需要小心

函数式方式（本项目）：
```cpp
auto pool = PoolFactory::create<Connection>(factory, config);
pool->with_resource([](Connection& conn) {
    // 使用连接，完成后自动归还
});
```

优势：
- `with_resource` 保证资源释放，通过类型系统强制正确使用
- `Result<T>` 显式表达可能的失败
- 不可变配置，线程安全的共享

---

## 3. 从零构建

### 3.1 Unit 类型

**文件：** `include/poolfactory/unit.hpp`

**问题：** C++ 的 `void` 不是一等公民 —— 你不能声明 `void` 类型的变量，不能把 `void` 存入容器。

**解决方案：** 引入 `Unit` 类型，表示"没有有意义的值"。

```cpp
namespace poolfactory {

struct Unit {
    // 所有 Unit 值相等
    constexpr auto operator==(const Unit&) const -> bool = default;
    constexpr auto operator<=>(const Unit&) const = default;
};

// 单例值，类似 Rust 的 () 或 Haskell 的 ()
inline constexpr Unit unit{};

}
```

**设计决策：**

| 选项 | 优点 | 缺点 |
|------|------|------|
| 使用 `void` | C++ 原生 | 不能存入 variant，不能作为值传递 |
| 使用 `std::monostate` | 标准库类型 | 语义不够明确 |
| 自定义 `Unit` | 语义清晰，命名来自 Haskell/Rust | 需要额外定义 |

`Unit` 让我们可以写：
```cpp
Result<Unit> doSomething();  // 明确表示：可能失败，成功时没有返回值
```

而不是：
```cpp
bool doSomething();  // true/false 语义不明确，无法携带错误信息
void doSomething();  // 要么成功要么抛异常，不是显式错误处理
```

---

### 3.2 Result Monad

**文件：** `include/poolfactory/result.hpp`

**问题：** 函数可能成功也可能失败，如何显式表达？

传统方式的问题：
```cpp
// 方式1：返回 null/nullptr
Connection* connect();  // 返回 null 表示失败，但为什么失败？

// 方式2：抛异常
Connection connect();   // 抛异常，调用者可能忘记 try-catch

// 方式3：out 参数
bool connect(Connection& out, std::string& error);  // 丑陋
```

**解决方案：** `Result<T, E>` monad —— 一个值要么是成功的 `T`，要么是失败的 `E`。

#### 3.2.1 Tagged Union 设计

核心问题：如果 `T` 和 `E` 是相同类型怎么办？

```cpp
Result<std::string, std::string> parse(input);
// "hello" 是成功值还是错误信息？无法区分！
```

解决方案：用标签包装器消除歧义：

```cpp
template <typename T> struct Ok {
    T value;
    explicit Ok(T v) : value(std::move(v)) {}
};

template <typename E> struct Err {
    E value;
    explicit Err(E v) : value(std::move(v)) {}
};

// 推导指引，让 Ok{42} 自动推导为 Ok<int>
template <typename T> Ok(T) -> Ok<T>;
template <typename E> Err(E) -> Err<E>;
```

现在可以明确区分：
```cpp
Result<std::string, std::string> r1 = Ok{"success"};   // 成功
Result<std::string, std::string> r2 = Err{"failed"};   // 失败
```

#### 3.2.2 Result 类实现

```cpp
template <typename T, typename E = std::string>
class Result {
public:
    // 工厂方法：创建成功/失败值
    static auto ok(T value) -> Result { return Result{Ok<T>{std::move(value)}}; }
    static auto err(E error) -> Result { return Result{Err<E>{std::move(error)}}; }

    // 从标签构造
    Result(Ok<T> ok) : data_(std::move(ok)) {}
    Result(Err<E> err) : data_(std::move(err)) {}

    // 查询状态
    [[nodiscard]] auto is_ok() const -> bool {
        return std::holds_alternative<Ok<T>>(data_);
    }
    [[nodiscard]] auto is_err() const -> bool { return !is_ok(); }

    // 访问值（仅在 is_ok() 为真时有效）
    [[nodiscard]] auto value() const& -> const T& {
        return std::get<Ok<T>>(data_).value;
    }
    [[nodiscard]] auto value() && -> T {
        return std::move(std::get<Ok<T>>(data_).value);
    }

    // 访问错误（仅在 is_err() 为真时有效）
    [[nodiscard]] auto error() const& -> const E& {
        return std::get<Err<E>>(data_).value;
    }

private:
    std::variant<Ok<T>, Err<E>> data_;
};
```

#### 3.2.3 Monad 操作

`Result` 不只是一个容器，它是一个 **Monad**，支持链式组合：

**`map`：转换成功值**
```cpp
template <typename F>
auto map(F&& f) const& -> Result<decltype(f(value())), E> {
    if (is_ok()) {
        return Result<...>::ok(f(value()));
    }
    return Result<...>::err(error());  // 错误原样传递
}
```

使用示例：
```cpp
Result<int>::ok(5)
    .map([](int n) { return n * 2; })    // Result<int>::ok(10)
    .map([](int n) { return std::to_string(n); });  // Result<string>::ok("10")
```

**`and_then`：链式操作（flatMap/bind）**

当转换函数本身返回 `Result` 时使用：
```cpp
template <typename F>
auto and_then(F&& f) const& -> decltype(f(value())) {
    if (is_ok()) {
        return f(value());  // f 返回 Result<U>
    }
    return decltype(f(value()))::err(error());
}
```

使用示例：
```cpp
auto parseInt = [](const std::string& s) -> Result<int> {
    try { return Result<int>::ok(std::stoi(s)); }
    catch (...) { return Result<int>::err("parse failed"); }
};

Result<std::string>::ok("42")
    .and_then(parseInt)  // Result<int>::ok(42)
    .map([](int n) { return n + 1; });  // Result<int>::ok(43)
```

**`match`：模式匹配**

强制处理两种情况：
```cpp
template <typename OnOk, typename OnErr>
auto match(OnOk&& on_ok, OnErr&& on_err) const& {
    if (is_ok()) {
        return on_ok(value());
    }
    return on_err(error());
}
```

使用示例：
```cpp
result.match(
    [](int value) { std::cout << "Got: " << value << "\n"; },
    [](const std::string& err) { std::cerr << "Error: " << err << "\n"; }
);
```

---

### 3.3 Concepts 约束

**文件：** `include/poolfactory/concepts.hpp`

C++20 Concepts 让我们在编译期约束类型，提供更好的错误信息。

```cpp
// 可池化的资源必须可移动和可析构
template <typename T>
concept Poolable = std::movable<T> && std::destructible<T>;

// 资源工厂：无参函数，返回 Result<T>
template <typename F, typename T>
concept ResourceFactory = std::invocable<F> && requires(F f) {
    { f() } -> std::same_as<Result<T>>;
};

// 验证器：接受 const T&，返回 bool
template <typename F, typename T>
concept ResourceValidator = std::invocable<F, const T&> && requires(F f, const T& r) {
    { f(r) } -> std::convertible_to<bool>;
};

// 重置器：接受 T&，返回 Result<Unit>
template <typename F, typename T>
concept ResourceResetter = std::invocable<F, T&> && requires(F f, T& r) {
    { f(r) } -> std::same_as<Result<Unit>>;
};
```

**为什么这样设计？**

| Concept | 约束 | 原因 |
|---------|------|------|
| `Poolable` | movable + destructible | 资源需要在池中移动，需要能析构 |
| `ResourceFactory` | `() -> Result<T>` | 创建可能失败，用 Result 表达 |
| `ResourceValidator` | `(const T&) -> bool` | 只读检查，不修改资源 |
| `ResourceResetter` | `(T&) -> Result<Unit>` | 可能修改资源，可能失败 |

使用 Concepts 的好处：

```cpp
// 如果传入不符合约束的类型，编译器给出清晰错误：
// "constraint 'ResourceFactory<F, T>' was not satisfied"
template <Poolable T, typename Factory>
    requires ResourceFactory<Factory, T>
auto create(Factory factory, PoolConfig config);
```

---

### 3.4 不可变配置

**文件：** `include/poolfactory/pool_config.hpp`

**原则：** 配置是不可变的值类型，修改配置返回新配置。

```cpp
struct PoolConfig {
    std::size_t min_size{0};
    std::size_t max_size{10};
    std::chrono::milliseconds acquire_timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds idle_timeout{std::chrono::minutes{5}};
    bool validate_on_acquire{true};
    bool validate_on_release{false};

    // Builder 方法：返回新配置，不修改原配置
    [[nodiscard]] constexpr auto with_max_size(std::size_t n) const -> PoolConfig {
        auto copy = *this;
        copy.max_size = n;
        return copy;
    }

    // 其他 with_xxx 方法类似...
};

// 预定义配置
inline constexpr PoolConfig default_config{};
inline constexpr PoolConfig thread_pool_config =
    default_config.with_min_size(4).with_max_size(16);
inline constexpr PoolConfig connection_pool_config =
    default_config.with_min_size(2).with_max_size(20).with_validation(true, true);
```

**Builder 模式的函数式变体：**

传统 Builder（可变）：
```cpp
builder.setMaxSize(10);  // 修改 builder 自身
builder.setMinSize(2);   // 继续修改
auto config = builder.build();
```

函数式 Builder（不可变）：
```cpp
auto config = default_config
    .with_max_size(10)   // 返回新 PoolConfig
    .with_min_size(2);   // 链式调用
```

优势：
- **线程安全**：配置可以自由共享，无需同步
- **可追溯**：原配置不变，可以基于同一个 base 创建多个变体
- **constexpr**：配置可在编译期创建

---

### 3.5 RAII 资源句柄

**文件：** `include/poolfactory/pooled_resource.hpp`

**核心思想：** 资源的释放绑定到对象的生命周期，编译器保证调用析构函数。

```cpp
template <Poolable T>
class PooledResource {
public:
    using Releaser = std::function<void(T)>;

    // 禁止拷贝：资源所有权是独占的
    PooledResource(const PooledResource&) = delete;
    auto operator=(const PooledResource&) -> PooledResource& = delete;

    // 允许移动：转移所有权
    PooledResource(PooledResource&& other) noexcept
        : resource_(std::move(other.resource_)),
          releaser_(std::move(other.releaser_)) {
        other.releaser_ = nullptr;  // 防止被移动后的对象释放资源
    }

    // 析构时自动归还
    ~PooledResource() { release(); }

    // 访问底层资源
    [[nodiscard]] auto get() -> T& { return *resource_; }
    auto operator->() -> T* { return &(*resource_); }
    auto operator*() -> T& { return *resource_; }

    // 函数式访问
    template <typename F>
    auto use(F&& f) -> decltype(f(std::declval<T&>())) {
        return f(*resource_);
    }

private:
    void release() {
        if (resource_ && releaser_) {
            releaser_(std::move(*resource_));
            resource_.reset();
            releaser_ = nullptr;
        }
    }

    std::optional<T> resource_;
    Releaser releaser_;  // 归还回调，由 Pool 提供
};
```

**RAII 保证：**

```cpp
{
    auto conn = pool->acquire();  // 借出资源
    conn->query("SELECT ...");
    // ...使用连接...
}  // 离开作用域，~PooledResource() 自动调用 releaser_，归还资源
```

无论如何离开作用域（正常返回、异常、break/continue），资源都会归还。

---

### 3.6 资源池实现

**文件：** `include/poolfactory/pool.hpp`

#### 3.6.1 池统计（纯读取）

```cpp
struct PoolStats {
    std::size_t available;      // 空闲资源数
    std::size_t in_use;         // 已借出数
    std::size_t total_created;  // 累计创建数
    std::size_t max_size;       // 配置上限
};
```

#### 3.6.2 单线程池

```cpp
template <Poolable T>
class Pool {
public:
    using Factory = std::function<Result<T>()>;
    using Validator = std::function<bool(const T&)>;
    using Resetter = std::function<Result<Unit>(T&)>;

    // 核心操作：借出资源
    [[nodiscard]] virtual auto acquire() -> Result<PooledResource<T>> {
        // 1. 尝试从空闲队列获取
        if (!available_.empty()) {
            T resource = std::move(available_.front());
            available_.pop_front();

            // 验证资源有效性
            if (config_.validate_on_acquire && validator_ && !validator_(resource)) {
                return create_and_wrap();  // 无效，创建新的
            }

            ++in_use_;
            return wrap_resource(std::move(resource));
        }

        // 2. 空闲队列为空，检查是否能创建新资源
        if (in_use_ >= config_.max_size) {
            return Result<PooledResource<T>>::err("Pool exhausted");
        }

        // 3. 创建新资源
        return create_and_wrap();
    }

    // Bracket 模式：推荐的 API
    template <typename F>
    auto with_resource(F&& f) -> Result<...> {
        auto acquired = acquire();
        if (acquired.is_err()) {
            return Result<...>::err(std::move(acquired).error());
        }

        auto resource = std::move(acquired).value();
        // 资源在 lambda 执行后自动归还
        return Result<...>::ok(f(resource.get()));
    }

protected:
    // 归还资源
    virtual void do_release(T resource) {
        --in_use_;

        // 重置资源状态
        if (resetter_) {
            auto reset_result = resetter_(resource);
            if (reset_result.is_err()) {
                return;  // 重置失败，丢弃资源
            }
        }

        // 验证后归还
        if (config_.validate_on_release && validator_ && !validator_(resource)) {
            return;  // 验证失败，丢弃
        }

        available_.push_back(std::move(resource));
    }

private:
    Factory factory_;
    Validator validator_;
    Resetter resetter_;
    PoolConfig config_;

    std::deque<T> available_;  // 空闲资源队列
    std::size_t in_use_{0};
    std::size_t total_created_{0};
};
```

#### 3.6.3 处理 void 返回类型

`with_resource` 的返回类型需要特殊处理：

```cpp
template <typename F>
auto with_resource(F&& f)
    -> Result<std::conditional_t<
           std::is_void_v<std::invoke_result_t<F, T&>>,
           Unit,                        // F 返回 void → 用 Unit
           std::invoke_result_t<F, T&>  // 否则使用 F 的返回类型
       >>
{
    using RawR = std::invoke_result_t<F, T&>;
    using R = std::conditional_t<std::is_void_v<RawR>, Unit, RawR>;

    auto acquired = acquire();
    if (acquired.is_err()) {
        return Result<R>::err(std::move(acquired).error());
    }

    auto resource = std::move(acquired).value();

    if constexpr (std::is_void_v<RawR>) {
        f(resource.get());
        return Result<R>::ok(unit);  // 返回 Unit
    } else {
        return Result<R>::ok(f(resource.get()));
    }
}
```

#### 3.6.4 线程安全池

```cpp
template <Poolable T>
class ThreadSafePool : public Pool<T> {
public:
    [[nodiscard]] auto acquire() -> Result<PooledResource<T>> override {
        std::unique_lock lock(mutex_);

        auto deadline = std::chrono::steady_clock::now() +
                        this->config_.acquire_timeout;

        // 等待资源可用或有空位创建新资源
        while (this->available_.empty() &&
               this->in_use_ >= this->config_.max_size) {
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return Result<...>::err("Pool acquire timeout");
            }
        }

        // ... 获取或创建资源 ...
    }

protected:
    void do_release(T resource) override {
        {
            std::lock_guard lock(mutex_);
            Pool<T>::do_release(std::move(resource));
        }
        cv_.notify_one();  // 唤醒等待的线程
    }

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
};
```

**线程安全设计：**
- `mutex_` 保护所有状态访问
- `condition_variable` 实现阻塞等待
- `notify_one()` 在释放时唤醒等待线程
- 超时机制防止永久阻塞

---

### 3.7 池工厂

**文件：** `include/poolfactory/pool_factory.hpp`

工厂是纯函数：相同的 `(factory, config)` 输入产生行为相同的池。

```cpp
class PoolFactory {
public:
    PoolFactory() = delete;  // 禁止实例化，只有静态方法

    // 基础创建
    template <Poolable T, typename Factory>
        requires ResourceFactory<Factory, T>
    [[nodiscard]] static auto create(Factory factory, PoolConfig config = default_config)
        -> Result<std::shared_ptr<Pool<T>>>;

    // 带验证器
    template <Poolable T, typename Factory, typename Validator>
        requires ResourceFactory<Factory, T> && ResourceValidator<Validator, T>
    [[nodiscard]] static auto create_validated(Factory, Validator, PoolConfig)
        -> Result<std::shared_ptr<Pool<T>>>;

    // 完整生命周期
    template <Poolable T, typename Factory, typename Validator, typename Resetter>
        requires ResourceFactory<Factory, T> &&
                 ResourceValidator<Validator, T> &&
                 ResourceResetter<Resetter, T>
    [[nodiscard]] static auto create_with_lifecycle(Factory, Validator, Resetter, PoolConfig)
        -> Result<std::shared_ptr<Pool<T>>>;

    // 线程安全版本
    template <Poolable T, typename Factory>
    [[nodiscard]] static auto create_thread_safe(Factory, PoolConfig)
        -> Result<std::shared_ptr<ThreadSafePool<T>>>;

    // ... 更多变体 ...

private:
    // 配置验证：纯函数
    [[nodiscard]] static auto validate_config(const PoolConfig& config)
        -> Result<Unit> {
        if (config.max_size == 0) {
            return Result<Unit>::err("max_size cannot be 0");
        }
        if (config.min_size > config.max_size) {
            return Result<Unit>::err("min_size cannot exceed max_size");
        }
        return Result<Unit>::ok(unit);
    }
};

// 便捷函数
template <Poolable T, typename Factory>
auto make_pool(Factory factory, PoolConfig config = default_config) {
    return PoolFactory::create<T>(std::move(factory), config);
}
```

---

## 4. 使用模式

### 4.1 Bracket 模式（推荐）

```cpp
pool->with_resource([](Connection& conn) {
    conn.execute("SELECT * FROM users");
    // 无论是否异常，资源都会归还
});
```

**优势：**
- 不可能忘记释放资源
- 异常安全
- 意图清晰

### 4.2 显式 RAII 句柄

```cpp
{
    auto result = pool->acquire();
    if (result.is_ok()) {
        auto& conn = result.value();
        conn->execute("...");
    }
}  // 自动释放
```

**适用场景：** 需要在多个步骤间保持资源

### 4.3 Monadic 链式调用

```cpp
make_pool<int>(factory, config)
    .and_then([](auto pool) {
        return pool->with_resource([](int& n) {
            return n * 2;
        });
    })
    .map([](int result) {
        return result + 1;
    })
    .or_else([](const std::string& err) {
        std::cerr << "Error: " << err << "\n";
        return Result<int>::err(err);
    });
```

**适用场景：** 复杂的操作流水线

### 4.4 模式匹配

```cpp
pool_result.match(
    [](auto pool) {
        // 成功：使用池
        pool->with_resource([](auto& r) { ... });
    },
    [](const std::string& err) {
        // 失败：处理错误
        std::cerr << "Failed: " << err << "\n";
    }
);
```

---

## 5. 实战案例

### 5.1 数据库连接池

```cpp
struct DbConnection {
    std::string connstr;
    bool connected{false};
    int transaction_count{0};
};

auto create_db_pool() {
    return PoolFactory::create_with_lifecycle<DbConnection>(
        // Factory: 创建连接
        []() -> Result<DbConnection> {
            DbConnection conn{"postgres://localhost:5432/db"};
            // 实际连接逻辑...
            conn.connected = true;
            return Result<DbConnection>::ok(std::move(conn));
        },
        // Validator: 检查连接有效性
        [](const DbConnection& c) -> bool {
            return c.connected;  // 实际应该 ping 服务器
        },
        // Resetter: 重置连接状态
        [](DbConnection& c) -> Result<Unit> {
            c.transaction_count = 0;
            // 回滚未提交事务...
            return Result<Unit>::ok(unit);
        },
        connection_pool_config.with_max_size(50)
    );
}
```

### 5.2 内存池

```cpp
template <std::size_t BlockSize>
struct MemoryBlock {
    alignas(std::max_align_t) std::array<std::byte, BlockSize> data{};

    auto ptr() -> void* { return data.data(); }
};

auto create_memory_pool() {
    using Block = MemoryBlock<4096>;

    return PoolFactory::create_with_lifecycle<Block>(
        []() { return Result<Block>::ok(Block{}); },
        [](const Block&) { return true; },  // 总是有效
        [](Block& b) {
            // 安全擦除：归还前清零
            std::fill(b.data.begin(), b.data.end(), std::byte{0});
            return Result<Unit>::ok(unit);
        },
        memory_pool_config.with_min_size(16)
    );
}
```

### 5.3 线程安全工作池

```cpp
struct Worker {
    int id;
    void execute(const Task& task) { /* ... */ }
};

auto pool = PoolFactory::create_thread_safe<Worker>(
    [id = 0]() mutable -> Result<Worker> {
        return Result<Worker>::ok(Worker{++id});
    },
    thread_pool_config
);

// 多线程安全使用
std::vector<std::thread> threads;
for (int i = 0; i < 100; ++i) {
    threads.emplace_back([&pool, task]() {
        pool->with_resource([&task](Worker& w) {
            w.execute(task);
        });
    });
}
```

---

## 6. 设计决策与权衡

### 6.1 为什么用 `shared_ptr<Pool<T>>` 而不是值类型？

**问题：** Pool 内部持有 releaser 闭包，闭包捕获 `this` 指针。如果 Pool 被移动，`this` 失效。

**解决方案：**
- Pool 禁止移动和拷贝
- 通过 `shared_ptr` 共享所有权
- releaser 捕获的 `this` 永远有效

### 6.2 为什么返回 `Result<shared_ptr<Pool<T>>>` 而不是 `shared_ptr<Pool<T>>`？

创建池可能失败：
- 配置无效（max_size = 0）
- 预热资源创建失败

显式表达这种可能性：
```cpp
auto pool = PoolFactory::create<T>(factory, config);
// pool 是 Result，必须处理两种情况
```

### 6.3 Validator 和 Resetter 的职责分离

| 组件 | 职责 | 时机 |
|------|------|------|
| Validator | 检查资源是否可用 | acquire 时、release 时（可配置） |
| Resetter | 重置资源状态 | release 时 |

为什么分开：
- 关注点分离：检查 vs 修改
- 可选配置：可以只要验证器，不要重置器
- 测试容易：可以独立测试各个组件

### 6.4 为什么 `with_resource` 比 `acquire` 更推荐？

```cpp
// acquire：需要手动管理生命周期
auto res = pool->acquire();
if (res.is_ok()) {
    auto& r = res.value();
    // 使用 r...
}  // r 在这里释放

// with_resource：生命周期由库管理
pool->with_resource([](auto& r) {
    // 使用 r，完成后自动释放
});
```

`with_resource` 的优势：
- 无法忘记释放
- 生命周期边界清晰
- 符合"最小权限"原则：只在需要时持有资源

---

## 附录：完整类型依赖图

```
                    ┌─────────────────────┐
                    │       Unit          │
                    └─────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                       Result<T, E>                      │
│  (Ok<T> / Err<E> tagged union + map/and_then/match)     │
└─────────────────────────────────────────────────────────┘
                              │
                              ▼
┌─────────────────────────────────────────────────────────┐
│                       Concepts                          │
│  (Poolable, ResourceFactory, Validator, Resetter)       │
└─────────────────────────────────────────────────────────┘
                              │
              ┌───────────────┼───────────────┐
              ▼               ▼               ▼
       PoolConfig      PooledResource<T>    PoolStats
              │               │
              └───────┬───────┘
                      ▼
              ┌───────────────┐
              │   Pool<T>     │
              └───────────────┘
                      │
                      ▼
            ┌─────────────────┐
            │ThreadSafePool<T>│
            └─────────────────┘
                      │
                      ▼
              ┌───────────────┐
              │  PoolFactory  │
              └───────────────┘
```

---

## 练习题

1. **扩展 Result**：实现 `map_err` 方法，允许转换错误类型
2. **添加统计**：记录 acquire 成功/失败次数、平均等待时间
3. **实现对象池**：创建一个预分配固定数量对象的池
4. **健康检查**：添加定期验证空闲资源的后台线程
