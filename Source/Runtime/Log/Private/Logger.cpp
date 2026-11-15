#include <Nexyl/Log/Logger.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Log.h>
#include <Nexyl/Base/Profile.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/Time.h>

namespace Nexyl::Log {

namespace Detail {

struct LogSlot {
    LogLevel level = LogLevel::Info;
    usize length = 0;
    SourceLocation location;
    u64 timeNanoseconds = 0;
    u64 threadId = 0;
    u64 sequence = 0;
    char text[kMaxLogMessageSize] = {};
};

} // namespace Detail

namespace {

// Entries handed to the sinks in one call from the background thread.
constexpr usize kBatchSize = 64;

[[nodiscard]] LogEntry ToEntry(const Detail::LogSlot& slot) {
    return LogEntry{slot.level,    Containers::StringView(slot.text, slot.length),
                    slot.location, slot.timeNanoseconds,
                    slot.threadId, slot.sequence};
}

} // namespace

Result<Memory::UniquePtr<Logger>, Error> Logger::Create(Memory::IAllocator& allocator, const LoggerDesc& desc) {
    NX_ASSERT(desc.queueCapacity >= 1, "A logger needs a queue of at least one record.");
    Memory::UniquePtr<Logger> logger = Memory::MakeUnique<Logger>(allocator, CreateKey{}, allocator, desc);
    NX_TRY(logger->Start());
    return logger;
}

Logger::Logger(CreateKey /*key*/, Memory::IAllocator& allocator, const LoggerDesc& desc)
    : m_memory(allocator, kLogMemoryTag), m_desc(desc), m_binding{&WriteThroughHook, this},
      m_startNanoseconds(Platform::GetMonotonicNanoseconds()) {}

Logger::~Logger() {
    Uninstall();
    if (m_thread.IsJoinable()) {
        m_queueMutex.Lock();
        m_stopping = true;
        m_workAvailable.NotifyOne();
        m_queueMutex.Unlock();
        // The thread delivers what is queued and flushes the sinks before it ends.
        m_thread.Join();
    } else {
        const Platform::MutexLock lock(m_sinkMutex);
        FlushSinks();
    }
    if (m_slots != nullptr) {
        for (usize index = 0; index < m_desc.queueCapacity; ++index) {
            DestroyAt(&m_slots[index]);
        }
        m_memory.Deallocate(m_slots, sizeof(Detail::LogSlot) * m_desc.queueCapacity);
    }
}

Result<void, Error> Logger::Start() {
    if (m_desc.threading == LogThreading::Synchronous) {
        return {};
    }
    NX_TRY_ASSIGN(void* const memory,
                  m_memory.TryAllocate(sizeof(Detail::LogSlot) * m_desc.queueCapacity, alignof(Detail::LogSlot)));
    m_slots = static_cast<Detail::LogSlot*>(memory);
    for (usize index = 0; index < m_desc.queueCapacity; ++index) {
        ConstructAt<Detail::LogSlot>(&m_slots[index]);
    }
    NX_TRY_ASSIGN(Platform::Thread thread, Platform::Thread::Create(Platform::ThreadDesc{&ThreadMain, this, "Log"}));
    m_thread = Move(thread);
    return {};
}

Result<void, Error> Logger::AddSink(ILogSink& sink) {
    const Platform::MutexLock lock(m_sinkMutex);
    for (usize index = 0; index < m_sinkCount; ++index) {
        NX_ASSERT(m_sinks[index] != &sink, "The sink has already been added.");
    }
    if (m_sinkCount == kMaxLogSinks) {
        return Error(Memory::MemoryError::CapacityExceeded);
    }
    m_sinks[m_sinkCount++] = &sink;
    return {};
}

void Logger::RemoveSink(ILogSink& sink) {
    const Platform::MutexLock lock(m_sinkMutex);
    for (usize index = 0; index < m_sinkCount; ++index) {
        if (m_sinks[index] == &sink) {
            // Keep the order of the remaining sinks.
            for (usize next = index + 1; next < m_sinkCount; ++next) {
                m_sinks[next - 1] = m_sinks[next];
            }
            m_sinks[--m_sinkCount] = nullptr;
            return;
        }
    }
    NX_ASSERT(false, "The sink was not added.");
}

void Logger::Install() {
    NX_ASSERT(!m_installed, "The logger is already installed.");
    SetLogMinimumLevel(m_desc.minimumLevel);
    m_previousBinding = SetLogSink(&m_binding);
    m_installed = true;
}

void Logger::Uninstall() {
    if (!m_installed) {
        return;
    }
    const LogSinkBinding* const replaced = SetLogSink(m_previousBinding);
    NX_ASSERT(replaced == &m_binding, "Another log binding was installed after this logger.");
    static_cast<void>(replaced);
    m_previousBinding = nullptr;
    m_installed = false;
}

void Logger::SetMinimumLevel(LogLevel level) {
    m_desc.minimumLevel = level;
    if (m_installed) {
        SetLogMinimumLevel(level);
    }
}

void Logger::WriteThroughHook(void* context, const LogRecord& record) {
    static_cast<Logger*>(context)->Write(record);
}

void Logger::Write(const LogRecord& record) {
    NX_ASSERT(record.level != LogLevel::Off, "LogLevel::Off is not a record level.");
    const u64 threadId = Platform::Thread::GetCurrentId();
    const u64 time = Platform::GetMonotonicNanoseconds() - m_startNanoseconds;
    m_received.FetchAdd(1, MemoryOrder::Relaxed);
    if (m_sinkThreadId.Load(MemoryOrder::Relaxed) == threadId) {
        // A sink is logging: delivering would recurse into the sinks or deadlock.
        m_dropped.FetchAdd(1, MemoryOrder::Relaxed);
        return;
    }
    const usize length = record.length < kMaxLogMessageSize ? record.length : kMaxLogMessageSize;

    if (m_desc.threading == LogThreading::Synchronous) {
        LogEntry entry{record.level, Containers::StringView(record.text, length), record.location, time, threadId, 0};
        WriteSynchronously(entry);
        return;
    }

    {
        const Platform::MutexLock lock(m_queueMutex);
        // Numbered under the lock, so that the queue, and therefore every sink, sees the records in sequence order.
        const u64 sequence = m_nextSequence.FetchAdd(1, MemoryOrder::Relaxed);
        if (m_count == m_desc.queueCapacity) {
            m_dropped.FetchAdd(1, MemoryOrder::Relaxed);
            return;
        }
        Detail::LogSlot& slot = m_slots[(m_head + m_count) % m_desc.queueCapacity];
        slot.level = record.level;
        slot.length = length;
        slot.location = record.location;
        slot.timeNanoseconds = time;
        slot.threadId = threadId;
        slot.sequence = sequence;
        Memory::Copy(slot.text, record.text, length);
        ++m_count;
        ++m_enqueuedTotal;
        m_workAvailable.NotifyOne();
    }
    if (record.level == LogLevel::Fatal) {
        Flush();
    }
}

void Logger::WriteSynchronously(LogEntry& entry) {
    const Platform::MutexLock lock(m_sinkMutex);
    entry.sequence = m_nextSequence.FetchAdd(1, MemoryOrder::Relaxed);
    m_sinkThreadId.Store(entry.threadId, MemoryOrder::Relaxed);
    Deliver(Span<const LogEntry>(&entry, 1));
    if (entry.level == LogLevel::Fatal) {
        FlushSinks();
    }
    m_sinkThreadId.Store(0, MemoryOrder::Relaxed);
}

void Logger::Flush() {
    if (m_sinkThreadId.Load(MemoryOrder::Relaxed) == Platform::Thread::GetCurrentId()) {
        return;
    }
    if (m_desc.threading == LogThreading::Synchronous) {
        const Platform::MutexLock lock(m_sinkMutex);
        FlushSinks();
        return;
    }
    const Platform::MutexLock lock(m_queueMutex);
    m_flushTarget = m_enqueuedTotal;
    const u64 ticket = ++m_flushRequests;
    m_workAvailable.NotifyOne();
    while (m_flushesCompleted < ticket) {
        m_progress.Wait(m_queueMutex);
    }
}

LoggerStatistics Logger::GetStatistics() const {
    LoggerStatistics statistics;
    statistics.received = m_received.Load(MemoryOrder::Relaxed);
    statistics.delivered = m_delivered.Load(MemoryOrder::Relaxed);
    statistics.dropped = m_dropped.Load(MemoryOrder::Relaxed);
    statistics.sinkFailures = m_sinkFailures.Load(MemoryOrder::Relaxed);
    return statistics;
}

void Logger::ThreadMain(void* argument) {
    static_cast<Logger*>(argument)->Run();
}

void Logger::Run() {
    ProfileThreadName("Log");
    m_sinkThreadId.Store(Platform::Thread::GetCurrentId(), MemoryOrder::Relaxed);
    m_queueMutex.Lock();
    for (;;) {
        const bool flushPending = m_flushesCompleted != m_flushRequests;
        if (flushPending && m_deliveredTotal >= m_flushTarget) {
            const u64 requests = m_flushRequests;
            m_queueMutex.Unlock();
            {
                const Platform::MutexLock lock(m_sinkMutex);
                FlushSinks();
            }
            m_queueMutex.Lock();
            m_flushesCompleted = requests;
            m_progress.NotifyAll();
            continue;
        }
        if (m_count > 0) {
            // The slots from m_head stay reserved until m_count shrinks, so they can be read without the lock.
            const usize contiguous = m_desc.queueCapacity - m_head;
            usize batch = m_count < contiguous ? m_count : contiguous;
            batch = batch < kBatchSize ? batch : kBatchSize;
            const usize first = m_head;
            m_queueMutex.Unlock();
            LogEntry entries[kBatchSize];
            for (usize index = 0; index < batch; ++index) {
                entries[index] = ToEntry(m_slots[first + index]);
            }
            {
                const Platform::MutexLock lock(m_sinkMutex);
                Deliver(Span<const LogEntry>(entries, batch));
            }
            m_queueMutex.Lock();
            m_head = (m_head + batch) % m_desc.queueCapacity;
            m_count -= batch;
            m_deliveredTotal += batch;
            continue;
        }
        if (m_stopping) {
            break;
        }
        m_workAvailable.Wait(m_queueMutex);
    }
    m_queueMutex.Unlock();
    const Platform::MutexLock lock(m_sinkMutex);
    FlushSinks();
}

void Logger::Deliver(Span<const LogEntry> entries) {
    for (usize index = 0; index < m_sinkCount; ++index) {
        if (m_sinks[index]->Write(entries).IsError()) {
            m_sinkFailures.FetchAdd(1, MemoryOrder::Relaxed);
        }
    }
    m_delivered.FetchAdd(entries.Size(), MemoryOrder::Relaxed);
}

void Logger::FlushSinks() {
    for (usize index = 0; index < m_sinkCount; ++index) {
        if (m_sinks[index]->Flush().IsError()) {
            m_sinkFailures.FetchAdd(1, MemoryOrder::Relaxed);
        }
    }
}

} // namespace Nexyl::Log
