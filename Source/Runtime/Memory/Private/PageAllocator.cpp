#include <Nexyl/Memory/PageAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/SystemInfo.h>
#include <Nexyl/Platform/VirtualMemory.h>

namespace Nexyl::Memory {

PageAllocator::PageAllocator() {
    const Platform::SystemInfo info = Platform::GetSystemInfo();
    m_pageSize = info.pageSize;
    m_allocationGranularity = info.allocationGranularity;
}

PageAllocator::~PageAllocator() {
    NX_ASSERT(m_counters.Get().liveAllocationCount == 0, "PageAllocator destroyed while blocks from it are alive.");
}

Result<void*, Error> PageAllocator::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0, "Allocation size must not be zero.");
    NX_ASSERT(IsPowerOfTwo(alignment) && alignment <= m_allocationGranularity,
              "PageAllocator supports power-of-two alignments up to the allocation granularity.");
    if (size > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }

    const usize pages = AlignUp(size, m_pageSize);
    const Result<void*, Error> reserved = Platform::ReserveAddressSpace(pages);
    if (reserved.IsError()) {
        return Error(MemoryError::OutOfMemory, reserved.GetError().GetDetail());
    }
    void* const block = reserved.GetValue();
    const Result<void, Error> committed = Platform::CommitMemory(block, pages);
    if (committed.IsError()) {
        Platform::ReleaseAddressSpace(block, pages);
        return Error(MemoryError::OutOfMemory, committed.GetError().GetDetail());
    }

    m_counters.OnAllocate(size);
    m_counters.AddReserved(pages);
    return block;
}

Result<void*, Error> PageAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    NX_ASSERT(block != nullptr && newSize != 0, "Reallocate needs a block and a non-zero size.");
    if (AlignUp(oldSize, m_pageSize) == AlignUp(newSize, m_pageSize)) {
        m_counters.OnResize(oldSize, newSize);
        return block;
    }
    NX_TRY_ASSIGN(void* const moved, TryAllocate(newSize, alignment));
    Copy(moved, block, oldSize < newSize ? oldSize : newSize);
    Deallocate(block, oldSize);
    return moved;
}

void PageAllocator::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    NX_ASSERT(Owns(block), "Block does not come from this PageAllocator.");
    const usize pages = AlignUp(size, m_pageSize);
    Platform::ReleaseAddressSpace(block, pages);
    m_counters.OnDeallocate(size);
    m_counters.RemoveReserved(pages);
}

bool PageAllocator::Owns(const void* block) const {
    return block != nullptr && IsAligned(block, m_allocationGranularity);
}

AllocatorStatistics PageAllocator::GetStatistics() const {
    return m_counters.Get();
}

} // namespace Nexyl::Memory
