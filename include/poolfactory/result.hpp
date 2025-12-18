#pragma once

#include <string>
#include <variant>

namespace poolfactory {

/**
 * @brief Result<T> - Functional error handling monad
 *
 * Represents either a successful value (Ok) or an error message (Err).
 * Enables explicit error handling without exceptions.
 *
 * Usage:
 *   auto result = parse_int("42");
 *   if (result.is_ok()) {
 *       use(result.value());
 *   } else {
 *       handle_error(result.error());
 *   }
 */
template <typename T> class Result {
  public:
    static auto ok(T value) -> Result { return Result{std::move(value)}; }
    static auto err(std::string error) -> Result { return Result{std::move(error)}; }

    [[nodiscard]] auto is_ok() const -> bool { return std::holds_alternative<T>(data_); }
    [[nodiscard]] auto is_err() const -> bool { return !is_ok(); }

    [[nodiscard]] auto value() const -> const T& { return std::get<T>(data_); }
    [[nodiscard]] auto error() const -> const std::string& { return std::get<std::string>(data_); }

    /**
     * @brief Transform the value if Ok, propagate error if Err
     */
    template <typename F> auto map(F&& f) const -> Result<decltype(f(std::declval<T>()))> {
        using U = decltype(f(std::declval<T>()));
        if (is_ok()) {
            return Result<U>::ok(f(value()));
        }
        return Result<U>::err(error());
    }

    /**
     * @brief Chain operations that return Result
     */
    template <typename F> auto and_then(F&& f) const -> decltype(f(std::declval<T>())) {
        if (is_ok()) {
            return f(value());
        }
        return decltype(f(std::declval<T>()))::err(error());
    }

  private:
    explicit Result(T val) : data_(std::move(val)) {}
    explicit Result(std::string err) : data_(std::move(err)) {}

    std::variant<T, std::string> data_;
};

} // namespace poolfactory
