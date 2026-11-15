#pragma once

// Whitelisted CPU intrinsic headers (docs/CODING_STANDARDS.md, section 4.1; decision D-33).
// The rest of the engine uses the functions in <Nexyl/Base/FloatingPoint.h>, never this header directly.

#include <Nexyl/Base/Config.h>

#if NX_ARCH_X64
#include <emmintrin.h>
#elif NX_ARCH_ARM64
#include <arm_neon.h>
#endif
