#pragma once

#include <Nexyl/Base/Types.h>

// Byte-level memory operations (rule STL-4). Use them instead of the C runtime's functions, and Copy instead of
// reinterpret_cast for type punning (rule CFG-2). Every function accepts null pointers when the size is zero.

namespace Nexyl::Memory {

/// Copies bytes between two ranges that do not overlap.
/// @param destination First byte to write.
/// @param source      First byte to read.
/// @param size        Number of bytes.
/// @pre The ranges do not overlap; use Move otherwise.
/// @threadsafety safe
/// @since 0.1
NX_MEMORY_API void Copy(void* destination, const void* source, usize size);

/// Copies bytes between two ranges that may overlap.
/// @param destination First byte to write.
/// @param source      First byte to read.
/// @param size        Number of bytes.
/// @threadsafety safe
/// @since 0.1
NX_MEMORY_API void Move(void* destination, const void* source, usize size);

/// Sets every byte of a range to one value.
/// @param destination First byte to write.
/// @param value       The byte to store.
/// @param size        Number of bytes.
/// @threadsafety safe
/// @since 0.1
NX_MEMORY_API void Set(void* destination, u8 value, usize size);

/// Compares two ranges byte by byte, as unsigned bytes.
/// @param first  First range.
/// @param second Second range.
/// @param size   Number of bytes in each.
/// @return -1 if the first differing byte is smaller in `first`, 1 if it is larger, and 0 if the ranges are equal.
///         The result is the same on every platform.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_MEMORY_API i32 Compare(const void* first, const void* second, usize size);

} // namespace Nexyl::Memory
