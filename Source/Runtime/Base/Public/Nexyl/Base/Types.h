#pragma once

#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Std/Bit.h>
#include <Nexyl/Base/Std/CStdDef.h>
#include <Nexyl/Base/Std/CStdInt.h>

namespace Nexyl {

/// Unsigned 8-bit integer.
/// @since 0.1
using u8 = std::uint8_t;
/// Unsigned 16-bit integer.
/// @since 0.1
using u16 = std::uint16_t;
/// Unsigned 32-bit integer.
/// @since 0.1
using u32 = std::uint32_t;
/// Unsigned 64-bit integer.
/// @since 0.1
using u64 = std::uint64_t;

/// Signed 8-bit integer.
/// @since 0.1
using i8 = std::int8_t;
/// Signed 16-bit integer.
/// @since 0.1
using i16 = std::int16_t;
/// Signed 32-bit integer.
/// @since 0.1
using i32 = std::int32_t;
/// Signed 64-bit integer.
/// @since 0.1
using i64 = std::int64_t;

/// IEEE-754 binary32 floating-point number.
/// @since 0.1
using f32 = float;
/// IEEE-754 binary64 floating-point number.
/// @since 0.1
using f64 = double;

/// Unsigned integer wide enough for any object size or array index.
/// @since 0.1
using usize = std::size_t;
/// Signed integer wide enough for any pointer difference.
/// @since 0.1
using isize = std::ptrdiff_t;

static_assert(sizeof(f32) == 4, "f32 must be IEEE-754 binary32");
static_assert(sizeof(f64) == 8, "f64 must be IEEE-754 binary64");
static_assert(sizeof(usize) == 8 && sizeof(void*) == 8, "Nexyl supports 64-bit targets only");
static_assert(std::endian::native == std::endian::little, "Nexyl supports little-endian targets only");

} // namespace Nexyl
