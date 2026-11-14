#pragma once

#include <Nexyl/Base/Std/Concepts.h>
#include <Nexyl/Base/Std/TypeTraits.h>

namespace Nexyl {

/// `T` with any reference removed.
/// @since 0.1
template <typename T>
using RemoveReference = std::remove_reference_t<T>;

/// `T` with any reference and any top-level const and volatile removed.
/// @since 0.1
template <typename T>
using RemoveCvRef = std::remove_cvref_t<T>;

/// The underlying integer type of the enumeration `T`.
/// @since 0.1
template <typename T>
using UnderlyingType = std::underlying_type_t<T>;

/// Satisfied when `T` and `U` are the same type.
/// @since 0.1
template <typename T, typename U>
concept SameAs = std::same_as<T, U>;

/// Satisfied when `TFrom` converts implicitly to `TTo`.
/// @since 0.1
template <typename TFrom, typename TTo>
concept ConvertibleTo = std::convertible_to<TFrom, TTo>;

/// Satisfied when `T` can be constructed from arguments of types `TArgs`.
/// @since 0.1
template <typename T, typename... TArgs>
concept ConstructibleFrom = std::constructible_from<T, TArgs...>;

/// Satisfied when `T` is an integer type, including `bool` and character types.
/// @since 0.1
template <typename T>
concept Integral = std::integral<T>;

/// Satisfied when `T` is a floating-point type.
/// @since 0.1
template <typename T>
concept FloatingPoint = std::floating_point<T>;

/// Satisfied when `T` is an enumeration type.
/// @since 0.1
template <typename T>
concept Enumeration = std::is_enum_v<T>;

/// Satisfied when `T` can be copied with a plain memory copy.
/// @since 0.1
template <typename T>
concept TriviallyCopyable = std::is_trivially_copyable_v<T>;

/// Satisfied when destroying a `T` does nothing.
/// @since 0.1
template <typename T>
concept TriviallyDestructible = std::is_trivially_destructible_v<T>;

/// Satisfied when `T` can be copy-constructed.
/// @since 0.1
template <typename T>
concept CopyConstructible = std::is_copy_constructible_v<T>;

/// Satisfied when `T` can be move-constructed.
/// @since 0.1
template <typename T>
concept MoveConstructible = std::is_move_constructible_v<T>;

/// Satisfied when `T` is an lvalue reference type.
/// @since 0.1
template <typename T>
concept LvalueReference = std::is_lvalue_reference_v<T>;

} // namespace Nexyl
