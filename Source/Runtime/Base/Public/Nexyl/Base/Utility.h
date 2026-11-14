#pragma once

#include <Nexyl/Base/Traits.h>

namespace Nexyl {

/// Casts `value` to an rvalue, so that it can be moved from.
///
/// @param value The object to move from. It stays valid but unspecified (rule OWN-6).
/// @return An rvalue reference to `value`.
/// @threadsafety safe
/// @since 0.1
template <typename T>
[[nodiscard]] constexpr RemoveReference<T>&& Move(T&& value) noexcept {
    return static_cast<RemoveReference<T>&&>(value);
}

/// Passes on a forwarding-reference parameter with its original value category.
///
/// @param value The parameter to forward.
/// @return `value` as an lvalue if it was passed as one, otherwise as an rvalue.
/// @threadsafety safe
/// @since 0.1
template <typename T>
[[nodiscard]] constexpr T&& Forward(RemoveReference<T>& value) noexcept {
    return static_cast<T&&>(value);
}

/// Overload of Forward for rvalue arguments.
///
/// @param value The parameter to forward.
/// @return `value` as an rvalue.
/// @threadsafety safe
/// @since 0.1
template <typename T>
[[nodiscard]] constexpr T&& Forward(RemoveReference<T>&& value) noexcept {
    static_assert(!LvalueReference<T>, "An rvalue cannot be forwarded as an lvalue.");
    return static_cast<T&&>(value);
}

/// Exchanges the values of two objects by moving.
///
/// @param first  One object.
/// @param second The other object.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
constexpr void Swap(T& first, T& second) noexcept {
    T temporary = Move(first);
    first = Move(second);
    second = Move(temporary);
}

/// Replaces the value of an object and returns its old value.
///
/// @param target   The object to update.
/// @param newValue The value to store in `target`.
/// @return The value `target` held before.
/// @threadsafety compatible
/// @since 0.1
template <typename T, typename U = T>
[[nodiscard]] constexpr T Exchange(T& target, U&& newValue) noexcept {
    T oldValue = Move(target);
    target = Forward<U>(newValue);
    return oldValue;
}

} // namespace Nexyl
