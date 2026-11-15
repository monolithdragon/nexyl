#pragma once

#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Types.h>

// AddressSanitizer annotations for allocators that manage memory themselves.
//
// AddressSanitizer sees only the blocks that an allocator takes from its parent, not the smaller blocks it hands
// out from them. An allocator marks the bytes nobody may touch, such as free blocks, as poisoned, so that the
// sanitizer reports a use after free or an overrun inside its memory (rule TEST-6). Without AddressSanitizer
// both functions are empty and inline.

namespace Nexyl::Platform {

#if NX_ASAN_ENABLED

/// Marks memory as unusable: AddressSanitizer reports any later access to it.
/// @param address First byte. Best aligned to 8 bytes, the sanitizer's granularity.
/// @param size    Number of bytes.
/// @threadsafety safe
/// @since 0.1
NX_PLATFORM_API void PoisonMemoryRegion(const void* address, usize size);

/// Marks memory as usable again after PoisonMemoryRegion.
/// @param address First byte.
/// @param size    Number of bytes.
/// @threadsafety safe
/// @since 0.1
NX_PLATFORM_API void UnpoisonMemoryRegion(const void* address, usize size);

#else

/// Marks memory as unusable for AddressSanitizer. Does nothing in this build, which has no sanitizer.
/// @param address First byte.
/// @param size    Number of bytes.
/// @threadsafety safe
/// @since 0.1
inline void PoisonMemoryRegion(const void* /*address*/, usize /*size*/) {}

/// Marks memory as usable again for AddressSanitizer. Does nothing in this build, which has no sanitizer.
/// @param address First byte.
/// @param size    Number of bytes.
/// @threadsafety safe
/// @since 0.1
inline void UnpoisonMemoryRegion(const void* /*address*/, usize /*size*/) {}

#endif

} // namespace Nexyl::Platform
