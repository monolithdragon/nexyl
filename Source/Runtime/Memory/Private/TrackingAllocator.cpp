#include <Nexyl/Memory/TrackingAllocator.h>

#include <Nexyl/Base/Assert.h>

namespace Nexyl::Memory {

TrackingAllocator::TrackingAllocator(IAllocator& inner, MemoryTag tag) : m_inner(&inner), m_tag(tag) {}

TrackingAllocator::~TrackingAllocator() {
    NX_ASSERT(m_counters.Get().liveAllocationCount == 0, "Memory leak: blocks of this tag are still alive.");
}

Result<void*, Error> TrackingAllocator::TryAllocate(usize size, usize alignment) {
    Result<void*, Error> block = m_inner->TryAllocate(size, alignment);
    if (block.IsOk()) {
        m_counters.OnAllocate(size);
    }
    return block;
}

Result<void*, Error> TrackingAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    Result<void*, Error> resized = m_inner->TryReallocate(block, oldSize, newSize, alignment);
    if (resized.IsOk()) {
        m_counters.OnResize(oldSize, newSize);
    }
    return resized;
}

void TrackingAllocator::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    m_inner->Deallocate(block, size);
    m_counters.OnDeallocate(size);
}

bool TrackingAllocator::Owns(const void* block) const {
    return m_inner->Owns(block);
}

AllocatorStatistics TrackingAllocator::GetStatistics() const {
    return m_counters.Get();
}

} // namespace Nexyl::Memory
