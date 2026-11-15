#pragma once

#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/ProfileHook.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/Synchronization.h>

namespace Nexyl::Profile {

namespace Detail {

/// The events of one thread. Defined in the implementation. @since 0.1
struct ThreadBuffer;

} // namespace Detail

/// The memory tag of everything the Profile module allocates.
/// @since 0.1
inline constexpr Memory::MemoryTag kProfileMemoryTag = Memory::MemoryTag::Make(ModuleId::Profile);

/// How a Profiler is set up.
/// @threadsafety safe
/// @since 0.1
struct ProfilerDesc {
    /// Events one thread can record in one capture; about 40 bytes each, allocated when the thread first emits
    /// an event. Further events of that capture are dropped and counted.
    u32 eventsPerThread = 32 * 1024;
    /// Threads the profiler records. Events of further threads are dropped and counted.
    u32 maxThreads = 64;
};

/// Counters of a Profiler since its creation, updated when a capture ends.
/// @threadsafety safe
/// @since 0.1
struct ProfilerStatistics {
    /// Events written into captures.
    u64 recorded = 0;
    /// Events lost because a thread's buffer was full or there was no room for another thread.
    u64 dropped = 0;
    /// Threads that have a buffer.
    u32 threads = 0;
};

/// Receives the profile events of every module and writes captures as Chrome Trace Event files: the Profile
/// module's back end.
///
/// The application creates one at startup, next to the Logger and before Jobs (docs/NEXYL_ARCHITECTURE.md,
/// section 5.9), and installs it as the binding of the Base profile hook. Events are recorded only between
/// BeginCapture and EndCapture. Each thread writes into a buffer of its own without locking. EndCapture writes a
/// JSON file that the Perfetto UI (ui.perfetto.dev) or `chrome://tracing` displays.
///
/// Scope and counter names are kept by pointer. A module that wrote events must stay loaded until the capture
/// that holds them has ended.
///
/// ```cpp
/// NX_TRY_ASSIGN(UniquePtr<Profiler> profiler, Profiler::Create(profileMemory));
/// profiler->Install();
/// profiler->BeginCapture();
/// RunFrames(10);
/// NX_TRY(profiler->EndCapture("Saved/Profiles/Startup.json"));
/// ```
/// @threadsafety safe
/// @since 0.1
class Profiler final {
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    /// Creates a profiler. It allocates thread buffers on demand.
    /// @param allocator Supplies the thread buffers. It must outlive the profiler.
    /// @param desc      How the profiler is set up.
    /// @return The profiler.
    /// @error OutOfMemory The thread table cannot be allocated.
    /// @pre `desc.eventsPerThread` and `desc.maxThreads` are at least 1.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PROFILE_API static Result<Memory::UniquePtr<Profiler>, Error>
    Create(Memory::IAllocator& allocator, const ProfilerDesc& desc = {});

    /// Constructs a profiler without a thread table. Only Create can call it.
    /// @param key       Restricts construction to Create.
    /// @param allocator Supplies the thread buffers.
    /// @param desc      How the profiler is set up.
    /// @since 0.1
    NX_PROFILE_API Profiler(CreateKey key, Memory::IAllocator& allocator, const ProfilerDesc& desc);

    /// Uninstalls the profiler and releases its buffers. A capture in progress is discarded.
    /// @pre No other thread is emitting profile events. @since 0.1
    NX_PROFILE_API ~Profiler();

    Profiler(const Profiler&) = delete;
    Profiler& operator=(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;
    Profiler& operator=(Profiler&&) = delete;

    /// Makes this profiler the receiver of the Base profile hook.
    /// @pre The profiler is not installed. No other thread is emitting profile events.
    /// @threadsafety main
    /// @since 0.1
    NX_PROFILE_API void Install();

    /// Gives the Base profile hook back to the binding that Install replaced. Does nothing if not installed.
    /// @pre No other thread is emitting profile events.
    /// @threadsafety main
    /// @since 0.1
    NX_PROFILE_API void Uninstall();

    /// Starts recording events.
    /// @pre No capture is in progress.
    /// @threadsafety main
    /// @since 0.1
    NX_PROFILE_API void BeginCapture();

    /// Stops recording and writes the capture as a Chrome Trace Event JSON file.
    ///
    /// Waits until no thread is inside a recording write, so it may be called while other threads run.
    /// The buffers are emptied whether or not the file could be written.
    /// @param path UTF-8 path of the file to create or truncate. Borrowed for the duration of the call.
    /// @return Success, or the error that stopped the file from being written.
    /// @error Any Platform error of File::Open, File::WriteAt or File::Flush.
    /// @pre A capture is in progress.
    /// @threadsafety main
    /// @since 0.1
    [[nodiscard]] NX_PROFILE_API Result<void, Error> EndCapture(const char* path);

    /// Tells whether a capture is in progress.
    /// @return True between BeginCapture and EndCapture.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PROFILE_API bool IsCapturing() const;

    /// Takes one event, as the Base profile hook does, and records it with the time and the calling thread.
    /// @param event The event. Borrowed for the duration of the call; a thread name is copied.
    /// @threadsafety safe
    /// @since 0.1
    NX_PROFILE_API void Write(const ProfileEvent& event);

    /// Returns the profiler's counters.
    /// @return A snapshot.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PROFILE_API ProfilerStatistics GetStatistics() const;

private:
    static void WriteThroughHook(void* context, const ProfileEvent& event);
    Result<void, Error> Start();
    [[nodiscard]] Detail::ThreadBuffer* AcquireBuffer();
    [[nodiscard]] Detail::ThreadBuffer* RegisterThread();
    Result<void, Error> WriteTrace(const char* path);

    Memory::TrackingAllocator m_memory;
    ProfilerDesc m_desc;
    ProfileSinkBinding m_binding;
    const ProfileSinkBinding* m_previousBinding = nullptr;
    bool m_installed = false;
    // Distinguishes this profiler in the per-thread caches from an earlier one at the same address.
    u64 m_instance = 0;

    Atomic<bool> m_capturing{false};
    u64 m_captureStartNanoseconds = 0;

    // The thread buffers and their names, protected by m_registryMutex.
    mutable Platform::Mutex m_registryMutex;
    Detail::ThreadBuffer** m_buffers = nullptr;
    u32 m_bufferCount = 0;
    u64 m_recorded = 0;
    u64 m_dropped = 0;

    // Events of threads that found no room in the thread table.
    Atomic<u64> m_droppedUnregistered{0};
};

} // namespace Nexyl::Profile
