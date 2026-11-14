#include <Nexyl/Platform/Synchronization.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Construct.h>

#include "Windows/WindowsSupport.h"

namespace Nexyl::Platform {

namespace {

SRWLOCK* AsLock(void* storage) {
    return static_cast<SRWLOCK*>(storage);
}

CONDITION_VARIABLE* AsCondition(void* storage) {
    return static_cast<CONDITION_VARIABLE*>(storage);
}

// Converts a timeout to whole milliseconds, rounding up so the wait is never shorter than requested.
DWORD ToMilliseconds(u64 nanoseconds) {
    const u64 milliseconds = (nanoseconds + 999'999) / 1'000'000;
    constexpr u64 kLongest = INFINITE - 1;
    return static_cast<DWORD>(milliseconds < kLongest ? milliseconds : kLongest);
}

} // namespace

Mutex::Mutex() {
    static_assert(sizeof(SRWLOCK) <= kStorageSize && alignof(SRWLOCK) <= 8);
    ::InitializeSRWLock(ConstructAt<SRWLOCK>(m_storage));
}

Mutex::~Mutex() {
    // SRW locks hold no resources. The destructor only checks the documented precondition.
    const bool isFree = ::TryAcquireSRWLockExclusive(AsLock(m_storage)) != 0;
    if (isFree) {
        ::ReleaseSRWLockExclusive(AsLock(m_storage));
    }
    NX_ASSERT(isFree, "Mutex destroyed while locked.");
}

void Mutex::Lock() {
    ::AcquireSRWLockExclusive(AsLock(m_storage));
}

bool Mutex::TryLock() {
    return ::TryAcquireSRWLockExclusive(AsLock(m_storage)) != 0;
}

void Mutex::Unlock() {
    ::ReleaseSRWLockExclusive(AsLock(m_storage));
}

ConditionVariable::ConditionVariable() {
    static_assert(sizeof(CONDITION_VARIABLE) <= kStorageSize && alignof(CONDITION_VARIABLE) <= 8);
    ::InitializeConditionVariable(ConstructAt<CONDITION_VARIABLE>(m_storage));
}

ConditionVariable::~ConditionVariable() {
    // Windows condition variables hold no resources. The destructor only checks the documented precondition.
    NX_ASSERT(m_waiters == 0, "ConditionVariable destroyed while threads wait on it.");
}

void ConditionVariable::Wait(Mutex& mutex) {
    ++m_waiters;
    NX_VERIFY(::SleepConditionVariableSRW(AsCondition(m_storage), AsLock(NativeMutex(mutex)), INFINITE, 0) != 0);
    --m_waiters;
}

bool ConditionVariable::WaitFor(Mutex& mutex, u64 timeoutNanoseconds) {
    ++m_waiters;
    const bool woken = ::SleepConditionVariableSRW(AsCondition(m_storage), AsLock(NativeMutex(mutex)),
                                                   ToMilliseconds(timeoutNanoseconds), 0) != 0;
    NX_ASSERT(woken || ::GetLastError() == ERROR_TIMEOUT, "SleepConditionVariableSRW failed.");
    --m_waiters;
    return woken;
}

void ConditionVariable::NotifyOne() {
    ::WakeConditionVariable(AsCondition(m_storage));
}

void ConditionVariable::NotifyAll() {
    ::WakeAllConditionVariable(AsCondition(m_storage));
}

} // namespace Nexyl::Platform
