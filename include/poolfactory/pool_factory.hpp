#pragma once

#include <memory>

#include "poolfactory/concepts.hpp"
#include "poolfactory/pool.hpp"
#include "poolfactory/pool_config.hpp"
#include "poolfactory/result.hpp"
#include "poolfactory/unit.hpp"

namespace poolfactory {

/**
 * @brief PoolFactory - Creates pools from factory lambdas
 *
 * The factory functions are conceptually pure:
 * Same (factory_fn, config) -> Same pool behavior
 *
 * This is the "pure" entry point - you describe WHAT you want,
 * and the factory produces a pool that will do it.
 */
class PoolFactory {
  public:
    PoolFactory() = delete;

    // =========================================================================
    // Single-threaded pool creation
    // =========================================================================

    /**
     * @brief Create a single-threaded pool from a factory lambda
     */
    template <Poolable T, typename Factory>
        requires ResourceFactory<Factory, T>
    [[nodiscard]] static auto create(Factory factory, PoolConfig config = default_config)
        -> Result<std::shared_ptr<Pool<T>>> {

        return create_with_lifecycle<T>(
            std::move(factory),
            [](const T&) { return true; },
            [](T&) { return Result<Unit>::ok(unit); },
            config);
    }

    /**
     * @brief Create a single-threaded pool with custom validation
     */
    template <Poolable T, typename Factory, typename Validator>
        requires ResourceFactory<Factory, T> && ResourceValidator<Validator, T>
    [[nodiscard]] static auto
    create_validated(Factory factory, Validator validator, PoolConfig config = default_config)
        -> Result<std::shared_ptr<Pool<T>>> {

        return create_with_lifecycle<T>(
            std::move(factory),
            std::move(validator),
            [](T&) { return Result<Unit>::ok(unit); },
            config);
    }

    /**
     * @brief Create a single-threaded pool with full lifecycle management
     */
    template <Poolable T, typename Factory, typename Validator, typename Resetter>
        requires ResourceFactory<Factory, T> && ResourceValidator<Validator, T> &&
                 ResourceResetter<Resetter, T>
    [[nodiscard]] static auto create_with_lifecycle(Factory factory,
                                                    Validator validator,
                                                    Resetter resetter,
                                                    PoolConfig config = default_config)
        -> Result<std::shared_ptr<Pool<T>>> {

        auto validation = validate_config(config);
        if (validation.is_err()) {
            return Result<std::shared_ptr<Pool<T>>>::err(validation.error());
        }

        auto pool = std::shared_ptr<Pool<T>>(
            new Pool<T>(std::move(factory), std::move(validator), std::move(resetter), config));

        return Result<std::shared_ptr<Pool<T>>>::ok(std::move(pool));
    }

    // =========================================================================
    // Thread-safe pool creation
    // =========================================================================

    /**
     * @brief Create a thread-safe pool from a factory lambda
     */
    template <Poolable T, typename Factory>
        requires ResourceFactory<Factory, T>
    [[nodiscard]] static auto create_thread_safe(Factory factory,
                                                 PoolConfig config = default_config)
        -> Result<std::shared_ptr<ThreadSafePool<T>>> {

        return create_thread_safe_with_lifecycle<T>(
            std::move(factory),
            [](const T&) { return true; },
            [](T&) { return Result<Unit>::ok(unit); },
            config);
    }

    /**
     * @brief Create a thread-safe pool with custom validation
     */
    template <Poolable T, typename Factory, typename Validator>
        requires ResourceFactory<Factory, T> && ResourceValidator<Validator, T>
    [[nodiscard]] static auto create_thread_safe_validated(Factory factory,
                                                           Validator validator,
                                                           PoolConfig config = default_config)
        -> Result<std::shared_ptr<ThreadSafePool<T>>> {

        return create_thread_safe_with_lifecycle<T>(
            std::move(factory),
            std::move(validator),
            [](T&) { return Result<Unit>::ok(unit); },
            config);
    }

    /**
     * @brief Create a thread-safe pool with full lifecycle management
     */
    template <Poolable T, typename Factory, typename Validator, typename Resetter>
        requires ResourceFactory<Factory, T> && ResourceValidator<Validator, T> &&
                 ResourceResetter<Resetter, T>
    [[nodiscard]] static auto create_thread_safe_with_lifecycle(Factory factory,
                                                                Validator validator,
                                                                Resetter resetter,
                                                                PoolConfig config = default_config)
        -> Result<std::shared_ptr<ThreadSafePool<T>>> {

        auto validation = validate_config(config);
        if (validation.is_err()) {
            return Result<std::shared_ptr<ThreadSafePool<T>>>::err(validation.error());
        }

        auto pool = std::shared_ptr<ThreadSafePool<T>>(new ThreadSafePool<T>(
            std::move(factory), std::move(validator), std::move(resetter), config));

        return Result<std::shared_ptr<ThreadSafePool<T>>>::ok(std::move(pool));
    }

  private:
    [[nodiscard]] static auto validate_config(const PoolConfig& config) -> Result<Unit> {
        if (config.max_size == 0) {
            return Result<Unit>::err("max_size cannot be 0");
        }
        if (config.min_size > config.max_size) {
            return Result<Unit>::err("min_size cannot exceed max_size");
        }
        return Result<Unit>::ok(unit);
    }
};

// =============================================================================
// Convenience free functions
// =============================================================================

/**
 * @brief Create a single-threaded pool (convenience function)
 */
template <Poolable T, typename Factory>
    requires ResourceFactory<Factory, T>
[[nodiscard]] auto make_pool(Factory factory, PoolConfig config = default_config) {
    return PoolFactory::create<T>(std::move(factory), config);
}

/**
 * @brief Create a thread-safe pool (convenience function)
 */
template <Poolable T, typename Factory>
    requires ResourceFactory<Factory, T>
[[nodiscard]] auto make_thread_safe_pool(Factory factory, PoolConfig config = default_config) {
    return PoolFactory::create_thread_safe<T>(std::move(factory), config);
}

} // namespace poolfactory
