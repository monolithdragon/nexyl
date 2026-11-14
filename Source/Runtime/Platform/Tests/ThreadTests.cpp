#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Platform/Synchronization.h>
#include <Nexyl/Platform/Thread.h>
#include <Nexyl/Platform/Time.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Platform;

namespace {

struct Counter {
    Mutex mutex;
    u64 value = 0;
    u64 threadId = 0;
};

void IncrementManyTimes(void* argument) {
    auto* const counter = static_cast<Counter*>(argument);
    for (u32 iteration = 0; iteration < 10'000; ++iteration) {
        const MutexLock lock(counter->mutex);
        ++counter->value;
    }
}

void RecordThreadId(void* argument) {
    auto* const counter = static_cast<Counter*>(argument);
    const MutexLock lock(counter->mutex);
    counter->threadId = Thread::GetCurrentId();
}

struct Handshake {
    Mutex mutex;
    ConditionVariable changed;
    u32 turn = 0;
    u32 rounds = 0;
};

// Alternates with the main thread: waits for odd turns, answers with even ones.
void PingPong(void* argument) {
    auto* const handshake = static_cast<Handshake*>(argument);
    const MutexLock lock(handshake->mutex);
    while (handshake->rounds < 100) {
        while (handshake->turn % 2 == 0) {
            handshake->changed.Wait(handshake->mutex);
        }
        ++handshake->turn;
        ++handshake->rounds;
        handshake->changed.NotifyAll();
    }
}

} // namespace

NX_TEST(Thread, RunsAndJoins) {
    Counter counter;
    Result<Thread, Error> created =
        Thread::Create(ThreadDesc{.function = &IncrementManyTimes, .argument = &counter, .name = "Nexyl.Test"});
    NX_REQUIRE(created.IsOk());
    Thread thread = Move(created).TakeValue();
    NX_CHECK(thread.IsJoinable());
    thread.Join();
    NX_CHECK(!thread.IsJoinable());
    NX_CHECK(counter.value == 10'000);
}

NX_TEST(Thread, MutexSerialisesManyThreads) {
    constexpr u32 kThreadCount = 8;
    Counter counter;
    Thread threads[kThreadCount];
    for (Thread& thread : threads) {
        Result<Thread, Error> created = Thread::Create(ThreadDesc{
            .function = &IncrementManyTimes, .argument = &counter, .name = "", .stackSize = usize{256} * 1024});
        NX_REQUIRE(created.IsOk());
        thread = Move(created).TakeValue();
    }
    for (Thread& thread : threads) {
        thread.Join();
    }
    NX_CHECK(counter.value == u64{kThreadCount} * 10'000);
}

NX_TEST(Thread, HasItsOwnId) {
    Counter counter;
    Result<Thread, Error> created = Thread::Create(ThreadDesc{.function = &RecordThreadId, .argument = &counter});
    NX_REQUIRE(created.IsOk());
    Thread thread = Move(created).TakeValue();
    thread.Join();
    NX_CHECK(counter.threadId != 0);
    NX_CHECK(counter.threadId != Thread::GetCurrentId());
}

NX_TEST(Thread, MovedHandleKeepsOwnership) {
    Counter counter;
    Result<Thread, Error> created = Thread::Create(ThreadDesc{.function = &IncrementManyTimes, .argument = &counter});
    NX_REQUIRE(created.IsOk());
    Thread first = Move(created).TakeValue();
    Thread second(Move(first));
    NX_CHECK(second.IsJoinable());
    second.Join();
    NX_CHECK(counter.value == 10'000);
}

NX_TEST(Mutex, TryLockFailsWhileHeld) {
    Mutex mutex;
    NX_REQUIRE(mutex.TryLock());
    mutex.Unlock();
    {
        const MutexLock lock(mutex);
        // TryLock from the owning thread would be relocking; probe from another thread instead.
        struct Probe {
            Mutex* mutex = nullptr;
            bool acquired = true;
        } probe{&mutex};
        Result<Thread, Error> created = Thread::Create(ThreadDesc{.function =
                                                                      [](void* argument) {
                                                                          auto* const state =
                                                                              static_cast<Probe*>(argument);
                                                                          state->acquired = state->mutex->TryLock();
                                                                          if (state->acquired) {
                                                                              state->mutex->Unlock();
                                                                          }
                                                                      },
                                                                  .argument = &probe});
        NX_REQUIRE(created.IsOk());
        Thread thread = Move(created).TakeValue();
        thread.Join();
        NX_CHECK(!probe.acquired);
    }
    NX_CHECK(mutex.TryLock());
    mutex.Unlock();
}

NX_TEST(ConditionVariable, AlternatesBetweenTwoThreads) {
    Handshake handshake;
    Result<Thread, Error> created = Thread::Create(ThreadDesc{.function = &PingPong, .argument = &handshake});
    NX_REQUIRE(created.IsOk());
    Thread thread = Move(created).TakeValue();
    {
        const MutexLock lock(handshake.mutex);
        while (handshake.rounds < 100) {
            ++handshake.turn;
            handshake.changed.NotifyAll();
            while (handshake.turn % 2 == 1) {
                handshake.changed.Wait(handshake.mutex);
            }
        }
    }
    thread.Join();
    NX_CHECK(handshake.rounds == 100);
    NX_CHECK(handshake.turn == 200);
}

NX_TEST(ConditionVariable, WaitForTimesOut) {
    Mutex mutex;
    ConditionVariable never;
    constexpr u64 kTimeout = 20'000'000; // 20 ms
    const MutexLock lock(mutex);
    const u64 start = GetMonotonicNanoseconds();
    bool signalled = true;
    // Spurious wake-ups are allowed, so keep waiting until a timeout is reported.
    for (u32 attempt = 0; attempt < 100 && signalled; ++attempt) {
        signalled = never.WaitFor(mutex, kTimeout);
    }
    const u64 elapsed = GetMonotonicNanoseconds() - start;
    NX_CHECK(!signalled);
    // The bound is loose because Windows timers have about 15 ms resolution.
    NX_CHECK(elapsed >= kTimeout / 2);
}

NX_TEST(Thread, SleepForWaitsAtLeastRoughlyTheRequestedTime) {
    constexpr u64 kSleep = 10'000'000; // 10 ms
    const u64 start = GetMonotonicNanoseconds();
    Thread::SleepFor(kSleep);
    NX_CHECK(GetMonotonicNanoseconds() - start >= kSleep / 2);
    Thread::YieldTimeSlice();
}
