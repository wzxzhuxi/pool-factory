#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "poolfactory/pool_factory.hpp"

using namespace poolfactory;

// =============================================================================
// Example 1: Simple Connection Pool
// =============================================================================

struct Connection {
    std::string host;
    int id;
    bool connected{false};

    Connection() = default;
    Connection(std::string h, int i) : host(std::move(h)), id(i), connected(true) {}

    Connection(Connection&&) = default;
    auto operator=(Connection&&) -> Connection& = default;
};

auto demo_connection_pool() -> void {
    std::cout << "=== Connection Pool Demo ===" << std::endl;

    int next_id = 0;

    // Factory lambda: produces connections
    auto factory = [&next_id]() -> Result<Connection> {
        return Result<Connection>::ok(Connection{"localhost:5432", ++next_id});
    };

    // Validator: check if connection is alive
    auto validator = [](const Connection& conn) -> bool { return conn.connected; };

    // Resetter: reset connection state for reuse
    auto resetter = [](Connection& conn) -> Result<Unit> {
        // Reset any transaction state, etc.
        conn.connected = true;
        return Result<Unit>::ok(unit);
    };

    // Create pool with config
    auto pool_result = PoolFactory::create_with_lifecycle<Connection>(
        factory, validator, resetter, connection_pool_config.with_max_size(5));

    pool_result.match(
        [](auto pool) {
            std::cout << "Pool created. Stats: available=" << pool->stats().available
                      << ", max=" << pool->stats().max_size << std::endl;

            // Use bracket pattern - safest way
            pool->with_resource([](Connection& conn) {
                std::cout << "Using connection #" << conn.id << " to " << conn.host << std::endl;
            });

            // Acquire explicitly
            auto conn1 = pool->acquire();
            auto conn2 = pool->acquire();

            conn1.match(
                [](auto& res) { std::cout << "Acquired connection #" << res->id << std::endl; },
                [](const auto& err) { std::cout << "Error: " << err << std::endl; });

            std::cout << "After acquire: available=" << pool->stats().available
                      << ", in_use=" << pool->stats().in_use << std::endl;

            // Resources auto-release when going out of scope
        },
        [](const auto& err) { std::cout << "Failed to create pool: " << err << std::endl; });

    std::cout << std::endl;
}

// =============================================================================
// Example 2: Memory Block Pool
// =============================================================================

template <std::size_t Size> struct MemoryBlock {
    alignas(std::max_align_t) std::array<std::byte, Size> data{};
    bool dirty{false};

    auto ptr() -> void* { return data.data(); }
    static constexpr auto size() -> std::size_t { return Size; }
};

auto demo_memory_pool() -> void {
    std::cout << "=== Memory Pool Demo ===" << std::endl;

    using Block = MemoryBlock<4096>;

    auto factory = []() -> Result<Block> { return Result<Block>::ok(Block{}); };

    auto resetter = [](Block& block) -> Result<Unit> {
        // Zero out memory on return (security)
        std::fill(block.data.begin(), block.data.end(), std::byte{0});
        block.dirty = false;
        return Result<Unit>::ok(unit);
    };

    auto pool_result = PoolFactory::create_with_lifecycle<Block>(
        factory,
        [](const Block&) { return true; },
        resetter,
        memory_pool_config.with_min_size(2).with_max_size(8));

    pool_result.match(
        [](auto pool) {
            std::cout << "Memory pool created. Block size: " << Block::size() << " bytes"
                      << std::endl;
            std::cout << "Pre-warmed: " << pool->stats().available << " blocks" << std::endl;

            // Use memory block
            pool->with_resource([](Block& block) {
                auto* ptr = static_cast<int*>(block.ptr());
                *ptr = 42;
                block.dirty = true;
                std::cout << "Wrote value " << *ptr << " to block" << std::endl;
            });

            std::cout << "After use: available=" << pool->stats().available << std::endl;
        },
        [](const auto& err) { std::cout << "Failed: " << err << std::endl; });

    std::cout << std::endl;
}

// =============================================================================
// Example 3: Thread-Safe Pool with Concurrent Access
// =============================================================================

struct Worker {
    int id;
    std::string status{"idle"};

    Worker() = default;
    explicit Worker(int i) : id(i) {}

    Worker(Worker&&) = default;
    auto operator=(Worker&&) -> Worker& = default;
};

auto demo_thread_safe_pool() -> void {
    std::cout << "=== Thread-Safe Pool Demo ===" << std::endl;

    int next_id = 0;

    auto factory = [&next_id]() -> Result<Worker> { return Result<Worker>::ok(Worker{++next_id}); };

    auto pool_result = PoolFactory::create_thread_safe<Worker>(
        factory, thread_pool_config.with_min_size(2).with_max_size(4));

    pool_result.match(
        [](auto pool) {
            std::cout << "Thread-safe pool created" << std::endl;

            std::vector<std::thread> threads;

            // Spawn multiple threads that compete for resources
            for (int i = 0; i < 6; ++i) {
                threads.emplace_back([pool, i]() {
                    pool->with_resource([i](Worker& w) {
                        w.status = "working";
                        std::cout << "Thread " << i << " using worker #" << w.id << std::endl;
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    });
                });
            }

            for (auto& t : threads) {
                t.join();
            }

            std::cout << "All threads completed. Total workers created: "
                      << pool->stats().total_created << std::endl;
        },
        [](const auto& err) { std::cout << "Failed: " << err << std::endl; });

    std::cout << std::endl;
}

// =============================================================================
// Example 4: Monadic Chaining
// =============================================================================

auto demo_monadic_chaining() -> void {
    std::cout << "=== Monadic Chaining Demo ===" << std::endl;

    auto factory = []() -> Result<int> { return Result<int>::ok(10); };

    // Chain pool creation with resource usage
    make_pool<int>(factory, default_config.with_max_size(3))
        .and_then([](auto pool) { return pool->with_resource([](int& n) { return n * 2; }); })
        .map([](int result) {
            std::cout << "Computed result: " << result << std::endl;
            return result;
        })
        .or_else([](const std::string& err) {
            std::cout << "Error in chain: " << err << std::endl;
            return Result<int>::err(err);
        });

    std::cout << std::endl;
}

// =============================================================================
// Main
// =============================================================================

auto main() -> int {
    demo_connection_pool();
    demo_memory_pool();
    demo_thread_safe_pool();
    demo_monadic_chaining();

    return 0;
}
