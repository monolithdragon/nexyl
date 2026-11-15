#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/IAllocator.h>

// The counters behind AllocatorStatistics, for allocator implementations. AllocatorCounters serves allocators that
// one thread uses at a time; ConcurrentAllocatorCounters serves thread-safe ones. The two are one type in two
// variants, so they share this header (rule HDR-9).

namespace Nexyl::Memory {

/// Keeps AllocatorStatistics up to date for an allocator that one thread uses at a time.
/// @threadsafety compatible
/// @since 0.1
class AllocatorCounters final {
public:
    /// Records a block handed out.
    /// @param size The requested size.
    /// @since 0.1
    void OnAllocate(usize size) {
        m_statistics.bytesInUse += size;
        if (m_statistics.bytesInUse > m_statistics.peakBytesInUse) {
            m_statistics.peakBytesInUse = m_statistics.bytesInUse;
        }
        ++m_statistics.liveAllocationCount;
        ++m_statistics.totalAllocationCount;
    }

    /// Records a block released.
    /// @param size The size it was allocated or last resized with.
    /// @since 0.1
    void OnDeallocate(usize size) {
        NX_ASSERT(m_statistics.liveAllocationCount > 0 && m_statistics.bytesInUse >= size,
                  "More memory released than allocated.");
        m_statistics.bytesInUse -= size;
        --m_statistics.liveAllocationCount;
    }

    /// Records a block resized.
    /// @param oldSize The previous size.
    /// @param newSize The new size.
    /// @since 0.1
    void OnResize(usize oldSize, usize newSize) {
        NX_ASSERT(m_statistics.bytesInUse >= oldSize, "A block is larger than the bytes in use.");
        m_statistics.bytesInUse = m_statistics.bytesInUse - oldSize + newSize;
        if (m_statistics.bytesInUse > m_statistics.peakBytesInUse) {
            m_statistics.peakBytesInUse = m_statistics.bytesInUse;
        }
    }

    /// Records that every block was released at once, as an arena reset does. @since 0.1
    void OnReset() {
        m_statistics.bytesInUse = 0;
        m_statistics.liveAllocationCount = 0;
    }

    /// Restores the in-use counters to an earlier snapshot, as a stack allocator does when it rewinds.
    /// @param bytesInUse          The earlier bytes in use.
    /// @param liveAllocationCount The earlier number of live blocks.
    /// @since 0.1
    void Restore(usize bytesInUse, usize liveAllocationCount) {
        m_statistics.bytesInUse = bytesInUse;
        m_statistics.liveAllocationCount = liveAllocationCount;
    }

    /// Records memory taken from the parent allocator or the system. @param bytes The amount. @since 0.1
    void AddReserved(usize bytes) {
        m_statistics.bytesReserved += bytes;
    }

    /// Records memory returned to the parent allocator or the system. @param bytes The amount. @since 0.1
    void RemoveReserved(usize bytes) {
        NX_ASSERT(m_statistics.bytesReserved >= bytes, "More memory returned than taken.");
        m_statistics.bytesReserved -= bytes;
    }

    /// Returns the statistics. @since 0.1
    [[nodiscard]] const AllocatorStatistics& Get() const {
        return m_statistics;
    }

private:
    AllocatorStatistics m_statistics;
};

/// Keeps AllocatorStatistics up to date for a thread-safe allocator. Each counter is updated atomically; a snapshot
/// may combine counters from slightly different moments.
/// @threadsafety safe
/// @since 0.1
class ConcurrentAllocatorCounters final {
public:
    /// Records a block handed out. @param size The requested size. @since 0.1
    void OnAllocate(usize size) {
        RaisePeak(m_bytesInUse.FetchAdd(size, MemoryOrder::Relaxed) + size);
        m_liveAllocationCount.FetchAdd(1, MemoryOrder::Relaxed);
        m_totalAllocationCount.FetchAdd(1, MemoryOrder::Relaxed);
    }

    /// Records a block released. @param size The size it was allocated or last resized with. @since 0.1
    void OnDeallocate(usize size) {
        [[maybe_unused]] const usize previousBytes = m_bytesInUse.FetchSub(size, MemoryOrder::Relaxed);
        [[maybe_unused]] const usize previousCount = m_liveAllocationCount.FetchSub(1, MemoryOrder::Relaxed);
        NX_ASSERT(previousBytes >= size && previousCount > 0, "More memory released than allocated.");
    }

    /// Records a block resized. @param oldSize The previous size. @param newSize The new size. @since 0.1
    void OnResize(usize oldSize, usize newSize) {
        if (newSize >= oldSize) {
            RaisePeak(m_bytesInUse.FetchAdd(newSize - oldSize, MemoryOrder::Relaxed) + (newSize - oldSize));
        } else {
            m_bytesInUse.FetchSub(oldSize - newSize, MemoryOrder::Relaxed);
        }
    }

    /// Records memory taken from the parent allocator or the system. @param bytes The amount. @since 0.1
    void AddReserved(usize bytes) {
        m_bytesReserved.FetchAdd(bytes, MemoryOrder::Relaxed);
    }

    /// Records memory returned to the parent allocator or the system. @param bytes The amount. @since 0.1
    void RemoveReserved(usize bytes) {
        m_bytesReserved.FetchSub(bytes, MemoryOrder::Relaxed);
    }

    /// Returns a snapshot of the statistics. @since 0.1
    [[nodiscard]] AllocatorStatistics Get() const {
        AllocatorStatistics statistics;
        statistics.bytesInUse = m_bytesInUse.Load(MemoryOrder::Relaxed);
        statistics.peakBytesInUse = m_peakBytesInUse.Load(MemoryOrder::Relaxed);
        statistics.liveAllocationCount = m_liveAllocationCount.Load(MemoryOrder::Relaxed);
        statistics.totalAllocationCount = m_totalAllocationCount.Load(MemoryOrder::Relaxed);
        statistics.bytesReserved = m_bytesReserved.Load(MemoryOrder::Relaxed);
        return statistics;
    }

private:
    void RaisePeak(usize candidate) {
        usize peak = m_peakBytesInUse.Load(MemoryOrder::Relaxed);
        while (candidate > peak && !m_peakBytesInUse.CompareExchange(peak, candidate, MemoryOrder::Relaxed)) {}
    }

    Atomic<usize> m_bytesInUse;
    Atomic<usize> m_peakBytesInUse;
    Atomic<usize> m_liveAllocationCount;
    Atomic<u64> m_totalAllocationCount;
    Atomic<usize> m_bytesReserved;
};

} // namespace Nexyl::Memory
