#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

/// A position in a StackAllocator, to rewind to with FreeToMarker. It also holds the statistics of that moment.
/// @threadsafety safe
/// @since 0.1
struct StackMarker {
    /// Bytes used at the marker.
    usize offset = 0;
    /// Bytes in use at the marker, as the statistics count them.
    usize bytesInUse = 0;
    /// Live blocks at the marker.
    usize liveAllocationCount = 0;
};

/// Hands out blocks from one fixed buffer like LinearAllocator, and releases them in last-in, first-out order
/// (docs/NEXYL_ARCHITECTURE.md, section 5.4).
///
/// Nested temporary scopes take a marker on entry and rewind to it on exit, which releases everything allocated
/// in between. Deallocate releases the memory of the most recent block at once; the memory of any other block
/// returns when the stack rewinds past it. This is the backing of scratch memory (section 5.5).
///
/// ```cpp
/// const StackMarker marker = scratch.GetMarker();
/// void* const temporary = Memory::Allocate(scratch, 512);
/// scratch.FreeToMarker(marker);
/// ```
/// @threadsafety compatible
/// @since 0.1
class StackAllocator final : public IAllocator {
public:
    /// Creates an allocator over `buffer`.
    /// @param buffer The memory to hand out. Borrowed: the caller keeps it alive longer than the allocator.
    /// @since 0.1
    NX_MEMORY_API explicit StackAllocator(Span<u8> buffer);
    /// Destroys the allocator. Blocks from it become invalid. @since 0.1
    NX_MEMORY_API ~StackAllocator();

    StackAllocator(const StackAllocator&) = delete;
    StackAllocator& operator=(const StackAllocator&) = delete;
    StackAllocator(StackAllocator&&) = delete;
    StackAllocator& operator=(StackAllocator&&) = delete;

    /// See IAllocator::TryAllocate. @error CapacityExceeded The buffer is full. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. The most recent block is resized in place if it fits. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Frees the memory of the most recent block; other blocks wait for a rewind.
    /// @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Exact: checks the buffer's range. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

    /// Returns the current position. @since 0.1
    [[nodiscard]] NX_MEMORY_API StackMarker GetMarker() const;

    /// Releases every block allocated after `marker` was taken.
    /// @param marker A marker from this allocator.
    /// @pre The stack has not been rewound to an earlier marker since `marker` was taken.
    /// @since 0.1
    NX_MEMORY_API void FreeToMarker(const StackMarker& marker);

    /// Returns the bytes used, including alignment padding. @since 0.1
    [[nodiscard]] usize GetUsedBytes() const {
        return m_used;
    }

    /// Returns the size of the buffer. @since 0.1
    [[nodiscard]] usize GetCapacity() const {
        return m_capacity;
    }

private:
    u8* m_base = nullptr;
    usize m_capacity = 0;
    usize m_used = 0;
    // Offset of the most recent block; only valid while m_hasLastBlock is set.
    usize m_lastBlockOffset = 0;
    bool m_hasLastBlock = false;
    AllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
