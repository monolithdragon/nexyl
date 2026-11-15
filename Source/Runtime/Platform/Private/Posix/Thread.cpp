#include <Nexyl/Platform/Thread.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Platform/Synchronization.h>
#include <Nexyl/Platform/SystemInfo.h>

#include "Posix/PosixSupport.h"

#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <time.h>
#include <unistd.h>

namespace Nexyl::Platform {

static_assert(sizeof(pthread_t) == sizeof(usize), "Thread stores pthread_t in a usize.");

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
#if NX_PLATFORM_APPLE
    // Naming is diagnostic only; a failure is not worth reporting.
    static_cast<void>(::pthread_setname_np(name));
#else
    // Linux and Android accept at most 15 bytes plus the terminator.
    char truncated[16] = {};
    for (usize index = 0; index < sizeof(truncated) - 1 && name[index] != '\0'; ++index) {
        truncated[index] = name[index];
    }
    static_cast<void>(::pthread_setname_np(::pthread_self(), truncated));
#endif
}

void* ThreadEntry(void* parameter) {
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
    return nullptr;
}

} // namespace

Result<Thread, Error> Thread::Create(const ThreadDesc& desc) {
    NX_ASSERT(desc.function != nullptr, "A thread needs a function.");
    NX_ASSERT(desc.name != nullptr, "Thread name must not be null; use an empty string.");

    pthread_attr_t attributes;
    NX_VERIFY(::pthread_attr_init(&attributes) == 0);
    if (desc.stackSize != 0) {
        // POSIX requires at least PTHREAD_STACK_MIN, and some systems a whole number of pages.
        const usize pageSize = GetSystemInfo().pageSize;
        const auto minimum = static_cast<usize>(PTHREAD_STACK_MIN);
        usize stackSize = desc.stackSize < minimum ? minimum : desc.stackSize;
        stackSize = (stackSize + pageSize - 1) / pageSize * pageSize;
        const int result = ::pthread_attr_setstacksize(&attributes, stackSize);
        if (result != 0) {
            NX_VERIFY(::pthread_attr_destroy(&attributes) == 0);
            return Detail::MakePosixError(result);
        }
    }

    StartBlock block;
    block.function = desc.function;
    block.argument = desc.argument;
    block.name = desc.name;

    pthread_t native = {};
    const int result = ::pthread_create(&native, &attributes, &ThreadEntry, &block);
    NX_VERIFY(::pthread_attr_destroy(&attributes) == 0);
    if (result != 0) {
        // pthread_create reports its error as the return value, not through errno.
        return Detail::MakePosixError(result);
    }

    {
        const MutexLock lock(block.mutex);
        while (!block.isTaken) {
            block.taken.Wait(block.mutex);
        }
    }

    Thread thread;
    thread.m_native = BitCast<usize>(native);
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
    NX_VERIFY(::pthread_join(BitCast<pthread_t>(m_native), nullptr) == 0);
    m_native = 0;
    m_joinable = false;
}

void Thread::SleepFor(u64 nanoseconds) {
    constexpr u64 kNanosecondsPerSecond = 1'000'000'000;
    timespec remaining = {};
    remaining.tv_sec = static_cast<time_t>(nanoseconds / kNanosecondsPerSecond);
    remaining.tv_nsec = static_cast<long>(nanoseconds % kNanosecondsPerSecond);
    // nanosleep stops early when a signal arrives; continue with the time that is left.
    while (::nanosleep(&remaining, &remaining) != 0 && errno == EINTR) {}
}

void Thread::YieldTimeSlice() {
    static_cast<void>(::sched_yield());
}

u64 Thread::GetCurrentId() {
#if NX_PLATFORM_APPLE
    u64 id = 0;
    NX_VERIFY(::pthread_threadid_np(nullptr, &id) == 0);
    return id;
#else
    return static_cast<u64>(::gettid());
#endif
}

} // namespace Nexyl::Platform
