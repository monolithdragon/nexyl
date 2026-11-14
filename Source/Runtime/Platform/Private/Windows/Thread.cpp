#include <Nexyl/Platform/Thread.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Platform/Synchronization.h>

#include "Windows/WindowsSupport.h"

namespace Nexyl::Platform {

namespace {

// Lives on the creating thread's stack. Create waits until the new thread has taken what it needs from it,
// so starting a thread allocates nothing.
struct StartBlock {
    ThreadFunction function = nullptr;
    void* argument = nullptr;
    const char* name = "";
    Mutex mutex;
    ConditionVariable taken;
    bool isTaken = false;
};

void SetCurrentThreadName(const char* name) {
    if (name[0] == '\0') {
        return;
    }
    wchar_t wideName[64] = {};
    if (Detail::ToWide(name, Span<wchar_t>(wideName)).IsOk()) {
        // Naming is diagnostic only; a failure is not worth reporting.
        static_cast<void>(::SetThreadDescription(::GetCurrentThread(), wideName));
    }
}

DWORD WINAPI ThreadEntry(LPVOID parameter) {
    auto* const block = static_cast<StartBlock*>(parameter);
    const ThreadFunction function = block->function;
    void* const argument = block->argument;
    SetCurrentThreadName(block->name);
    {
        const MutexLock lock(block->mutex);
        block->isTaken = true;
        block->taken.NotifyOne();
    }
    // `block` may be gone from here on.
    function(argument);
    return 0;
}

HANDLE AsHandle(usize native) {
    return BitCast<HANDLE>(native);
}

} // namespace

Result<Thread, Error> Thread::Create(const ThreadDesc& desc) {
    NX_ASSERT(desc.function != nullptr, "A thread needs a function.");
    NX_ASSERT(desc.name != nullptr, "Thread name must not be null; use an empty string.");

    StartBlock block;
    block.function = desc.function;
    block.argument = desc.argument;
    block.name = desc.name;

    DWORD threadId = 0;
    void* const handle =
        ::CreateThread(nullptr, desc.stackSize, &ThreadEntry, &block, STACK_SIZE_PARAM_IS_A_RESERVATION, &threadId);
    if (handle == nullptr) {
        return Detail::LastWindowsError();
    }

    {
        const MutexLock lock(block.mutex);
        while (!block.isTaken) {
            block.taken.Wait(block.mutex);
        }
    }

    Thread thread;
    thread.m_native = BitCast<usize>(handle);
    thread.m_joinable = true;
    return thread;
}

Thread::Thread(Thread&& other) noexcept
    : m_native(Exchange(other.m_native, usize{0})), m_joinable(Exchange(other.m_joinable, false)) {}

Thread& Thread::operator=(Thread&& other) noexcept {
    if (this != &other) {
        NX_ASSERT(!m_joinable, "A joinable thread handle must be joined before it is overwritten.");
        if (m_joinable) {
            Join();
        }
        m_native = Exchange(other.m_native, usize{0});
        m_joinable = Exchange(other.m_joinable, false);
    }
    return *this;
}

Thread::~Thread() {
    NX_ASSERT(!m_joinable, "A thread must be joined before its handle is destroyed.");
    if (m_joinable) {
        Join();
    }
}

void Thread::Join() {
    NX_ASSERT(m_joinable, "Join called on a thread that is not joinable.");
    void* const handle = AsHandle(m_native);
    NX_VERIFY(::WaitForSingleObject(handle, INFINITE) == WAIT_OBJECT_0);
    NX_VERIFY(::CloseHandle(handle) != 0);
    m_native = 0;
    m_joinable = false;
}

void Thread::SleepFor(u64 nanoseconds) {
    const u64 milliseconds = (nanoseconds + 999'999) / 1'000'000;
    constexpr u64 kLongest = INFINITE - 1;
    ::Sleep(static_cast<DWORD>(milliseconds < kLongest ? milliseconds : kLongest));
}

void Thread::YieldTimeSlice() {
    static_cast<void>(::SwitchToThread());
}

u64 Thread::GetCurrentId() {
    return ::GetCurrentThreadId();
}

} // namespace Nexyl::Platform
