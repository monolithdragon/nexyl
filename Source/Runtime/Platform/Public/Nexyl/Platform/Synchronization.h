#pragma once

#include <Nexyl/Base/Types.h>

// Mutex, MutexLock and ConditionVariable share this header because a condition variable must reach the
// operating-system object inside a mutex (rule HDR-9 allows tightly coupled types in one header).

namespace Nexyl::Platform {

/// A non-recursive mutual-exclusion lock (rule CONC-5).
///
/// Locking a mutex the calling thread already holds is a programming error: it deadlocks, and builds with
/// assertions report it on POSIX. A mutex is neither copyable nor movable, because the operating system
/// object lives inside it. Construction cannot fail.
///
/// Prefer partitioning data by ownership to locking (rule CONC-3). Never hold a mutex across a job wait.
/// @threadsafety safe
/// @since 0.1
class Mutex final {
public:
    /// Creates an unlocked mutex. @since 0.1
    NX_PLATFORM_API Mutex();
    /// Destroys the mutex. @pre The mutex is not locked. @since 0.1
    NX_PLATFORM_API ~Mutex();

    Mutex(const Mutex&) = delete;
    Mutex& operator=(const Mutex&) = delete;
    Mutex(Mutex&&) = delete;
    Mutex& operator=(Mutex&&) = delete;

    /// Blocks until the mutex is acquired. @pre The calling thread does not hold it. @since 0.1
    NX_PLATFORM_API void Lock();

    /// Acquires the mutex if it is free.
    /// @return True if the mutex was acquired.
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API bool TryLock();

    /// Releases the mutex. @pre The calling thread holds it. @since 0.1
    NX_PLATFORM_API void Unlock();

private:
    friend class ConditionVariable;

    // Large enough for the native mutex on every supported platform; checked by static_assert.
    static constexpr usize kStorageSize = 64;
    alignas(8) u8 m_storage[kStorageSize] = {};
};

/// Holds a Mutex for the lifetime of the object (rule OWN-10).
///
/// ```cpp
/// {
///     const MutexLock lock(m_mutex);
///     m_pending.Push(item);
/// }
/// ```
/// @threadsafety compatible
/// @since 0.1
class MutexLock final {
public:
    /// Locks `mutex`. @since 0.1
    explicit MutexLock(Mutex& mutex) : m_mutex(mutex) {
        m_mutex.Lock();
    }

    /// Unlocks the mutex. @since 0.1
    ~MutexLock() {
        m_mutex.Unlock();
    }

    MutexLock(const MutexLock&) = delete;
    MutexLock& operator=(const MutexLock&) = delete;
    MutexLock(MutexLock&&) = delete;
    MutexLock& operator=(MutexLock&&) = delete;

private:
    Mutex& m_mutex;
};

/// Lets threads wait until another thread signals a change of shared state protected by a Mutex.
///
/// Waits can end without a notification (spurious wake-up), so every wait sits in a loop that re-checks the
/// condition:
///
/// ```cpp
/// const MutexLock lock(m_mutex);
/// while (!m_ready) {
///     m_changed.Wait(m_mutex);
/// }
/// ```
/// Timeouts use the monotonic clock, so changes to the system time do not affect them.
/// @threadsafety safe
/// @since 0.1
class ConditionVariable final {
public:
    /// Creates a condition variable. @since 0.1
    NX_PLATFORM_API ConditionVariable();
    /// Destroys it. @pre No thread waits on it. @since 0.1
    NX_PLATFORM_API ~ConditionVariable();

    ConditionVariable(const ConditionVariable&) = delete;
    ConditionVariable& operator=(const ConditionVariable&) = delete;
    ConditionVariable(ConditionVariable&&) = delete;
    ConditionVariable& operator=(ConditionVariable&&) = delete;

    /// Releases `mutex`, waits for a notification, and re-acquires `mutex` before returning.
    /// @param mutex The mutex protecting the shared state.
    /// @pre The calling thread holds `mutex`.
    /// @since 0.1
    NX_PLATFORM_API void Wait(Mutex& mutex);

    /// Like Wait, but gives up after `timeoutNanoseconds`.
    /// @param mutex              The mutex protecting the shared state.
    /// @param timeoutNanoseconds Longest time to wait.
    /// @pre The calling thread holds `mutex`.
    /// @return False if the wait timed out; true if it ended for any other reason.
    /// @since 0.1
    NX_PLATFORM_API bool WaitFor(Mutex& mutex, u64 timeoutNanoseconds);

    /// Wakes one waiting thread, if any. @since 0.1
    NX_PLATFORM_API void NotifyOne();

    /// Wakes all waiting threads. @since 0.1
    NX_PLATFORM_API void NotifyAll();

private:
    static void* NativeMutex(Mutex& mutex) {
        return mutex.m_storage;
    }

    static constexpr usize kStorageSize = 64;
    alignas(8) u8 m_storage[kStorageSize] = {};
    // Threads inside Wait or WaitFor. Only changed while the caller's mutex is held; checked by the destructor.
    u32 m_waiters = 0;
};

} // namespace Nexyl::Platform
