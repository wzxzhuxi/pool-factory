#pragma once

#include <chrono>
#include <cstddef>

namespace poolfactory {

/**
 * @brief Immutable pool configuration with builder pattern
 *
 * All methods return a new PoolConfig, never mutating the original.
 * This is a pure value type - same input always produces same output.
 */
struct PoolConfig {
    std::size_t min_size{0};
    std::size_t max_size{10};
    std::chrono::milliseconds acquire_timeout{std::chrono::seconds{30}};
    std::chrono::milliseconds idle_timeout{std::chrono::minutes{5}};
    bool validate_on_acquire{true};
    bool validate_on_release{false};

    // Builder methods - pure functions returning new config
    [[nodiscard]] constexpr auto with_min_size(std::size_t n) const -> PoolConfig {
        auto copy = *this;
        copy.min_size = n;
        return copy;
    }

    [[nodiscard]] constexpr auto with_max_size(std::size_t n) const -> PoolConfig {
        auto copy = *this;
        copy.max_size = n;
        return copy;
    }

    [[nodiscard]] constexpr auto with_acquire_timeout(std::chrono::milliseconds t) const
        -> PoolConfig {
        auto copy = *this;
        copy.acquire_timeout = t;
        return copy;
    }

    [[nodiscard]] constexpr auto with_idle_timeout(std::chrono::milliseconds t) const
        -> PoolConfig {
        auto copy = *this;
        copy.idle_timeout = t;
        return copy;
    }

    [[nodiscard]] constexpr auto with_validation(bool on_acquire, bool on_release) const
        -> PoolConfig {
        auto copy = *this;
        copy.validate_on_acquire = on_acquire;
        copy.validate_on_release = on_release;
        return copy;
    }

    constexpr auto operator==(const PoolConfig&) const -> bool = default;
};

// Predefined configs as constexpr values
inline constexpr PoolConfig default_config{};

inline constexpr PoolConfig thread_pool_config =
    default_config.with_min_size(4).with_max_size(16).with_validation(false, false);

inline constexpr PoolConfig connection_pool_config =
    default_config.with_min_size(2).with_max_size(20).with_validation(true, true);

inline constexpr PoolConfig memory_pool_config =
    default_config.with_min_size(8).with_max_size(64).with_validation(false, false);

} // namespace poolfactory
