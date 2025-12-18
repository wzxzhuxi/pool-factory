#pragma once

#include <concepts>
#include <functional>
#include <type_traits>

#include "poolfactory/result.hpp"
#include "poolfactory/unit.hpp"

namespace poolfactory {

/**
 * @brief A resource that can be pooled
 */
template <typename T>
concept Poolable = std::movable<T> && std::destructible<T>;

/**
 * @brief Factory function: () -> Result<T>
 */
template <typename F, typename T>
concept ResourceFactory = std::invocable<F> && requires(F f) {
    { f() } -> std::same_as<Result<T>>;
};

/**
 * @brief Validator function: (const T&) -> bool
 */
template <typename F, typename T>
concept ResourceValidator = std::invocable<F, const T&> && requires(F f, const T& r) {
    { f(r) } -> std::convertible_to<bool>;
};

/**
 * @brief Resetter function: (T&) -> Result<Unit>
 */
template <typename F, typename T>
concept ResourceResetter = std::invocable<F, T&> && requires(F f, T& r) {
    { f(r) } -> std::same_as<Result<Unit>>;
};

/**
 * @brief Destroyer function: (T&) -> void
 */
template <typename F, typename T>
concept ResourceDestroyer = std::invocable<F, T&> && requires(F f, T& r) {
    { f(r) } -> std::same_as<void>;
};

} // namespace poolfactory
