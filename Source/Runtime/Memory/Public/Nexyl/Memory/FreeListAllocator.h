#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

namespace Detail {

/// The header of a FreeListAllocator block. Defined in the implementation. @since 0.1
struct FreeListBlock;
/// The header of a FreeListAllocator pool. Defined in the implementation. @since 0.1
struct FreeListPool;

} // namespace Detail

/// Variable-size blocks with individual release inside memory pools the caller provides, using two-level
/// segregated fit (TLSF) (docs/NEXYL_ARCHITECTURE.md, section 5.4; decision D-16).
///
/// Allocation and release take constant time, whatever the number of blocks, and free neighbours are merged at
/// once, which bounds fragmentation. Each block carries a 16-byte header; block sizes are multiples of 16. Uses
/// are variable-size data inside a fixed budget, and the medium blocks of Heap.
///
/// The allocator takes memory only through AddPool; it never grows by itself and fails with CapacityExceeded when
/// the pools are full. The caller owns the pools and keeps them alive longer than the allocator.
///
/// ```cpp
/// FreeListAllocator budget(Span<u8>(poolMemory, poolSize));
/// void* const block = Memory::Allocate(budget, 300);
/// budget.Deallocate(block, 300);
/// ```
/// @threadsafety compatible: callers synchronise externally, as Heap does.
/// @since 0.1
class FreeListAllocator final : public IAllocator {
public:
    /// The smallest pool AddPool accepts, in bytes.
    /// @since 0.1
    static constexpr usize kMinimumPoolSize = 128;

    /// Creates an allocator without memory. Add pools before allocating. @since 0.1
    NX_MEMORY_API FreeListAllocator();

    /// Creates an allocator over one pool.
    /// @param pool Memory to manage, as for AddPool.
    /// @since 0.1
    NX_MEMORY_API explicit FreeListAllocator(Span<u8> pool);

    /// Destroys the allocator. It does not touch the pools, which belong to the caller.
    /// @pre Every block has been released.
    /// @since 0.1
    NX_MEMORY_API ~FreeListAllocator();

    FreeListAllocator(const FreeListAllocator&) = delete;
    FreeListAllocator& operator=(const FreeListAllocator&) = delete;
    FreeListAllocator(FreeListAllocator&&) = delete;
    FreeListAllocator& operator=(FreeListAllocator&&) = delete;

    /// Adds memory to manage.
    /// @param pool The memory. Borrowed: the caller keeps it alive and untouched longer than the allocator.
    ///             At least kMinimumPoolSize bytes; at most kMaxAllocationSize.
    /// @threadsafety compatible
    /// @since 0.1
    NX_MEMORY_API void AddPool(Span<u8> pool);

    /// See IAllocator::TryAllocate. @error CapacityExceeded No free block is large enough. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. Grows in place into a free neighbour when it can. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Merges the block with free neighbours. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Checks the pools' ranges. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. `bytesReserved` is the total size of the pools. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

private:
    // Two-level index of the free blocks: the first level splits sizes by powers of two, the second splits each
    // of those ranges into 32 equal parts. Must match the size classes in the implementation.
    static constexpr u32 kFirstLevelCount = 40;
    static constexpr u32 kSecondLevelCount = 32;

    void InsertFree(Detail::FreeListBlock* block);
    void RemoveFree(Detail::FreeListBlock* block);
    [[nodiscard]] Detail::FreeListBlock* FindFree(usize payloadSize) const;
    void SplitTail(Detail::FreeListBlock* block, usize payloadSize);
    void Release(Detail::FreeListBlock* block);

    Detail::FreeListBlock* m_freeLists[kFirstLevelCount][kSecondLevelCount] = {};
    u32 m_secondLevelBitmaps[kFirstLevelCount] = {};
    u64 m_firstLevelBitmap = 0;
    Detail::FreeListPool* m_pools = nullptr;
    AllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
