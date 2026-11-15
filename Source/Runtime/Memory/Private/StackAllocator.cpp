#include <Nexyl/Memory/StackAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/Sanitizer.h>

#include "BumpRegion.h"

namespace Nexyl::Memory {

StackAllocator::StackAllocator(Span<u8> buffer) : m_base(buffer.Data()), m_capacity(buffer.Size()) {
    Platform::PoisonMemoryRegion(m_base, m_capacity);
    m_counters.AddReserved(m_capacity);
}

StackAllocator::~StackAllocator() {
    // The buffer goes back to its owner, who may use it for anything.
    Platform::UnpoisonMemoryRegion(m_base, m_capacity);
}

Result<void*, Error> StackAllocator::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0 && IsPowerOfTwo(alignment), "Allocation needs a non-zero size and a power-of-two alignment.");
    const Option<usize> start = Detail::FitBlock(m_base, m_capacity, m_used, size, alignment);
    if (!start.HasValue()) {
        return Error(MemoryError::CapacityExceeded);
    }
    u8* const block = m_base + start.GetValue();
    Platform::UnpoisonMemoryRegion(block, size);
    m_lastBlockOffset = start.GetValue();
    m_hasLastBlock = true;
    m_used = start.GetValue() + size;
    m_counters.OnAllocate(size);
    return static_cast<void*>(block);
}

Result<void*, Error> StackAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    NX_ASSERT(Owns(block) && newSize != 0, "Reallocate needs a block from this allocator and a non-zero size.");
    const u8* const bytes = static_cast<const u8*>(block);
    if (m_hasLastBlock && bytes == m_base + m_lastBlockOffset && newSize <= m_capacity - m_lastBlockOffset) {
        if (newSize < oldSize) {
            Platform::PoisonMemoryRegion(bytes + newSize, oldSize - newSize);
        } else {
            Platform::UnpoisonMemoryRegion(bytes + oldSize, newSize - oldSize);
        }
        m_used = m_lastBlockOffset + newSize;
        m_counters.OnResize(oldSize, newSize);
        return block;
    }
    NX_TRY_ASSIGN(void* const moved, TryAllocate(newSize, alignment));
    Copy(moved, block, oldSize < newSize ? oldSize : newSize);
    Deallocate(block, oldSize);
    return moved;
}

void StackAllocator::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    NX_ASSERT(Owns(block), "Block does not come from this StackAllocator.");
    m_counters.OnDeallocate(size);
    const u8* const bytes = static_cast<const u8*>(block);
    // Only the top block can give its memory back at once: below it, the space is still in use.
    if (m_hasLastBlock && bytes == m_base + m_lastBlockOffset) {
        Platform::PoisonMemoryRegion(bytes, size);
        m_used = m_lastBlockOffset;
        m_hasLastBlock = false;
    }
}

bool StackAllocator::Owns(const void* block) const {
    const usize address = ToAddress(block);
    return address >= ToAddress(m_base) && address < ToAddress(m_base) + m_capacity;
}

AllocatorStatistics StackAllocator::GetStatistics() const {
    return m_counters.Get();
}

StackMarker StackAllocator::GetMarker() const {
    const AllocatorStatistics& statistics = m_counters.Get();
    return StackMarker{m_used, statistics.bytesInUse, statistics.liveAllocationCount};
}

void StackAllocator::FreeToMarker(const StackMarker& marker) {
    NX_ASSERT(marker.offset <= m_used, "The stack was already rewound past this marker.");
    Platform::PoisonMemoryRegion(m_base + marker.offset, m_used - marker.offset);
    m_used = marker.offset;
    m_hasLastBlock = false;
    m_counters.Restore(marker.bytesInUse, marker.liveAllocationCount);
}

} // namespace Nexyl::Memory
