#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

/// Hands out blocks of one fixed size from chunks taken from a parent allocator, and keeps released blocks in an
/// intrusive free list (docs/NEXYL_ARCHITECTURE.md, section 5.4).
///
/// Allocation and release take constant time, and uniform blocks cannot fragment. Typical uses are nodes, handles
/// and ECS chunks. A request may be smaller than the block size; a larger one fails. Chunks are returned to the
/// parent only when the pool is destroyed.
///
/// ```cpp
/// PoolAllocator nodes(heap, sizeof(Node), alignof(Node));
/// Node* const node = Memory::New<Node>(nodes);
/// Memory::Delete(nodes, node);
/// ```
/// @threadsafety compatible
/// @since 0.1
class PoolAllocator final : public IAllocator {
public:
    /// Blocks per chunk when the caller does not choose a number.
    /// @since 0.1
    static constexpr usize kDefaultBlocksPerChunk = 64;

    /// Creates an empty pool. It takes no memory until the first allocation.
    /// @param parent         Supplies the chunks. It must outlive the pool.
    /// @param blockSize      Size of every block. Rounded up to hold at least a pointer and to the alignment.
    /// @param blockAlignment Alignment of every block, a power of two. At least that of a pointer is used.
    /// @param blocksPerChunk Blocks taken from the parent at a time. Not zero.
    /// @since 0.1
    NX_MEMORY_API PoolAllocator(IAllocator& parent, usize blockSize, usize blockAlignment = kDefaultAlignment,
                                usize blocksPerChunk = kDefaultBlocksPerChunk);
    /// Returns every chunk to the parent. Blocks from the pool become invalid. @since 0.1
    NX_MEMORY_API ~PoolAllocator();

    PoolAllocator(const PoolAllocator&) = delete;
    PoolAllocator& operator=(const PoolAllocator&) = delete;
    PoolAllocator(PoolAllocator&&) = delete;
    PoolAllocator& operator=(PoolAllocator&&) = delete;

    /// See IAllocator::TryAllocate.
    /// @error CapacityExceeded `size` exceeds the block size or `alignment` the block alignment.
    /// @error OutOfMemory The parent cannot provide a new chunk.
    /// @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. Succeeds in place up to the block size, fails above it. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Puts the block on the free list. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Exact; walks the chunks. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

    /// Returns the size of every block, after rounding. @since 0.1
    [[nodiscard]] usize GetBlockSize() const {
        return m_blockSize;
    }

private:
    struct Chunk;
    struct FreeBlock;

    Result<void, Error> TryGrow();

    IAllocator* m_parent = nullptr;
    usize m_blockSize = 0;
    usize m_blockAlignment = 0;
    usize m_blocksPerChunk = 0;
    // Offset of the first block from the start of a chunk.
    usize m_firstBlockOffset = 0;
    Chunk* m_chunks = nullptr;
    FreeBlock* m_freeList = nullptr;
    AllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
