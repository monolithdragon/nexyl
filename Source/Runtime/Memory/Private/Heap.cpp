#include <Nexyl/Memory/Heap.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Memory/PageAllocator.h>
#include <Nexyl/Platform/Sanitizer.h>

namespace Nexyl::Memory {

namespace Detail {

// Starts every pool; links the pools so that the destructor can return them.
struct HeapPool {
    HeapPool* next = nullptr;
    // Size requested from the page allocator, header included.
    usize size = 0;
};

// Sits directly in front of every large block; links the large blocks for Owns and the destructor.
struct HeapLargeBlock {
    HeapLargeBlock* previous = nullptr;
    HeapLargeBlock* next = nullptr;
    // The pages holding the header and the block, as the page allocator returned them.
    void* pages = nullptr;
    usize pageBytes = 0;
};

} // namespace Detail

namespace {

constexpr usize kMinimumAlignment = 16;
constexpr usize kPoolHeaderSize = sizeof(Detail::HeapPool);
constexpr usize kLargeHeaderSize = sizeof(Detail::HeapLargeBlock);

static_assert(kPoolHeaderSize % kMinimumAlignment == 0 && kLargeHeaderSize % kMinimumAlignment == 0);

Detail::HeapLargeBlock* LargeHeaderOf(void* block) {
    return reinterpret_cast<Detail::HeapLargeBlock*>(static_cast<u8*>(block) - kLargeHeaderSize);
}

} // namespace

Heap::Heap(PageAllocator& pages, const HeapDesc& desc) : m_pages(&pages), m_desc(desc) {
    NX_ASSERT(desc.poolSize >= kPoolHeaderSize + FreeListAllocator::kMinimumPoolSize && desc.largeBlockThreshold != 0,
              "A heap needs room for at least one block per pool and a non-zero large-block threshold.");
}

Heap::~Heap() {
    NX_ASSERT(m_counters.Get().liveAllocationCount == 0, "Heap destroyed while blocks from it are alive.");
    while (m_largeBlocks != nullptr) {
        Detail::HeapLargeBlock* const next = m_largeBlocks->next;
        m_pages->Deallocate(m_largeBlocks->pages, m_largeBlocks->pageBytes);
        m_largeBlocks = next;
    }
    while (m_pools != nullptr) {
        Detail::HeapPool* const next = m_pools->next;
        const usize size = m_pools->size;
        // The pages go back to the system and may be mapped again for anyone.
        Platform::UnpoisonMemoryRegion(m_pools, size);
        m_pages->Deallocate(m_pools, size);
        m_pools = next;
    }
}

Result<void*, Error> Heap::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0 && IsPowerOfTwo(alignment), "Allocation needs a non-zero size and a power-of-two alignment.");
    if (size > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }
    if (IsLarge(size)) {
        return TryAllocateLarge(size, alignment);
    }

    const Platform::MutexLock lock(m_mutex);
    Result<void*, Error> block = m_freeList.TryAllocate(size, alignment);
    if (block.IsError()) {
        NX_TRY(TryGrow(size, alignment));
        block = m_freeList.TryAllocate(size, alignment);
        if (block.IsError()) {
            return block.GetError();
        }
    }
    m_counters.OnAllocate(size);
    return block;
}

Result<void*, Error> Heap::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    NX_ASSERT(block != nullptr && newSize != 0, "Reallocate needs a block and a non-zero size.");
    if (newSize > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }
    if (!IsLarge(oldSize) && !IsLarge(newSize)) {
        const Platform::MutexLock lock(m_mutex);
        Result<void*, Error> resized = m_freeList.TryReallocate(block, oldSize, newSize, alignment);
        if (resized.IsError()) {
            NX_TRY(TryGrow(newSize, alignment));
            resized = m_freeList.TryReallocate(block, oldSize, newSize, alignment);
            if (resized.IsError()) {
                return resized.GetError();
            }
        }
        m_counters.OnResize(oldSize, newSize);
        return resized;
    }

    // Large blocks, and blocks that cross the threshold, move.
    NX_TRY_ASSIGN(void* const moved, TryAllocate(newSize, alignment));
    Copy(moved, block, oldSize < newSize ? oldSize : newSize);
    Deallocate(block, oldSize);
    return moved;
}

