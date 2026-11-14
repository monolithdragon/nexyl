#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/AllocatorCounters.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Memory {

/// A debugging allocator that puts every block on pages of its own, between inaccessible guard pages
/// (docs/NEXYL_ARCHITECTURE.md, section 5.4; rule TEST-6).
///
/// Each block ends at the start of a guard page, so writing even one byte past its end faults at once, and a
/// guard page in front catches writes before larger blocks. A released block's pages stay reserved and
/// inaccessible until the process ends, so a use after free also faults instead of reading reused memory. This
/// costs at least one allocation granule of address space per block and is meant for tests and bug hunts, not
/// for production use. Bytes between the end of a block and the guard page, left over by alignment, are not
/// guarded; AddressSanitizer covers them.
///
/// ```cpp
/// GuardAllocator guarded;
/// RunContainerTests(guarded);
/// ```
/// @threadsafety safe
/// @since 0.1
class GuardAllocator final : public IAllocator {
public:
    /// Creates the allocator. @since 0.1
    NX_MEMORY_API GuardAllocator();
    /// Destroys the allocator. Released blocks keep their address space. @pre Every block has been released.
    /// @since 0.1
    NX_MEMORY_API ~GuardAllocator();

    GuardAllocator(const GuardAllocator&) = delete;
    GuardAllocator& operator=(const GuardAllocator&) = delete;
    GuardAllocator(GuardAllocator&&) = delete;
    GuardAllocator& operator=(GuardAllocator&&) = delete;

    /// See IAllocator::TryAllocate. @pre `alignment` is at most the page size. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryAllocate(usize size, usize alignment) override;
    /// See IAllocator::TryReallocate. Always moves the block, so that stale pointers fault. @since 0.1
    NX_MEMORY_API Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize,
                                                     usize alignment) override;
    /// See IAllocator::Deallocate. Makes the block's pages inaccessible for good. @since 0.1
    NX_MEMORY_API void Deallocate(void* block, usize size) override;
    /// See IAllocator::Owns. Cannot tell, and returns true for any non-null block. @since 0.1
    [[nodiscard]] NX_MEMORY_API bool Owns(const void* block) const override;
    /// See IAllocator::GetStatistics. `bytesReserved` counts the committed pages of live blocks. @since 0.1
    [[nodiscard]] NX_MEMORY_API AllocatorStatistics GetStatistics() const override;

private:
    usize m_pageSize = 0;
    usize m_allocationGranularity = 0;
    ConcurrentAllocatorCounters m_counters;
};

} // namespace Nexyl::Memory
