#pragma once

#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Log/ILogSink.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/Synchronization.h>
#include <Nexyl/Platform/Thread.h>

namespace Nexyl::Log {

namespace Detail {

/// One queued record. Defined in the implementation. @since 0.1
struct LogSlot;

} // namespace Detail

/// The memory tag of everything the Log module allocates.
/// @since 0.1
inline constexpr Memory::MemoryTag kLogMemoryTag = Memory::MemoryTag::Make(ModuleId::Log);

/// The most sinks one Logger holds.
/// @since 0.1
inline constexpr usize kMaxLogSinks = 8;

/// Where a Logger calls its sinks.
/// @since 0.1
enum class LogThreading : u8 {
    /// A thread of the Logger's own calls the sinks. Writers copy the record into a bounded queue and never wait
    /// for output, so jobs may log (docs/NEXYL_ARCHITECTURE.md, section 6.2). When the queue is full, the record
    /// is dropped and counted.
    Background,
    /// The writing thread calls the sinks itself, one writer at a time. For tests and tools.
    Synchronous,
};

/// How a Logger is set up.
/// @threadsafety safe
/// @since 0.1
struct LoggerDesc {
    /// Records the background queue holds, including those being delivered. Each takes about kMaxLogMessageSize
    /// bytes. Ignored when synchronous.
    usize queueCapacity = 1024;
    /// Where the sinks are called.
    LogThreading threading = LogThreading::Background;
    /// The lowest level kept; Install passes it to SetLogMinimumLevel.
    LogLevel minimumLevel = LogLevel::Info;
};

/// Counters of a Logger since its creation.
/// @threadsafety safe
/// @since 0.1
struct LoggerStatistics {
    /// Records passed to Write.
    u64 received = 0;
    /// Records handed to the sinks.
    u64 delivered = 0;
    /// Records dropped because the queue was full or because a sink logged while it ran.
    u64 dropped = 0;
    /// Errors returned by sinks from Write or Flush.
    u64 sinkFailures = 0;
};

/// Receives the records of the log functions and hands them to its sinks: the Log module's back end.
///
/// The application creates one at startup, after Memory and before Jobs (docs/NEXYL_ARCHITECTURE.md, section 5.9),
/// adds sinks, and installs it as the binding of the Base log hook. Every LogInfo call in every module then ends up
/// here. A fatal record is delivered and flushed before Write returns, so it survives the termination that
/// follows. Destroying the Logger uninstalls it, delivers what is queued and flushes the sinks.
///
/// ```cpp
/// TrackingAllocator logMemory(heap, kLogMemoryTag);
/// NX_TRY_ASSIGN(UniquePtr<Logger> logger, Logger::Create(logMemory));
/// ConsoleSink console;
/// NX_TRY(logger->AddSink(console));
/// logger->Install();
/// LogInfo("Engine started in {:.1f} ms", startupMilliseconds);
/// ```
/// @threadsafety safe
/// @since 0.1
class Logger final {
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    /// Creates a logger and, for LogThreading::Background, starts its thread.
    /// @param allocator Supplies the logger and its queue. It must outlive the logger.
    /// @param desc      How the logger is set up.
    /// @return The logger.
    /// @error OutOfMemory   The queue cannot be allocated.
    /// @error ResourceLimit The background thread cannot be started (Platform::Thread::Create).
    /// @pre `desc.queueCapacity` is at least 1.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_LOG_API static Result<Memory::UniquePtr<Logger>, Error> Create(Memory::IAllocator& allocator,
                                                                                    const LoggerDesc& desc = {});

    /// Constructs an unstarted logger. Only Create can call it.
    /// @param key       Restricts construction to Create.
    /// @param allocator Supplies the queue.
    /// @param desc      How the logger is set up.
    /// @since 0.1
    NX_LOG_API Logger(CreateKey key, Memory::IAllocator& allocator, const LoggerDesc& desc);

