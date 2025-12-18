#pragma once

#include <functional>
#include <memory>
#include <optional>

#include "poolfactory/concepts.hpp"

namespace poolfactory {

// Forward declaration
template <Poolable T> class Pool;

template <Poolable T> class ThreadSafePool;

/**
 * @brief RAII wrapper for a pooled resource
 *
 * Automatically returns the resource to the pool when destroyed.
 * Non-copyable, movable. Provides functional interface via use().
 */
template <Poolable T> class PooledResource {
  public:
    using Releaser = std::function<void(T)>;

    PooledResource(const PooledResource&) = delete;
    auto operator=(const PooledResource&) -> PooledResource& = delete;

    PooledResource(PooledResource&& other) noexcept
        : resource_(std::move(other.resource_)), releaser_(std::move(other.releaser_)) {
        other.releaser_ = nullptr;
    }

    auto operator=(PooledResource&& other) noexcept -> PooledResource& {
        if (this != &other) {
            release();
            resource_ = std::move(other.resource_);
            releaser_ = std::move(other.releaser_);
            other.releaser_ = nullptr;
        }
        return *this;
    }

    ~PooledResource() { release(); }

    // Access the underlying resource
    [[nodiscard]] auto get() const -> const T& { return *resource_; }
    [[nodiscard]] auto get() -> T& { return *resource_; }

    auto operator->() -> T* { return &(*resource_); }
    auto operator->() const -> const T* { return &(*resource_); }
    auto operator*() -> T& { return *resource_; }
    auto operator*() const -> const T& { return *resource_; }

    [[nodiscard]] auto has_value() const -> bool { return resource_.has_value(); }
    explicit operator bool() const { return has_value(); }

    /**
     * @brief Apply a function to the resource (functor-style)
     */
    template <typename F> auto use(F&& f) const -> decltype(f(std::declval<const T&>())) {
        return f(*resource_);
    }

    template <typename F> auto use(F&& f) -> decltype(f(std::declval<T&>())) {
        return f(*resource_);
    }

  private:
    template <Poolable U> friend class Pool;

    template <Poolable U> friend class ThreadSafePool;

    PooledResource(T resource, Releaser releaser)
        : resource_(std::move(resource)), releaser_(std::move(releaser)) {}

    void release() {
        if (resource_ && releaser_) {
            releaser_(std::move(*resource_));
            resource_.reset();
            releaser_ = nullptr;
        }
    }

    std::optional<T> resource_;
    Releaser releaser_;
};

} // namespace poolfactory
