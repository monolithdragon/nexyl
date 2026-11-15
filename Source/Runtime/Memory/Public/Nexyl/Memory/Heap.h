#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/FreeListAllocator.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/PageAllocator.h>
#include <Nexyl/Platform/Synchronization.h>

namespace Nexyl::Memory {

namespace Detail {

/// The header of a pool a Heap takes from its page allocator. Defined in the implementation. @since 0.1
struct HeapPool;
/// The header in front of a large Heap block. Defined in the implementation. @since 0.1
struct HeapLargeBlock;

} // namespace Detail

/// How a Heap grows.
/// @threadsafety safe
/// @since 0.1
struct HeapDesc {
    /// Size of each pool taken from the page allocator for small and medium blocks. A request that does not fit a
    /// pool of this size gets a larger pool.
    usize poolSize = usize{16} * 1024 * 1024;
    /// Blocks of at least this size get pages of their own instead of pool memory.
    usize largeBlockThreshold = usize{1} * 1024 * 1024;
};

/// The general-purpose allocator: any size, any order of release, any thread (docs/NEXYL_ARCHITECTURE.md,
/// sections 5.4 and 5.5).
///
/// Small and medium blocks come from TLSF pools (FreeListAllocator) that the heap takes from a PageAllocator as it
/// grows. Large blocks get pages of their own, which go back to the system when the block is released, so they
/// cannot pin a pool. One mutex protects the heap. Per-thread caches for small blocks are planned for when Jobs
/// exists and measurements show contention; they will not change this interface. Pools are returned to the page
/// allocator only when the heap is destroyed.
///
/// Persistent allocations name an owner (rule OWN-2): put a TrackingAllocator with the owner's tag in front of
/// the heap rather than allocating from it directly.
///
/// ```cpp
/// PageAllocator pages;
/// Heap heap(pages);
/// TrackingAllocator physicsMemory(heap, MemoryTag::Make(ModuleId::Physics));
/// ```
/// @threadsafety safe
/// @since 0.1
class Heap final : public IAllocator {
public:
    /// Creates an empty heap. It takes no memory until the first allocation.
    /// @param pages Supplies the pools and the large blocks. It must outlive the heap.
    /// @param desc  How the heap grows.
    /// @since 0.1
    NX_MEMORY_API explicit Heap(PageAllocator& pages, const HeapDesc& desc = HeapDesc{});
    /// Returns every pool to the page allocator. @pre Every block has been released. @since 0.1
    NX_MEMORY_API ~Heap();

    Heap(const Heap&) = delete;
    Heap& operator=(const Heap&) = delete;
    Heap(Heap&&) = delete;
    Heap& operator=(Heap&&) = delete;

    /// See IAllocator::TryAllocate. @error OutOfMemory The page allocator cannot provide memory. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. Medium blocks grow in place when a free neighbour allows it. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Exact. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. `bytesReserved` covers the pools and the large blocks. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

private:
    [[nodiscard]] bool IsLarge(usize size) const {
        return size >= m_desc.largeBlockThreshold;
    }

    Result<void*, Error> TryAllocateLarge(usize size, usize alignment);
    void DeallocateLarge(void* block, usize size);
    Result<void, Error> TryGrow(usize size, usize alignment);

    PageAllocator* m_pages = nullptr;
    HeapDesc m_desc;
    mutable Platform::Mutex m_mutex;
    // Everything below is protected by m_mutex.
    FreeListAllocator m_freeList;
    Detail::HeapPool* m_pools = nullptr;
    Detail::HeapLargeBlock* m_largeBlocks = nullptr;
    AllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
