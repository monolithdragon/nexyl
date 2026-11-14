#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

/// Hands out blocks by moving a pointer forward through a growing chain of memory blocks taken from a parent
/// allocator, and releases them all at once (docs/NEXYL_ARCHITECTURE.md, sections 5.4 and 5.5).
///
/// The backing of the permanent, module and world lifetime classes: memory is grouped by when it dies, and the
/// whole group is released in bulk. Deallocate does nothing, and the statistics count every block until the next
/// Reset. A request larger than the block size gets a block of its own. The most recent allocation can grow or
/// shrink in place.
///
/// ```cpp
/// ArenaAllocator level(heap);
/// LevelData* const data = Memory::New<LevelData>(level);
/// level.Release(); // the level is unloaded
/// ```
/// @threadsafety compatible
/// @since 0.1
class ArenaAllocator final : public IAllocator {
public:
    /// Size of the blocks taken from the parent when the caller does not choose one.
    /// @since 0.1
    static constexpr usize kDefaultBlockSize = usize{64} * 1024;

    /// Creates an empty arena. It takes no memory until the first allocation.
    /// @param parent    Supplies the blocks. It must outlive the arena.
    /// @param blockSize Size of each block taken from the parent, including a 16-byte header.
    /// @since 0.1
    NX_MEMORY_API explicit ArenaAllocator(IAllocator& parent, usize blockSize = kDefaultBlockSize);
    /// Returns every block to the parent. Blocks from the arena become invalid. @since 0.1
    NX_MEMORY_API ~ArenaAllocator();

    ArenaAllocator(const ArenaAllocator&) = delete;
    ArenaAllocator& operator=(const ArenaAllocator&) = delete;
    ArenaAllocator(ArenaAllocator&&) = delete;
    ArenaAllocator& operator=(ArenaAllocator&&) = delete;

    /// See IAllocator::TryAllocate. @error OutOfMemory The parent cannot provide a new block. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. The most recent allocation is resized in place if it fits. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Does nothing: memory returns with Reset or Release. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Exact; walks the chain of blocks. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. `bytesReserved` is the memory held from the parent. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

    /// Releases every allocation at once. Keeps the newest block for reuse and returns the others to the parent.
    /// @since 0.1
    NX_MEMORY_API void Reset();

    /// Releases every allocation and returns every block to the parent. @since 0.1
    NX_MEMORY_API void Release();

private:
    struct Block;

    void ReturnBlock(Block* block);

    IAllocator* m_parent = nullptr;
    usize m_blockSize = 0;
    // The newest block; each block links to the one before it.
    Block* m_current = nullptr;
    // Bytes used in m_current, counted from its start, header included.
    usize m_used = 0;
    // Offset of the most recent allocation in m_current; only valid while m_hasLastBlock is set.
    usize m_lastBlockOffset = 0;
    bool m_hasLastBlock = false;
    AllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
