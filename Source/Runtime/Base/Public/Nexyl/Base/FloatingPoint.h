#pragma once

#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Std/Intrinsics.h>
#include <Nexyl/Base/Types.h>

// Floating-point primitives that the language cannot spell portably. IEEE-754 requires the square root to be
// correctly rounded, so the hardware instruction gives the same bits on every CPU (docs/NEXYL_ARCHITECTURE.md,
// section 7.4). It is reached through an intrinsic, not the C runtime, because GCC and MSVC turn a plain sqrt
// call into one that may call the C runtime to set errno (decision D-33).

namespace Nexyl {

/// Returns the square root, correctly rounded. Negative values and NaN give NaN.
/// @param value The operand.
/// @return The square root.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline f32 SquareRoot(f32 value) {
#if NX_ARCH_X64
    return _mm_cvtss_f32(_mm_sqrt_ss(_mm_set_ss(value)));
#elif NX_ARCH_ARM64
    return vget_lane_f32(vsqrt_f32(vdup_n_f32(value)), 0);
#endif
}

/// Returns the square root, correctly rounded. Negative values and NaN give NaN.
/// @param value The operand.
/// @return The square root.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline f64 SquareRoot(f64 value) {
#if NX_ARCH_X64
    return _mm_cvtsd_f64(_mm_sqrt_sd(_mm_setzero_pd(), _mm_set_sd(value)));
#elif NX_ARCH_ARM64
    return vget_lane_f64(vsqrt_f64(vdup_n_f64(value)), 0);
#endif
}

} // namespace Nexyl