    /// Uninstalls the logger, delivers the queued records, flushes the sinks and stops the thread. @since 0.1
    NX_LOG_API ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    /// Adds a sink. Records written from now on reach it.
    /// @param sink The sink. It must stay alive until it is removed or the logger is destroyed.
    /// @return Success, or the error.
    /// @error CapacityExceeded The logger already has kMaxLogSinks sinks.
    /// @pre `sink` has not been added.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_LOG_API Result<void, Error> AddSink(ILogSink& sink);

    /// Removes a sink. When the function returns, the logger no longer calls it.
    /// @param sink A sink added earlier.
    /// @threadsafety safe
    /// @since 0.1
    NX_LOG_API void RemoveSink(ILogSink& sink);

    /// Makes this logger the receiver of the Base log hook and applies its minimum level.
    /// @pre The logger is not installed.
    /// @threadsafety main
    /// @since 0.1
    NX_LOG_API void Install();

    /// Gives the Base log hook back to the binding that Install replaced. Does nothing if not installed.
    /// @pre If installed, no other binding has been installed since.
    /// @threadsafety main
    /// @since 0.1
    NX_LOG_API void Uninstall();

    /// Changes the lowest level kept, and the Base minimum level if the logger is installed.
    /// @param level The new minimum level.
    /// @threadsafety main
    /// @since 0.1
    NX_LOG_API void SetMinimumLevel(LogLevel level);

    /// Takes one record, as the Base log hook does. Records below the minimum level are kept too; filtering
    /// happens in Base before formatting.
    /// @param record The record. Borrowed for the duration of the call; the text is copied.
    /// @threadsafety safe
    /// @since 0.1
    NX_LOG_API void Write(const LogRecord& record);

    /// Delivers every record written before the call and flushes the sinks, then returns.
    ///
    /// Called from a sink while it runs, it returns at once.
    /// @threadsafety safe
    /// @since 0.1
    NX_LOG_API void Flush();

    /// Returns the logger's counters.
    /// @return A snapshot; the counters are read one by one.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_LOG_API LoggerStatistics GetStatistics() const;

private:
    static void WriteThroughHook(void* context, const LogRecord& record);
    static void ThreadMain(void* argument);
    Result<void, Error> Start();
    void Run();
    void Deliver(Span<const LogEntry> entries);
    void FlushSinks();
    void WriteSynchronously(LogEntry& entry);

    Memory::TrackingAllocator m_memory;
    LoggerDesc m_desc;
    LogSinkBinding m_binding;
    const LogSinkBinding* m_previousBinding = nullptr;
    bool m_installed = false;
    u64 m_startNanoseconds = 0;

    // The thread that is calling the sinks, or zero: records it writes are dropped instead of deadlocking.
    Atomic<u64> m_sinkThreadId{0};
    // Advanced under m_queueMutex (background) or m_sinkMutex (synchronous).
    Atomic<u64> m_nextSequence{0};
    Atomic<u64> m_received{0};
    Atomic<u64> m_delivered{0};
    Atomic<u64> m_dropped{0};
    Atomic<u64> m_sinkFailures{0};

    // The queue. Lock order: m_queueMutex and m_sinkMutex are never held together.
    Platform::Mutex m_queueMutex;
    Platform::ConditionVariable m_workAvailable;
    Platform::ConditionVariable m_progress;
    // Everything below up to m_thread is protected by m_queueMutex.
    Detail::LogSlot* m_slots = nullptr;
    usize m_head = 0;
    usize m_count = 0;
    u64 m_enqueuedTotal = 0;
    u64 m_deliveredTotal = 0;
    u64 m_flushTarget = 0;
    u64 m_flushRequests = 0;
    u64 m_flushesCompleted = 0;
    bool m_stopping = false;
    Platform::Thread m_thread;

    // The sinks, protected by m_sinkMutex. Held while a sink runs.
    Platform::Mutex m_sinkMutex;
    ILogSink* m_sinks[kMaxLogSinks] = {};
    usize m_sinkCount = 0;
};

} // namespace Nexyl::Log
