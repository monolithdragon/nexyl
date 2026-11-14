#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Std/Atomic.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// How an atomic operation orders the memory accesses around it. Every operation names one (rule CONC-4).
/// @since 0.1
enum class MemoryOrder : u8 {
    /// No ordering; only the operation itself is atomic. For counters and statistics.
    Relaxed,
    /// Reads and writes after this load stay after it. Pairs with a Release store.
    Acquire,
    /// Reads and writes before this store stay before it. Pairs with an Acquire load.
    Release,
    /// Acquire and Release together, for read-modify-write operations.
    AcquireRelease,
    /// One total order over all sequentially consistent operations.
    SequentiallyConsistent,
};

namespace Detail {

/// Converts a MemoryOrder into the standard library's value.
/// @param order The order.
/// @return The matching `std::memory_order`.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr std::memory_order ToStandardOrder(MemoryOrder order) {
    switch (order) {
        case MemoryOrder::Relaxed:
            return std::memory_order_relaxed;
        case MemoryOrder::Acquire:
            return std::memory_order_acquire;
        case MemoryOrder::Release:
            return std::memory_order_release;
        case MemoryOrder::AcquireRelease:
            return std::memory_order_acq_rel;
        case MemoryOrder::SequentiallyConsistent:
            return std::memory_order_seq_cst;
    }
    return std::memory_order_seq_cst;
}

} // namespace Detail

/// A value that threads read and modify concurrently without a data race (docs/CODING_STANDARDS.md, section 12).
///
/// Holds an integer, an enumeration, a pointer or another small trivially copyable value, and is always
/// lock-free. Every operation states its memory order (rule CONC-4). An Atomic is neither copyable nor movable.
/// The constructors are `constexpr`, so a `constinit` global needs no static constructor (rule INIT-1).
///
/// ```cpp
/// constinit Atomic<u64> g_allocationCount{0};
/// g_allocationCount.FetchAdd(1, MemoryOrder::Relaxed);
/// ```
/// @threadsafety safe
/// @since 0.1
template <typename T>
    requires TriviallyCopyable<T>
class Atomic final {
    static_assert(std::atomic<T>::is_always_lock_free, "Atomic<T> must be lock-free on every platform.");

public:
    /// Creates an atomic holding a value-initialised `T`. @since 0.1
    constexpr Atomic() = default;

    /// Creates an atomic holding `value`. @since 0.1
    constexpr explicit Atomic(T value) : m_value(value) {}

    /// Destroys the atomic. @since 0.1
    ~Atomic() = default;

    Atomic(const Atomic&) = delete;
    Atomic& operator=(const Atomic&) = delete;
    Atomic(Atomic&&) = delete;
    Atomic& operator=(Atomic&&) = delete;

    /// Reads the value.
    /// @param order Relaxed, Acquire or SequentiallyConsistent.
    /// @return The value.
    /// @since 0.1
    [[nodiscard]] T Load(MemoryOrder order) const {
        NX_ASSERT(order != MemoryOrder::Release && order != MemoryOrder::AcquireRelease,
                  "A load cannot have release semantics.");
        return m_value.load(Detail::ToStandardOrder(order));
    }

    /// Writes the value.
    /// @param value The new value.
    /// @param order Relaxed, Release or SequentiallyConsistent.
    /// @since 0.1
    void Store(T value, MemoryOrder order) {
        NX_ASSERT(order != MemoryOrder::Acquire && order != MemoryOrder::AcquireRelease,
                  "A store cannot have acquire semantics.");
        m_value.store(value, Detail::ToStandardOrder(order));
    }

    /// Replaces the value and returns the previous one.
    /// @param value The new value.
    /// @param order The memory order.
    /// @return The value before the exchange.
    /// @since 0.1
    T Exchange(T value, MemoryOrder order) {
        return m_value.exchange(value, Detail::ToStandardOrder(order));
    }

    /// Replaces the value with `desired` if it equals `expected`; otherwise loads it into `expected`.
    /// @param expected The value assumed to be stored. Receives the actual value when the exchange fails.
    /// @param desired  The value to store.
    /// @param order    The memory order of a successful exchange. A failed one uses the matching load order.
    /// @return True if the value was replaced.
    /// @since 0.1
    [[nodiscard]] bool CompareExchange(T& expected, T desired, MemoryOrder order) {
        return m_value.compare_exchange_strong(expected, desired, Detail::ToStandardOrder(order));
    }

    /// Adds `delta` and returns the previous value. Unsigned integers wrap around.
    /// @param delta The amount to add.
    /// @param order The memory order.
    /// @return The value before the addition.
    /// @since 0.1
    T FetchAdd(T delta, MemoryOrder order)
        requires(Integral<T> && !SameAs<T, bool>)
    {
        return m_value.fetch_add(delta, Detail::ToStandardOrder(order));
    }

    /// Subtracts `delta` and returns the previous value. Unsigned integers wrap around.
    /// @param delta The amount to subtract.
    /// @param order The memory order.
    /// @return The value before the subtraction.
    /// @since 0.1
    T FetchSub(T delta, MemoryOrder order)
        requires(Integral<T> && !SameAs<T, bool>)
    {
        return m_value.fetch_sub(delta, Detail::ToStandardOrder(order));
    }

private:
    std::atomic<T> m_value{};
};

} // namespace Nexyl
