#pragma once

namespace poolfactory {

/**
 * @brief Unit type - represents "no meaningful value"
 *
 * Used instead of void in Result<T> context since void cannot be stored in variant.
 * Semantically equivalent to () in Haskell or Unit in Rust/Scala.
 */
struct Unit {
    constexpr auto operator==(const Unit&) const -> bool = default;
    constexpr auto operator<=>(const Unit&) const = default;
};

inline constexpr Unit unit{};

} // namespace poolfactory
