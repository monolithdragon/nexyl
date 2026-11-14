#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// The function a new thread runs.
/// @since 0.1
using ThreadFunction = void (*)(void* argument);

/// Describes a thread to create.
/// @since 0.1
struct ThreadDesc {
    /// The function the thread runs. Must not be null.
    ThreadFunction function = nullptr;
    /// Passed to `function` unchanged.
    void* argument = nullptr;
    /// Name shown by debuggers and profilers, UTF-8. Platforms truncate long names (Linux: 15 bytes).
    /// Only needs to stay valid until Thread::Create returns.
    const char* name = "";
    /// Stack size in bytes, or zero for the platform default.
    usize stackSize = 0;
};

/// An operating-system thread (rule CONC-1: only Platform, Jobs, audio back ends and connector adapters
/// create threads).
///
/// A Thread is a movable handle. Every created thread MUST be joined before its handle is destroyed;
/// destroying a joinable handle is a programming error that asserts, and joins in builds without assertions.
/// Creating a thread does not allocate memory in the engine.
/// @threadsafety compatible
/// @since 0.1
class Thread final {
public:
    /// Starts a thread.
    ///
    /// @param desc What to run, and how.
    /// @return The handle of the running thread.
    /// @error ResourceLimit The process or system cannot create more threads.
    /// @error InvalidArgument The stack size was rejected.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API static Result<Thread, Error> Create(const ThreadDesc& desc);

    /// Creates an empty handle. @since 0.1
    Thread() noexcept = default;

    /// Moves a handle; `other` becomes empty. @since 0.1
    NX_PLATFORM_API Thread(Thread&& other) noexcept;
    /// Moves a handle; `other` becomes empty. @pre This handle is not joinable. @since 0.1
    NX_PLATFORM_API Thread& operator=(Thread&& other) noexcept;
    /// Destroys the handle. @pre The thread was joined. @since 0.1
    NX_PLATFORM_API ~Thread();

    Thread(const Thread&) = delete;
    Thread& operator=(const Thread&) = delete;

    /// Checks whether the handle refers to a thread that has not been joined yet.
    /// @return True until Join is called.
    /// @since 0.1
    [[nodiscard]] bool IsJoinable() const noexcept {
        return m_joinable;
    }

    /// Waits for the thread to finish and releases it. @pre IsJoinable(). @since 0.1
    NX_PLATFORM_API void Join();

    /// Suspends the calling thread for at least the given time. The platform may round it up.
    /// @param nanoseconds Time to sleep.
    /// @threadsafety safe
    /// @since 0.1
    NX_PLATFORM_API static void SleepFor(u64 nanoseconds);

    /// Gives the rest of the calling thread's time slice to another ready thread.
    /// @threadsafety safe
    /// @since 0.1
    NX_PLATFORM_API static void YieldTimeSlice();

    /// Returns an identifier of the calling thread that is unique among running threads.
    /// Simulation code MUST NOT observe it (rule DET-6).
    /// @return The operating system's thread identifier.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API static u64 GetCurrentId();

private:
    usize m_native = 0;
    bool m_joinable = false;
};

} // namespace Nexyl::Platform
