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

/// `T` unchanged. As a parameter type, it keeps that parameter out of template argument deduction.
/// @since 0.1
template <typename T>
using TypeIdentity = std::type_identity_t<T>;

/// `T` with any top-level const and volatile removed.
/// @since 0.1
template <typename T>
using RemoveCv = std::remove_cv_t<T>;

/// The type `T` points to, or `T` itself if it is not a pointer.
/// @since 0.1
template <typename T>
using RemovePointer = std::remove_pointer_t<T>;

/// The element type of the array type `T`, or `T` itself if it is not an array.
/// @since 0.1
template <typename T>
using RemoveExtent = std::remove_extent_t<T>;

/// Satisfied when `T` is a pointer type (not a pointer to member).
/// @since 0.1
template <typename T>
concept PointerType = std::is_pointer_v<T>;

/// Satisfied when `T` is the type of `nullptr`.
/// @since 0.1
template <typename T>
concept NullPointerType = std::is_null_pointer_v<T>;

/// Satisfied when `T` is an array type.
/// @since 0.1
template <typename T>
concept ArrayType = std::is_array_v<T>;

/// Satisfied when `T` is a signed integer type.
/// @since 0.1
template <typename T>
concept SignedIntegral = std::signed_integral<T>;

/// Satisfied when `T` is a class or struct type (not a union).
/// @since 0.1
template <typename T>
concept ClassType = std::is_class_v<T>;

/// Satisfied when `T` can be constructed without arguments.
/// @since 0.1
template <typename T>
concept DefaultConstructible = std::is_default_constructible_v<T>;

/// Satisfied when an object of type `T` can be destroyed where the concept is checked (the destructor exists and
/// is accessible).
/// @since 0.1
template <typename T>
concept Destructible = std::is_destructible_v<T>;

} // namespace Nexyl