void Heap::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    if (IsLarge(size)) {
        DeallocateLarge(block, size);
        return;
    }
    const Platform::MutexLock lock(m_mutex);
    m_freeList.Deallocate(block, size);
    m_counters.OnDeallocate(size);
}

bool Heap::Owns(const void* block) const {
    const Platform::MutexLock lock(m_mutex);
    if (m_freeList.Owns(block)) {
        return true;
    }
    for (const Detail::HeapLargeBlock* large = m_largeBlocks; large != nullptr; large = large->next) {
        if (ToAddress(large) + kLargeHeaderSize == ToAddress(block)) {
            return true;
        }
    }
    return false;
}

AllocatorStatistics Heap::GetStatistics() const {
    const Platform::MutexLock lock(m_mutex);
    return m_counters.Get();
}

Result<void*, Error> Heap::TryAllocateLarge(usize size, usize alignment) {
    NX_ASSERT(alignment <= m_pages->GetAllocationGranularity(),
              "Large heap blocks support alignments up to the allocation granularity.");
    const usize blockAlignment = alignment > kMinimumAlignment ? alignment : kMinimumAlignment;
    // The header sits directly in front of the block; the space before the block keeps the block aligned.
    const usize headerSpace = AlignUp(kLargeHeaderSize, blockAlignment);
    const usize pageBytes = headerSpace + size;
    NX_TRY_ASSIGN(void* const pages, m_pages->TryAllocate(pageBytes, blockAlignment));

    u8* const block = static_cast<u8*>(pages) + headerSpace;
    Detail::HeapLargeBlock* const header = ConstructAt<Detail::HeapLargeBlock>(block - kLargeHeaderSize);
    header->pages = pages;
    header->pageBytes = pageBytes;

    const Platform::MutexLock lock(m_mutex);
    header->next = m_largeBlocks;
    if (m_largeBlocks != nullptr) {
        m_largeBlocks->previous = header;
    }
    m_largeBlocks = header;
    m_counters.OnAllocate(size);
    m_counters.AddReserved(pageBytes);
    return static_cast<void*>(block);
}

void Heap::DeallocateLarge(void* block, usize size) {
    const Detail::HeapLargeBlock* const header = LargeHeaderOf(block);
    void* pages = nullptr;
    usize pageBytes = 0;
    {
        const Platform::MutexLock lock(m_mutex);
        if (header->previous != nullptr) {
            header->previous->next = header->next;
        } else {
            NX_ASSERT(m_largeBlocks == header, "Large block is not from this heap.");
            m_largeBlocks = header->next;
        }
        if (header->next != nullptr) {
            header->next->previous = header->previous;
        }
        pages = header->pages;
        pageBytes = header->pageBytes;
        m_counters.OnDeallocate(size);
        m_counters.RemoveReserved(pageBytes);
    }
    m_pages->Deallocate(pages, pageBytes);
}

Result<void, Error> Heap::TryGrow(usize size, usize alignment) {
    // Room for the request after the free list rounds it up to its size class, plus every header in the pool.
    const usize search = AlignUp(size, kMinimumAlignment) + (alignment > kMinimumAlignment ? alignment + 32 : 0);
    const usize needed = kPoolHeaderSize + FreeListAllocator::kMinimumPoolSize + search + search / 16;
    const usize poolBytes = needed > m_desc.poolSize ? needed : m_desc.poolSize;
    NX_TRY_ASSIGN(void* const memory, m_pages->TryAllocate(poolBytes, kMinimumAlignment));

    Detail::HeapPool* const pool = ConstructAt<Detail::HeapPool>(memory);
    pool->next = m_pools;
    pool->size = poolBytes;
    m_pools = pool;
    m_freeList.AddPool(Span<u8>(static_cast<u8*>(memory) + kPoolHeaderSize, poolBytes - kPoolHeaderSize));
    m_counters.AddReserved(poolBytes);
    return {};
}

} // namespace Nexyl::Memory
