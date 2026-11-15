#include <Nexyl/Memory/PoolAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Platform/Sanitizer.h>

namespace Nexyl::Memory {

// Sits at the start of every chunk taken from the parent.
struct PoolAllocator::Chunk {
    Chunk* next = nullptr;
    // Size of the whole chunk, as requested from the parent.
    usize size = 0;
};

// The first bytes of a block on the free list.
struct PoolAllocator::FreeBlock {
    FreeBlock* next = nullptr;
};

PoolAllocator::PoolAllocator(IAllocator& parent, usize blockSize, usize blockAlignment, usize blocksPerChunk)
    : m_parent(&parent), m_blocksPerChunk(blocksPerChunk) {
    NX_ASSERT(blockSize != 0 && IsPowerOfTwo(blockAlignment) && blocksPerChunk != 0,
              "A pool needs a non-zero block size, a power-of-two alignment and at least one block per chunk.");
    m_blockAlignment = blockAlignment > alignof(FreeBlock) ? blockAlignment : alignof(FreeBlock);
    m_blockSize = AlignUp(blockSize > sizeof(FreeBlock) ? blockSize : sizeof(FreeBlock), m_blockAlignment);
    m_firstBlockOffset = AlignUp(sizeof(Chunk), m_blockAlignment);
}

PoolAllocator::~PoolAllocator() {
    NX_ASSERT(m_counters.Get().liveAllocationCount == 0, "PoolAllocator destroyed while blocks from it are alive.");
    while (m_chunks != nullptr) {
        Chunk* const next = m_chunks->next;
        const usize size = m_chunks->size;
        // The parent may hand this memory to anyone, so the sanitizer must see all of it as usable again.
        Platform::UnpoisonMemoryRegion(m_chunks, size);
        m_parent->Deallocate(m_chunks, size);
        m_chunks = next;
    }
}

Result<void*, Error> PoolAllocator::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0 && IsPowerOfTwo(alignment), "Allocation needs a non-zero size and a power-of-two alignment.");
    if (size > m_blockSize || alignment > m_blockAlignment) {
        return Error(MemoryError::CapacityExceeded);
    }
    if (m_freeList == nullptr) {
        NX_TRY(TryGrow());
        NX_ASSERT(m_freeList != nullptr, "A new chunk added no free blocks.");
    }
    FreeBlock* const block = m_freeList;
    m_freeList = block->next;
    // The caller may use exactly `size` bytes; the rest of the block stays poisoned to catch overruns.
    Platform::PoisonMemoryRegion(block, m_blockSize);
    Platform::UnpoisonMemoryRegion(block, size);
    m_counters.OnAllocate(size);
    return static_cast<void*>(block);
}

Result<void*, Error> PoolAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize /*alignment*/) {
    NX_ASSERT(Owns(block) && newSize != 0, "Reallocate needs a block from this pool and a non-zero size.");
    if (newSize > m_blockSize) {
        return Error(MemoryError::CapacityExceeded);
    }
    if (newSize < oldSize) {
        Platform::PoisonMemoryRegion(static_cast<u8*>(block) + newSize, oldSize - newSize);
    } else {
        Platform::UnpoisonMemoryRegion(static_cast<u8*>(block) + oldSize, newSize - oldSize);
    }
    m_counters.OnResize(oldSize, newSize);
    return block;
}

void PoolAllocator::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    NX_ASSERT(Owns(block), "Block does not come from this PoolAllocator.");
    NX_ASSERT(size <= m_blockSize, "Released size exceeds the block size.");
    // The link is written into the first bytes; everything after it is poisoned until the block is reused.
    Platform::UnpoisonMemoryRegion(block, m_blockSize);
    FreeBlock* const freed = ConstructAt<FreeBlock>(block);
    freed->next = m_freeList;
    m_freeList = freed;
    Platform::PoisonMemoryRegion(static_cast<u8*>(block) + sizeof(FreeBlock), m_blockSize - sizeof(FreeBlock));
    m_counters.OnDeallocate(size);
}

bool PoolAllocator::Owns(const void* block) const {
    const usize address = ToAddress(block);
    for (const Chunk* chunk = m_chunks; chunk != nullptr; chunk = chunk->next) {
        const usize first = ToAddress(chunk) + m_firstBlockOffset;
        const usize end = first + m_blockSize * m_blocksPerChunk;
        if (address >= first && address < end) {
            return (address - first) % m_blockSize == 0;
        }
    }
    return false;
}

AllocatorStatistics PoolAllocator::GetStatistics() const {
    return m_counters.Get();
}

Result<void, Error> PoolAllocator::TryGrow() {
    const usize chunkSize = m_firstBlockOffset + m_blockSize * m_blocksPerChunk;
    const usize chunkAlignment = m_blockAlignment > alignof(Chunk) ? m_blockAlignment : alignof(Chunk);
    NX_TRY_ASSIGN(void* const memory, m_parent->TryAllocate(chunkSize, chunkAlignment));

    Chunk* const chunk = ConstructAt<Chunk>(memory);
    chunk->next = m_chunks;
    chunk->size = chunkSize;
    m_chunks = chunk;
    m_counters.AddReserved(chunkSize);

    // Link the blocks from last to first, so that they are handed out in address order.
    u8* const first = static_cast<u8*>(memory) + m_firstBlockOffset;
    for (usize index = m_blocksPerChunk; index > 0; --index) {
        u8* const bytes = first + (index - 1) * m_blockSize;
        FreeBlock* const block = ConstructAt<FreeBlock>(bytes);
        block->next = m_freeList;
        m_freeList = block;
        Platform::PoisonMemoryRegion(bytes + sizeof(FreeBlock), m_blockSize - sizeof(FreeBlock));
    }
    return {};
}

} // namespace Nexyl::Memory
