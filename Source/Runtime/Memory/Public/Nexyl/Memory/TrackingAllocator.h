#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryTag.h>

namespace Nexyl::Memory {

/// Passes every call to another allocator and counts the blocks of one owner, named by a tag
/// (docs/NEXYL_ARCHITECTURE.md, sections 5.4 and 5.8).
///
/// Its statistics are those of the owner alone, so budgets and leak reports can be given per tag. Destroying it
/// while blocks are alive is reported as a leak: a tracking allocator per lifetime boundary checks that the
/// boundary releases everything (section 5.9). Call sites are recorded once Containers provides a map to keep
/// them in.
///
/// ```cpp
/// TrackingAllocator physicsMemory(heap, MemoryTag::Make(ModuleId::Physics));
/// ```
/// @threadsafety As the allocator it passes calls to; the counting itself is safe.
/// @since 0.1
class TrackingAllocator final : public IAllocator {
public:
    /// Creates a tracking allocator.
    /// @param inner The allocator that does the work. It must outlive this one.
    /// @param tag   The owner of the blocks.
    /// @since 0.1
    NX_MEMORY_API TrackingAllocator(IAllocator& inner, MemoryTag tag);
    /// Destroys the allocator. @pre Every block has been released; otherwise a leak is reported. @since 0.1
    NX_MEMORY_API ~TrackingAllocator();

    TrackingAllocator(const TrackingAllocator&) = delete;
    TrackingAllocator& operator=(const TrackingAllocator&) = delete;
    TrackingAllocator(TrackingAllocator&&) = delete;
    TrackingAllocator& operator=(TrackingAllocator&&) = delete;

    /// See IAllocator::TryAllocate. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Answers as the inner allocator does. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. Counts this owner's blocks only; `bytesReserved` is zero. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

    /// Returns the owner's tag. @since 0.1
    [[nodiscard]] MemoryTag GetTag() const {
        return m_tag;
    }

    /// Returns the allocator that does the work. @since 0.1
    [[nodiscard]] IAllocator& GetInner() const {
        return *m_inner;
    }

private:
    IAllocator* m_inner = nullptr;
    MemoryTag m_tag;
    ConcurrentAllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
