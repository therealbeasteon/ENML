#pragma once

#include <concepts>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

#include <os/core/error.hpp>
#include <os/core/panic.hpp>

namespace os::core {

template <typename T>
class [[nodiscard]] Result final {
public:
    Result(const T& value) requires std::copy_constructible<T>
        : has_value_(true) {
        std::construct_at(std::addressof(storage_.value), value);
    }

    Result(T&& value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(true) {
        std::construct_at(std::addressof(storage_.value), std::move(value));
    }

    Result(Error error) noexcept
        : has_value_(false) {
        std::construct_at(std::addressof(storage_.error), error);
    }

    Result(const Result& other) requires std::copy_constructible<T>
        : has_value_(other.has_value_) {
        if (has_value_) {
            std::construct_at(std::addressof(storage_.value), other.storage_.value);
        } else {
            std::construct_at(std::addressof(storage_.error), other.storage_.error);
        }
    }

    Result(Result&& other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : has_value_(other.has_value_) {
        if (has_value_) {
            std::construct_at(std::addressof(storage_.value), std::move(other.storage_.value));
        } else {
            std::construct_at(std::addressof(storage_.error), other.storage_.error);
        }
    }

    Result& operator=(const Result& other) requires std::copy_constructible<T> && std::is_copy_assignable_v<T> {
        if (this == &other) {
            return *this;
        }
        destroy_active();
        has_value_ = other.has_value_;
        if (has_value_) {
            std::construct_at(std::addressof(storage_.value), other.storage_.value);
        } else {
            std::construct_at(std::addressof(storage_.error), other.storage_.error);
        }
        return *this;
    }

    Result& operator=(Result&& other) noexcept(
        std::is_nothrow_move_constructible_v<T> && std::is_nothrow_move_assignable_v<T>)
        requires std::is_move_assignable_v<T> {
        if (this == &other) {
            return *this;
        }
        destroy_active();
        has_value_ = other.has_value_;
        if (has_value_) {
            std::construct_at(std::addressof(storage_.value), std::move(other.storage_.value));
        } else {
            std::construct_at(std::addressof(storage_.error), other.storage_.error);
        }
        return *this;
    }

    ~Result() { destroy_active(); }

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value_; }
    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }

    // Reading the wrong alternative is a broken invariant, not a recoverable
    // error, and the guard must survive NDEBUG. See os/core/panic.hpp.
    [[nodiscard]] T& value() & noexcept {
        if (!has_value_) {
            invariant_violated();
        }
        return storage_.value;
    }

    [[nodiscard]] const T& value() const & noexcept {
        if (!has_value_) {
            invariant_violated();
        }
        return storage_.value;
    }

    [[nodiscard]] T&& value() && noexcept {
        if (!has_value_) {
            invariant_violated();
        }
        return std::move(storage_.value);
    }

    [[nodiscard]] Error error() const noexcept {
        if (has_value_) {
            invariant_violated();
        }
        return storage_.error;
    }

private:
    union Storage {
        T value;
        Error error;

        constexpr Storage() noexcept {}
        ~Storage() {}
    } storage_;

    bool has_value_ {false};

    void destroy_active() noexcept {
        if (has_value_) {
            std::destroy_at(std::addressof(storage_.value));
        } else {
            std::destroy_at(std::addressof(storage_.error));
        }
    }
};

template <>
class [[nodiscard]] Result<void> final {
public:
    constexpr Result() noexcept = default;
    constexpr Result(Error error) noexcept : error_(error), has_value_(false) {}

    [[nodiscard]] explicit constexpr operator bool() const noexcept { return has_value_; }
    [[nodiscard]] constexpr bool has_value() const noexcept { return has_value_; }

    // These stay constexpr: the trap branch is unreachable for a correctly used
    // Result, so constant evaluation still succeeds. A misuse detected at
    // compile time becomes a compile error, which is the outcome we want.
    constexpr void value() const noexcept {
        if (!has_value_) {
            invariant_violated();
        }
    }

    [[nodiscard]] constexpr Error error() const noexcept {
        if (has_value_) {
            invariant_violated();
        }
        return error_;
    }

private:
    Error error_ {core_error(errors::core::invalid_argument)};
    bool has_value_ {true};
};

template <typename T>
constexpr void discard_result(Result<T>&&) noexcept {}

} // namespace os::core
