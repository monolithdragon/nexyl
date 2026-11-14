#include <Nexyl/Platform/Synchronization.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Construct.h>

#include "Posix/PosixSupport.h"

#include <pthread.h>
#include <time.h>

namespace Nexyl::Platform {

namespace {

pthread_mutex_t* AsMutex(void* storage) {
    return static_cast<pthread_mutex_t*>(storage);
}

pthread_cond_t* AsCondition(void* storage) {
    return static_cast<pthread_cond_t*>(storage);
}

constexpr u64 kNanosecondsPerSecond = 1'000'000'000;

} // namespace

Mutex::Mutex() {
    static_assert(sizeof(pthread_mutex_t) <= kStorageSize && alignof(pthread_mutex_t) <= 8);
    pthread_mutexattr_t attributes;
    NX_VERIFY(::pthread_mutexattr_init(&attributes) == 0);
#if NX_ASSERTS_ENABLED
    // Error-checking mutexes report relocking and unlocking by a non-owner instead of deadlocking silently.
    NX_VERIFY(::pthread_mutexattr_settype(&attributes, PTHREAD_MUTEX_ERRORCHECK) == 0);
#endif
    NX_VERIFY(::pthread_mutex_init(ConstructAt<pthread_mutex_t>(m_storage), &attributes) == 0);
    NX_VERIFY(::pthread_mutexattr_destroy(&attributes) == 0);
}

Mutex::~Mutex() {
    NX_VERIFY(::pthread_mutex_destroy(AsMutex(m_storage)) == 0);
}

void Mutex::Lock() {
    const int result = ::pthread_mutex_lock(AsMutex(m_storage));
    NX_ASSERT(result != EDEADLK, "Mutex locked twice by the same thread; Nexyl mutexes are not recursive.");
    NX_ASSERT(result == 0, "pthread_mutex_lock failed.");
}

bool Mutex::TryLock() {
    return ::pthread_mutex_trylock(AsMutex(m_storage)) == 0;
}

void Mutex::Unlock() {
    const int result = ::pthread_mutex_unlock(AsMutex(m_storage));
    NX_ASSERT(result == 0, "Mutex unlocked by a thread that does not hold it.");
}

ConditionVariable::ConditionVariable() {
    static_assert(sizeof(pthread_cond_t) <= kStorageSize && alignof(pthread_cond_t) <= 8);
    pthread_condattr_t attributes;
    NX_VERIFY(::pthread_condattr_init(&attributes) == 0);
#if !NX_PLATFORM_APPLE
    // Timed waits measure against the monotonic clock. Apple has no such attribute and uses a relative wait.
    NX_VERIFY(::pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC) == 0);
#endif
    NX_VERIFY(::pthread_cond_init(ConstructAt<pthread_cond_t>(m_storage), &attributes) == 0);
    NX_VERIFY(::pthread_condattr_destroy(&attributes) == 0);
}

ConditionVariable::~ConditionVariable() {
    NX_ASSERT(m_waiters == 0, "ConditionVariable destroyed while threads wait on it.");
    NX_VERIFY(::pthread_cond_destroy(AsCondition(m_storage)) == 0);
}

void ConditionVariable::Wait(Mutex& mutex) {
    ++m_waiters;
    NX_VERIFY(::pthread_cond_wait(AsCondition(m_storage), AsMutex(NativeMutex(mutex))) == 0);
    --m_waiters;
}

bool ConditionVariable::WaitFor(Mutex& mutex, u64 timeoutNanoseconds) {
    ++m_waiters;
#if NX_PLATFORM_APPLE
    timespec relative = {};
    relative.tv_sec = static_cast<time_t>(timeoutNanoseconds / kNanosecondsPerSecond);
    relative.tv_nsec = static_cast<long>(timeoutNanoseconds % kNanosecondsPerSecond);
    const int result =
        ::pthread_cond_timedwait_relative_np(AsCondition(m_storage), AsMutex(NativeMutex(mutex)), &relative);
#else
    timespec deadline = {};
    NX_VERIFY(::clock_gettime(CLOCK_MONOTONIC, &deadline) == 0);
    const u64 nanoseconds = static_cast<u64>(deadline.tv_nsec) + timeoutNanoseconds % kNanosecondsPerSecond;
    deadline.tv_sec +=
        static_cast<time_t>(timeoutNanoseconds / kNanosecondsPerSecond + nanoseconds / kNanosecondsPerSecond);
    deadline.tv_nsec = static_cast<long>(nanoseconds % kNanosecondsPerSecond);
    const int result = ::pthread_cond_timedwait(AsCondition(m_storage), AsMutex(NativeMutex(mutex)), &deadline);
#endif
    NX_ASSERT(result == 0 || result == ETIMEDOUT, "pthread_cond_timedwait failed.");
    --m_waiters;
    return result != ETIMEDOUT;
}

void ConditionVariable::NotifyOne() {
    NX_VERIFY(::pthread_cond_signal(AsCondition(m_storage)) == 0);
}

void ConditionVariable::NotifyAll() {
    NX_VERIFY(::pthread_cond_broadcast(AsCondition(m_storage)) == 0);
}

} // namespace Nexyl::Platform
