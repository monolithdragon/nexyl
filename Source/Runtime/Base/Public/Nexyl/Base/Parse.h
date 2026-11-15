#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>

// Exact conversion of decimal text to numbers, the counterpart of Format. The whole text must be the number:
// no white space, no leading '+'. Only integer arithmetic is used, so every platform gives the same bits.

namespace Nexyl {

/// Parses a decimal unsigned integer: one or more digits.
/// @param text The text. Borrowed for the duration of the call.
/// @return The value.
/// @error InvalidArgument The text is not a decimal integer.
/// @error OutOfRange      The value does not fit in 64 bits.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API Result<u64, Error> ParseU64(Span<const char> text);

/// Parses a decimal signed integer: an optional '-' and one or more digits.
/// @param text The text. Borrowed for the duration of the call.
/// @return The value.
/// @error InvalidArgument The text is not a decimal integer.
/// @error OutOfRange      The value does not fit in 64 bits.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API Result<i64, Error> ParseI64(Span<const char> text);

/// Parses a decimal floating-point number, correctly rounded to the nearest f64, ties to even.
///
/// The form is `-? digits (. digits)? ([eE] [+-]? digits)?`, or `inf`, `-inf`, `nan` as Format writes them.
/// Text that Format produced reads back to the same bits. A value too small for f64 becomes zero with its
/// sign; a value too large is an error.
/// @param text The text. Borrowed for the duration of the call.
/// @return The value.
/// @error InvalidArgument The text is not a number of that form.
/// @error OutOfRange      The value rounds to infinity.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API Result<f64, Error> ParseF64(Span<const char> text);

/// Parses a decimal floating-point number, correctly rounded to the nearest f32, ties to even. See ParseF64.
/// @param text The text. Borrowed for the duration of the call.
/// @return The value.
/// @error InvalidArgument The text is not a number of that form.
/// @error OutOfRange      The value rounds to infinity.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API Result<f32, Error> ParseF32(Span<const char> text);

} // namespace Nexyl
