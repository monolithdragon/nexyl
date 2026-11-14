#include <Nexyl/Memory/LinearAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/Sanitizer.h>

#include "BumpRegion.h"

namespace Nexyl::Memory {

LinearAllocator::LinearAllocator(Span<u8> buffer) : m_base(buffer.Data()), m_capacity(buffer.Size()) {
    Platform::PoisonMemoryRegion(m_base, m_capacity);
    m_counters.AddReserved(m_capacity);
}

LinearAllocator::~LinearAllocator() {
    // The buffer goes back to its owner, who may use it for anything.
    Platform::UnpoisonMemoryRegion(m_base, m_capacity);
}

Result<void*, Error> LinearAllocator::TryAllocate(usize size, usize alignment) {
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

Result<void*, Error> LinearAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
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
    return moved;
}

void LinearAllocator::Deallocate(void* block, usize /*size*/) {
    NX_ASSERT(block == nullptr || Owns(block), "Block does not come from this LinearAllocator.");
}

bool LinearAllocator::Owns(const void* block) const {
    const usize address = ToAddress(block);
    return address >= ToAddress(m_base) && address < ToAddress(m_base) + m_capacity;
}

AllocatorStatistics LinearAllocator::GetStatistics() const {
    return m_counters.Get();
}

void LinearAllocator::Reset() {
    Platform::PoisonMemoryRegion(m_base, m_used);
    m_used = 0;
    m_hasLastBlock = false;
    m_counters.OnReset();
}

} // namespace Nexyl::Memory
