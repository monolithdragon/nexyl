#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Memory {

/// Largest block any allocator accepts. A larger request fails with OutOfMemory instead of overflowing the size
/// arithmetic inside the allocator.
/// @since 0.1
inline constexpr usize kMaxAllocationSize = usize{1} << 46U;

/// The counters every allocator reports (docs/NEXYL_ARCHITECTURE.md, section 5.3).
/// @threadsafety safe
/// @since 0.1
struct AllocatorStatistics {
    /// Bytes in blocks handed out and not yet released, as the callers requested them.
    usize bytesInUse = 0;
    /// The highest value `bytesInUse` has reached.
    usize peakBytesInUse = 0;
    /// Blocks handed out and not yet released. Arena-style allocators count until they are reset.
    usize liveAllocationCount = 0;
    /// Blocks handed out since the allocator was created.
    u64 totalAllocationCount = 0;
    /// Memory the allocator holds from its parent or the operating system, including its own overhead.
    usize bytesReserved = 0;
};

/// The interface of every allocator (docs/NEXYL_ARCHITECTURE.md, section 5.3).
///
/// Engine code names an allocator for every allocation (rule OWN-2). A block is released through the allocator
/// that produced it, with its size passed back (sized deallocation), so allocators need no per-block size header.
/// Most code allocates through the helpers in Allocation.h, which terminate through the out-of-memory handler;
/// TryAllocate is for large or optional requests where the caller can fall back (section 5.8).
///
/// Thread safety is a property of each implementation and is stated in its documentation. An allocator is
/// neither copied nor moved while blocks from it are alive, because containers store its address.
///
/// ```cpp
/// void* const block = Memory::Allocate(allocator, 256);
/// // ...
/// allocator.Deallocate(block, 256);
/// ```
/// @threadsafety Stated by each implementation.
/// @since 0.1
class IAllocator {
public:
    /// Allocates a block, or reports why it cannot.
    /// @param size      Bytes. Not zero.
    /// @param alignment A power of two.
    /// @return The block, aligned to `alignment`. Its contents are unspecified.
    /// @error OutOfMemory The parent allocator or the system cannot provide the memory, or `size` exceeds
    ///        kMaxAllocationSize.
    /// @error CapacityExceeded An allocator with a fixed capacity is full.
    /// @since 0.1
    virtual Result<void*, Error> TryAllocate(usize size, usize alignment) = 0;

    /// Resizes a block and keeps its contents up to the smaller of the two sizes. The block may move.
    /// @param block     A block from this allocator.
    /// @param oldSize   The size it was allocated or last resized with.
    /// @param newSize   The new size. Not zero.
    /// @param alignment The alignment it was allocated with.
    /// @return The block: `block` itself if it was resized in place. On error, `block` is unchanged and valid.
    /// @error OutOfMemory, CapacityExceeded As for TryAllocate.
    /// @since 0.1
    virtual Result<void*, Error> TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) = 0;

    /// Releases a block. Arena-style allocators do nothing; they release their memory in bulk.
    /// @param block A block from this allocator, or null, which is ignored.
    /// @param size  The size it was allocated or last resized with.
    /// @since 0.1
    virtual void Deallocate(void* block, usize size) = 0;

    /// Checks whether a block may come from this allocator. For debug checks; it may be slow.
    /// @param block The block.
    /// @return False if the block certainly does not come from here. An allocator that cannot tell returns true.
    /// @since 0.1
    [[nodiscard]] virtual bool Owns(const void* block) const = 0;

    /// Returns the allocator's counters.
    /// @return A snapshot of the counters.
    /// @since 0.1
    [[nodiscard]] virtual AllocatorStatistics GetStatistics() const = 0;

protected:
    /// Interfaces are created only as part of an implementation. @since 0.1
    IAllocator() = default;
    /// Objects are never destroyed through the interface (rule OWN-8). @since 0.1
    ~IAllocator() = default;
    /// Copies the interface part of an implementation. @since 0.1
    IAllocator(const IAllocator&) = default;
    /// Copies the interface part of an implementation. @since 0.1
    IAllocator& operator=(const IAllocator&) = default;
    /// Moves the interface part of an implementation. @since 0.1
    IAllocator(IAllocator&&) = default;
    /// Moves the interface part of an implementation. @since 0.1
    IAllocator& operator=(IAllocator&&) = default;
};

} // namespace Nexyl::Memory
