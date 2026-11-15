#pragma once

// Bump-pointer arithmetic shared by LinearAllocator, StackAllocator and ArenaAllocator. Not part of the public API.

#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/Alignment.h>

namespace Nexyl::Memory::Detail {

/// Finds where the next block goes in a region that is used up to `used` bytes from `base`.
/// @return The block's offset from `base`, or kNone if it does not fit in `capacity` bytes.
inline Option<usize> FitBlock(const u8* base, usize capacity, usize used, usize size, usize alignment) {
    const usize start = AlignUp(ToAddress(base) + used, alignment) - ToAddress(base);
    if (start > capacity || size > capacity - start) {
        return kNone;
    }
    return start;
}

} // namespace Nexyl::Memory::Detail
