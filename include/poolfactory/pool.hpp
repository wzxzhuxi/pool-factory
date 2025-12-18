#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

#include "poolfactory/concepts.hpp"
#include "poolfactory/pool_config.hpp"
#include "poolfactory/pooled_resource.hpp"
#include "poolfactory/result.hpp"
#include "poolfactory/unit.hpp"

namespace poolfactory {

// Forward declaration
class PoolFactory;

/**
 * @brief Pool statistics (pure read-only snapshot)
 */
struct PoolStats {
    std::size_t available;
    std::size_t in_use;
    std::size_t total_created;
    std::size_t max_size;

    constexpr auto operator==(const PoolStats&) const -> bool = default;
};

/**
 * @brief Single-threaded resource pool
 *
 * Not thread-safe. Use ThreadSafePool for concurrent access.
 * The effectful boundary - mutations happen here, wrapped in Result.
 */
template <Poolable T> class Pool {
  public:
    using Factory = std::function<Result<T>()>;
    using Validator = std::function<bool(const T&)>;
    using Resetter = std::function<Result<Unit>(T&)>;

    Pool(const Pool&) = delete;
    auto operator=(const Pool&) -> Pool& = delete;
    Pool(Pool&&) = delete;
    auto operator=(Pool&&) -> Pool& = delete;

    virtual ~Pool() = default;

    /**
     * @brief Acquire a resource from the pool
     */
    [[nodiscard]] virtual auto acquire() -> Result<PooledResource<T>> {
        // Try to get from available pool
        if (!available_.empty()) {
            T resource = std::move(available_.front());
            available_.pop_front();

            // Validate if configured
            if (config_.validate_on_acquire && validator_ && !validator_(resource)) {
                // Resource invalid, try to create new one
                return create_and_wrap();
            }

            ++in_use_;
            return wrap_resource(std::move(resource));
        }

        // Need to create new resource
        if (in_use_ >= config_.max_size) {
            return Result<PooledResource<T>>::err("Pool exhausted: max_size reached");
        }

        return create_and_wrap();
    }

    /**
     * @brief Execute function with a pooled resource (bracket pattern)
     *
     * Preferred API - ensures resource is always released.
     */
    template <typename F>
    auto with_resource(F&& f)
        -> Result<std::conditional_t<std::is_void_v<std::invoke_result_t<F, T&>>,
                                     Unit,
                                     std::invoke_result_t<F, T&>>> {
        using RawR = std::invoke_result_t<F, T&>;
        using R = std::conditional_t<std::is_void_v<RawR>, Unit, RawR>;

        auto acquired = acquire();
        if (acquired.is_err()) {
            return Result<R>::err(std::move(acquired).error());
        }

        auto resource = std::move(acquired).value();
        if constexpr (std::is_void_v<RawR>) {
            f(resource.get());
            return Result<R>::ok(unit);
        } else {
            return Result<R>::ok(f(resource.get()));
        }
    }

    /**
     * @brief Get pool statistics (pure read)
     */
    [[nodiscard]] virtual auto stats() const -> PoolStats {
        return PoolStats{
            .available = available_.size(),
            .in_use = in_use_,
            .total_created = total_created_,
            .max_size = config_.max_size,
        };
    }

    /**
     * @brief Get current configuration (pure read)
     */
    [[nodiscard]] auto config() const -> const PoolConfig& { return config_; }

  protected:
    friend class PoolFactory;

    Pool(Factory factory, Validator validator, Resetter resetter, PoolConfig config)
        : factory_(std::move(factory)), validator_(std::move(validator)),
          resetter_(std::move(resetter)), config_(config) {
        // Pre-warm pool to min_size
        for (std::size_t i = 0; i < config_.min_size; ++i) {
            auto result = factory_();
            if (result.is_ok()) {
                available_.push_back(std::move(result).value());
                ++total_created_;
            }
        }
    }

    virtual void do_release(T resource) {
        --in_use_;

        // Reset resource if resetter provided
        if (resetter_) {
            auto reset_result = resetter_(resource);
            if (reset_result.is_err()) {
                // Resource cannot be reset, discard it
                return;
            }
        }

        // Validate on release if configured
        if (config_.validate_on_release && validator_ && !validator_(resource)) {
            // Resource invalid, discard
            return;
        }

        // Return to pool
        available_.push_back(std::move(resource));
    }

    auto wrap_resource(T resource) -> Result<PooledResource<T>> {
        auto releaser = [this](T r) { this->do_release(std::move(r)); };
        return Result<PooledResource<T>>::ok(
            PooledResource<T>(std::move(resource), std::move(releaser)));
    }

    auto create_and_wrap() -> Result<PooledResource<T>> {
        auto result = factory_();
        if (result.is_err()) {
            return Result<PooledResource<T>>::err(result.error());
        }

        ++total_created_;
        ++in_use_;
        return wrap_resource(std::move(result).value());
    }

    Factory factory_;
    Validator validator_;
    Resetter resetter_;
    PoolConfig config_;

    std::deque<T> available_;
    std::size_t in_use_{0};
    std::size_t total_created_{0};
};

/**
 * @brief Thread-safe resource pool
 *
 * Wraps Pool with mutex protection and condition variable for waiting.
 */
template <Poolable T> class ThreadSafePool : public Pool<T> {
  public:
    using typename Pool<T>::Factory;
    using typename Pool<T>::Validator;
    using typename Pool<T>::Resetter;

    /**
     * @brief Acquire a resource, blocking until available or timeout
     */
    [[nodiscard]] auto acquire() -> Result<PooledResource<T>> override {
        std::unique_lock lock(mutex_);

        auto deadline = std::chrono::steady_clock::now() + this->config_.acquire_timeout;

        // Wait for available resource or room to create new one
        while (this->available_.empty() && this->in_use_ >= this->config_.max_size) {
            if (cv_.wait_until(lock, deadline) == std::cv_status::timeout) {
                return Result<PooledResource<T>>::err("Pool acquire timeout");
            }
        }

        // Try to get from available pool
        if (!this->available_.empty()) {
            T resource = std::move(this->available_.front());
            this->available_.pop_front();

            // Validate if configured
            if (this->config_.validate_on_acquire && this->validator_ &&
                !this->validator_(resource)) {
                // Resource invalid, try to create new one
                return create_and_wrap_locked();
            }

            ++this->in_use_;
            return wrap_resource_locked(std::move(resource));
        }

        // Create new resource
        return create_and_wrap_locked();
    }

    /**
     * @brief Get pool statistics (thread-safe)
     */
    [[nodiscard]] auto stats() const -> PoolStats override {
        std::lock_guard lock(mutex_);
        return Pool<T>::stats();
    }

  protected:
    friend class PoolFactory;

    ThreadSafePool(Factory factory, Validator validator, Resetter resetter, PoolConfig config)
        : Pool<T>(std::move(factory), std::move(validator), std::move(resetter), config) {}

    void do_release(T resource) override {
        {
            std::lock_guard lock(mutex_);
            Pool<T>::do_release(std::move(resource));
        }
        cv_.notify_one();
    }

  private:
    auto wrap_resource_locked(T resource) -> Result<PooledResource<T>> {
        auto releaser = [this](T r) { this->do_release(std::move(r)); };
        return Result<PooledResource<T>>::ok(
            PooledResource<T>(std::move(resource), std::move(releaser)));
    }

    auto create_and_wrap_locked() -> Result<PooledResource<T>> {
        auto result = this->factory_();
        if (result.is_err()) {
            return Result<PooledResource<T>>::err(result.error());
        }

        ++this->total_created_;
        ++this->in_use_;
        return wrap_resource_locked(std::move(result).value());
    }

    mutable std::mutex mutex_;
    std::condition_variable cv_;
};

} // namespace poolfactory
