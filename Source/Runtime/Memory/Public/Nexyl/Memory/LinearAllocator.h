#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

/// Hands out blocks from one fixed buffer by moving a pointer forward, and releases them all at once with Reset
/// (docs/NEXYL_ARCHITECTURE.md, section 5.4).
///
/// The cheapest allocator: no per-block overhead and no free list. Deallocate does nothing, and the statistics
/// count every block until the next Reset. The most recent block can grow or shrink in place. Typical uses are
/// frame data and parse buffers.
///
/// ```cpp
/// u8 storage[4096];
/// LinearAllocator frame(storage);
/// void* const vertices = Memory::Allocate(frame, 1024);
/// frame.Reset();
/// ```
/// @threadsafety compatible
/// @since 0.1
class LinearAllocator final : public IAllocator {
public:
    /// Creates an allocator over `buffer`.
    /// @param buffer The memory to hand out. Borrowed: the caller keeps it alive longer than the allocator.
    /// @since 0.1
    NX_MEMORY_API explicit LinearAllocator(Span<u8> buffer);
    /// Destroys the allocator. Blocks from it become invalid. @since 0.1
    NX_MEMORY_API ~LinearAllocator();

    LinearAllocator(const LinearAllocator&) = delete;
    LinearAllocator& operator=(const LinearAllocator&) = delete;
    LinearAllocator(LinearAllocator&&) = delete;
    LinearAllocator& operator=(LinearAllocator&&) = delete;

    /// See IAllocator::TryAllocate. @error CapacityExceeded The buffer is full. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. The most recent block is resized in place if it fits. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Does nothing: memory returns with Reset. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Exact: checks the buffer's range. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

    /// Releases every block at once. Blocks from before the reset become invalid. @since 0.1
    NX_MEMORY_API void Reset();

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
    // Offset of the most recent block, which can be resized in place; only valid while m_hasLastBlock is set.
    usize m_lastBlockOffset = 0;
    bool m_hasLastBlock = false;
    AllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
