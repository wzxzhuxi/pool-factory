#pragma once

#include <string>
#include <variant>

namespace poolfactory {

/**
 * @brief Ok tag - wraps successful value
 */
template <typename T> struct Ok {
    T value;

    explicit Ok(T v) : value(std::move(v)) {}
};

/**
 * @brief Err tag - wraps error value
 */
template <typename E> struct Err {
    E value;

    explicit Err(E v) : value(std::move(v)) {}
};

// Deduction guides
template <typename T> Ok(T) -> Ok<T>;

template <typename E> Err(E) -> Err<E>;

/**
 * @brief Result<T, E> - Functional error handling monad
 *
 * Uses tagged union (Ok<T>/Err<E>) to disambiguate success from failure,
 * even when T and E are the same type.
 *
 * Usage:
 *   auto result = parse_int("42");
 *   result.match(
 *       [](int v) { use(v); },
 *       [](const std::string& e) { handle_error(e); }
 *   );
 */
template <typename T, typename E = std::string> class Result {
  public:
    static auto ok(T value) -> Result { return Result{Ok<T>{std::move(value)}}; }
    static auto err(E error) -> Result { return Result{Err<E>{std::move(error)}}; }

    // Construct from tagged values
    Result(Ok<T> ok) : data_(std::move(ok)) {}
    Result(Err<E> err) : data_(std::move(err)) {}

    [[nodiscard]] auto is_ok() const -> bool { return std::holds_alternative<Ok<T>>(data_); }
    [[nodiscard]] auto is_err() const -> bool { return !is_ok(); }

    [[nodiscard]] auto value() const& -> const T& { return std::get<Ok<T>>(data_).value; }
    [[nodiscard]] auto value() && -> T { return std::move(std::get<Ok<T>>(data_).value); }

    [[nodiscard]] auto error() const& -> const E& { return std::get<Err<E>>(data_).value; }
    [[nodiscard]] auto error() && -> E { return std::move(std::get<Err<E>>(data_).value); }

    /**
     * @brief Transform the value if Ok, propagate error if Err
     */
    template <typename F>
    auto map(F&& f) const& -> Result<decltype(f(std::declval<const T&>())), E> {
        using U = decltype(f(std::declval<const T&>()));
        if (is_ok()) {
            return Result<U, E>::ok(f(value()));
        }
        return Result<U, E>::err(error());
    }

    template <typename F> auto map(F&& f) && -> Result<decltype(f(std::declval<T>())), E> {
        using U = decltype(f(std::declval<T>()));
        if (is_ok()) {
            return Result<U, E>::ok(f(std::move(*this).value()));
        }
        return Result<U, E>::err(std::move(*this).error());
    }

    /**
     * @brief Transform the error if Err, propagate value if Ok
     */
    template <typename F>
    auto map_err(F&& f) const& -> Result<T, decltype(f(std::declval<const E&>()))> {
        using U = decltype(f(std::declval<const E&>()));
        if (is_err()) {
            return Result<T, U>::err(f(error()));
        }
        return Result<T, U>::ok(value());
    }

    /**
     * @brief Chain operations that return Result (flatMap/bind)
     */
    template <typename F> auto and_then(F&& f) const& -> decltype(f(std::declval<const T&>())) {
        using R = decltype(f(std::declval<const T&>()));
        if (is_ok()) {
            return f(value());
        }
        return R::err(error());
    }

    template <typename F> auto and_then(F&& f) && -> decltype(f(std::declval<T>())) {
        using R = decltype(f(std::declval<T>()));
        if (is_ok()) {
            return f(std::move(*this).value());
        }
        return R::err(std::move(*this).error());
    }

    /**
     * @brief Recover from error with a function returning Result
     */
    template <typename F> auto or_else(F&& f) const& -> Result<T, E> {
        if (is_ok()) {
            return *this;
        }
        return f(error());
    }

    template <typename F> auto or_else(F&& f) && -> Result<T, E> {
        if (is_ok()) {
            return std::move(*this);
        }
        return f(std::move(*this).error());
    }

    /**
     * @brief Get value or default
     */
    [[nodiscard]] auto value_or(T default_val) const& -> T {
        if (is_ok()) {
            return value();
        }
        return default_val;
    }

    [[nodiscard]] auto value_or(T default_val) && -> T {
        if (is_ok()) {
            return std::move(*this).value();
        }
        return default_val;
    }

    /**
     * @brief Pattern matching style dispatch
     */
    template <typename OnOk, typename OnErr>
    auto match(OnOk&& on_ok, OnErr&& on_err) const& -> decltype(on_ok(std::declval<const T&>())) {
        if (is_ok()) {
            return on_ok(value());
        }
        return on_err(error());
    }

    template <typename OnOk, typename OnErr>
    auto match(OnOk&& on_ok, OnErr&& on_err) && -> decltype(on_ok(std::declval<T>())) {
        if (is_ok()) {
            return on_ok(std::move(*this).value());
        }
        return on_err(std::move(*this).error());
    }

  private:
    std::variant<Ok<T>, Err<E>> data_;
};

} // namespace poolfactory
