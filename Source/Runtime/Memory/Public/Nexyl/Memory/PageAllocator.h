#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

/// The root of every allocator hierarchy: whole pages straight from the operating system
/// (docs/NEXYL_ARCHITECTURE.md, section 5.4).
///
/// Each block is its own address-space reservation, committed at once and returned to the system when released,
/// so it cannot fragment anything. Sizes are rounded up to whole pages, so the allocator suits large blocks and the
/// backing memory of other allocators, not small objects. Alignments up to the allocation granularity
/// (Platform::SystemInfo::allocationGranularity, 64 KiB on Windows) are supported.
///
/// ```cpp
/// PageAllocator pages;
/// Heap heap(pages);
/// ```
/// @threadsafety safe
/// @since 0.1
class PageAllocator final : public IAllocator {
public:
    /// Creates the allocator. It holds no memory until the first allocation. @since 0.1
    NX_MEMORY_API PageAllocator();
    /// Destroys the allocator. @pre Every block has been released. @since 0.1
    NX_MEMORY_API ~PageAllocator();

    PageAllocator(const PageAllocator&) = delete;
    PageAllocator& operator=(const PageAllocator&) = delete;
    PageAllocator(PageAllocator&&) = delete;
    PageAllocator& operator=(PageAllocator&&) = delete;

    /// See IAllocator::TryAllocate. @pre `alignment` is at most the allocation granularity. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. Resizes in place while the page count stays the same. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Returns the pages to the operating system. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Rejects only blocks that are not aligned to the allocation granularity. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. `bytesReserved` counts whole pages. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

    /// Returns the size of a page in bytes. @since 0.1
    [[nodiscard]] usize GetPageSize() const {
        return m_pageSize;
    }

    /// Returns the alignment of every block in bytes, the largest alignment the allocator supports. @since 0.1
    [[nodiscard]] usize GetAllocationGranularity() const {
        return m_allocationGranularity;
    }

private:
    usize m_pageSize = 0;
    usize m_allocationGranularity = 0;
    ConcurrentAllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
