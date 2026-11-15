#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Log.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Log/ILogSink.h>
#include <Nexyl/Log/Logger.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/Synchronization.h>
#include <Nexyl/Platform/Thread.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Log;

namespace {

constexpr usize kMaxRecords = 2048;

struct Recorded {
    LogLevel level = LogLevel::Info;
    u64 sequence = 0;
    u64 threadId = 0;
    usize length = 0;
    char text[32] = {};
};

class RecordingSink final : public ILogSink {
public:
    Result<void, Error> Write(Span<const LogEntry> entries) override {
        if (m_gate != nullptr) {
            m_gate->WaitUntilOpen();
        }
        for (const LogEntry& entry : entries) {
            if (m_count < kMaxRecords) {
                Recorded& record = m_records[m_count];
                record.level = entry.level;
                record.sequence = entry.sequence;
                record.threadId = entry.threadId;
                record.length = entry.text.Size();
                for (usize index = 0; index < entry.text.Size() && index < sizeof(record.text); ++index) {
                    record.text[index] = entry.text[index];
                }
            }
            ++m_count;
            if (m_logFromSink) {
                LogError("written from inside a sink");
            }
        }
        if (m_failWrites) {
            return Error(Memory::MemoryError::OutOfMemory);
        }
        return {};
    }

    Result<void, Error> Flush() override {
        ++m_flushes;
        return {};
    }

    // Blocks Write until opened, to fill the background queue.
    class Gate final {
    public:
        void WaitUntilOpen() {
            const Platform::MutexLock lock(m_mutex);
            m_waiting = true;
            m_changed.NotifyAll();
            while (!m_open) {
                m_changed.Wait(m_mutex);
            }
        }

        void WaitUntilBlocked() {
            const Platform::MutexLock lock(m_mutex);
            while (!m_waiting) {
                m_changed.Wait(m_mutex);
            }
        }

        void Open() {
            const Platform::MutexLock lock(m_mutex);
            m_open = true;
            m_changed.NotifyAll();
        }

    private:
        Platform::Mutex m_mutex;
        Platform::ConditionVariable m_changed;
        bool m_open = false;
        bool m_waiting = false;
    };

    [[nodiscard]] usize Count() const {
        return m_count;
    }

    [[nodiscard]] usize Flushes() const {
        return m_flushes;
    }

    [[nodiscard]] const Recorded& At(usize index) const {
        return m_records[index];
    }

    [[nodiscard]] bool TextAt(usize index, Containers::StringView expected) const {
        const Recorded& record = m_records[index];
        return Containers::StringView(record.text, record.length) == expected;
    }

    void SetGate(Gate* gate) {
        m_gate = gate;
    }

    void SetFailWrites(bool fail) {
        m_failWrites = fail;
    }

    void SetLogFromSink(bool log) {
        m_logFromSink = log;
    }

private:
    Recorded m_records[kMaxRecords] = {};
    usize m_count = 0;
    usize m_flushes = 0;
    Gate* m_gate = nullptr;
    bool m_failWrites = false;
    bool m_logFromSink = false;
};

[[nodiscard]] LogRecord MakeRecord(LogLevel level, Containers::StringView text) {
    return LogRecord{level, text.Data(), text.Size(), NX_SOURCE_LOCATION};
}

[[nodiscard]] Memory::UniquePtr<Logger> CreateLogger(Memory::IAllocator& allocator, const LoggerDesc& desc) {
    Result<Memory::UniquePtr<Logger>, Error> created = Logger::Create(allocator, desc);
    NX_ASSERT(created.IsOk(), "Logger creation failed in a test.");
    return Move(created).TakeValue();
}

struct WriterContext {
    Logger* logger = nullptr;
    u32 records = 0;
    Atomic<u32> finished{0};
};

void WriteMany(void* argument) {
    auto& context = *static_cast<WriterContext*>(argument);
    for (u32 index = 0; index < context.records; ++index) {
        context.logger->Write(MakeRecord(LogLevel::Info, "from a thread"));
    }
    context.finished.FetchAdd(1, MemoryOrder::Relaxed);
}

} // namespace

NX_TEST(Logger, SynchronousDeliversInOrder) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger =
        CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous});
    NX_REQUIRE(logger->AddSink(*sink).IsOk());

    logger->Write(MakeRecord(LogLevel::Info, "first"));
    logger->Write(MakeRecord(LogLevel::Warning, "second"));
    NX_REQUIRE(sink->Count() == 2);
    NX_CHECK(sink->TextAt(0, "first"));
    NX_CHECK(sink->At(0).sequence == 0);
    NX_CHECK(sink->At(1).level == LogLevel::Warning);
    NX_CHECK(sink->At(1).sequence == 1);
    NX_CHECK(sink->At(0).threadId == Platform::Thread::GetCurrentId());

    const LoggerStatistics statistics = logger->GetStatistics();
    NX_CHECK(statistics.received == 2);
    NX_CHECK(statistics.delivered == 2);
    NX_CHECK(statistics.dropped == 0);
}

NX_TEST(Logger, InstallRoutesTheLogFunctions) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger =
        CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous, .minimumLevel = LogLevel::Warning});
    NX_REQUIRE(logger->AddSink(*sink).IsOk());

    logger->Install();
    NX_CHECK(GetLogMinimumLevel() == LogLevel::Warning);
    LogInfo("hidden");
    LogWarning("{} warnings", 2);
    logger->SetMinimumLevel(LogLevel::Trace);
    LogDebug("now visible");
    logger->Uninstall();
    LogError("after uninstall");

    NX_REQUIRE(sink->Count() == 2);
    NX_CHECK(sink->TextAt(0, "2 warnings"));
    NX_CHECK(sink->TextAt(1, "now visible"));
    SetLogMinimumLevel(LogLevel::Trace);
}

NX_TEST(Logger, UninstallRestoresThePreviousBinding) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> outerSink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<RecordingSink> innerSink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> outer = CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous});
    const Memory::UniquePtr<Logger> inner = CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous});
    NX_REQUIRE(outer->AddSink(*outerSink).IsOk());
    NX_REQUIRE(inner->AddSink(*innerSink).IsOk());

    outer->Install();
    inner->Install();
    LogInfo("to inner");
    inner->Uninstall();
    LogInfo("to outer");
    outer->Uninstall();

    NX_CHECK(innerSink->Count() == 1);
    NX_CHECK(outerSink->Count() == 1);
    NX_CHECK(outerSink->TextAt(0, "to outer"));
    SetLogMinimumLevel(LogLevel::Trace);
}

NX_TEST(Logger, BackgroundDeliversEveryRecordFromManyThreadsInSequenceOrder) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger = CreateLogger(allocator, LoggerDesc{.queueCapacity = 1024});
    NX_REQUIRE(logger->AddSink(*sink).IsOk());

    constexpr u32 kThreads = 4;
    constexpr u32 kRecordsPerThread = 200;
    WriterContext context;
    context.logger = logger.Get();
    context.records = kRecordsPerThread;
    Platform::Thread threads[kThreads];
    for (Platform::Thread& thread : threads) {
        Result<Platform::Thread, Error> created =
            Platform::Thread::Create(Platform::ThreadDesc{&WriteMany, &context, "LogWriter"});
        NX_REQUIRE(created.IsOk());
        thread = Move(created).TakeValue();
    }
    for (Platform::Thread& thread : threads) {
        thread.Join();
    }
    NX_CHECK(context.finished.Load(MemoryOrder::Relaxed) == kThreads);
    logger->Flush();

    NX_REQUIRE(sink->Count() == usize{kThreads} * kRecordsPerThread);
    for (usize index = 0; index < sink->Count(); ++index) {
        NX_CHECK(sink->At(index).sequence == index);
    }
    NX_CHECK(sink->Flushes() >= 1);
    NX_CHECK(logger->GetStatistics().dropped == 0);
}

NX_TEST(Logger, DropsAndCountsWhenTheQueueIsFull) {
    Memory::GuardAllocator allocator;
    RecordingSink::Gate gate;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger = CreateLogger(allocator, LoggerDesc{.queueCapacity = 4});
    sink->SetGate(&gate);
    NX_REQUIRE(logger->AddSink(*sink).IsOk());

    // The first record reaches the sink, which blocks. It keeps its slot until the sink returns, so three more
    // fill the queue of four and the other six are dropped.
    logger->Write(MakeRecord(LogLevel::Info, "blocks the sink"));
    gate.WaitUntilBlocked();
    for (u32 index = 0; index < 9; ++index) {
        logger->Write(MakeRecord(LogLevel::Info, "queued or dropped"));
    }
    gate.Open();
    logger->Flush();

    const LoggerStatistics statistics = logger->GetStatistics();
    NX_CHECK(statistics.received == 10);
    NX_CHECK(statistics.dropped == 6);
    NX_CHECK(statistics.delivered == 4);
    NX_REQUIRE(sink->Count() == 4);
    // The last six were dropped; the delivered ones keep their numbers.
    NX_CHECK(sink->At(3).sequence == 3);
}

NX_TEST(Logger, FatalRecordsAreDeliveredAndFlushedBeforeWriteReturns) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger = CreateLogger(allocator, LoggerDesc{});
    NX_REQUIRE(logger->AddSink(*sink).IsOk());

    logger->Write(MakeRecord(LogLevel::Info, "before"));
    logger->Write(MakeRecord(LogLevel::Fatal, "the end"));
    NX_REQUIRE(sink->Count() == 2);
    NX_CHECK(sink->At(1).level == LogLevel::Fatal);
    NX_CHECK(sink->Flushes() >= 1);
}

NX_TEST(Logger, CountsSinkFailuresAndKeepsGoing) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> failing = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<RecordingSink> working = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger =
        CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous});
    failing->SetFailWrites(true);
    NX_REQUIRE(logger->AddSink(*failing).IsOk());
    NX_REQUIRE(logger->AddSink(*working).IsOk());

    logger->Write(MakeRecord(LogLevel::Info, "one"));
    logger->Write(MakeRecord(LogLevel::Info, "two"));
    NX_CHECK(working->Count() == 2);
    NX_CHECK(logger->GetStatistics().sinkFailures == 2);
}

NX_TEST(Logger, DropsRecordsWrittenByASink) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    const Memory::UniquePtr<Logger> logger =
        CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous});
    sink->SetLogFromSink(true);
    NX_REQUIRE(logger->AddSink(*sink).IsOk());
    logger->Install();

    LogInfo("outer");
    logger->Uninstall();
    NX_CHECK(sink->Count() == 1);
    NX_CHECK(logger->GetStatistics().dropped == 1);
    SetLogMinimumLevel(LogLevel::Trace);
}

NX_TEST(Logger, RejectsMoreThanTheMaximumNumberOfSinks) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    Memory::UniquePtr<RecordingSink> sinks[kMaxLogSinks];
    const Memory::UniquePtr<Logger> logger =
        CreateLogger(allocator, LoggerDesc{.threading = LogThreading::Synchronous});
    for (Memory::UniquePtr<RecordingSink>& each : sinks) {
        each = Memory::MakeUnique<RecordingSink>(allocator);
        NX_REQUIRE(logger->AddSink(*each).IsOk());
    }
    const Result<void, Error> full = logger->AddSink(*sink);
    NX_REQUIRE(full.IsError());
    NX_CHECK(full.GetError().Is(Memory::MemoryError::CapacityExceeded));

    logger->RemoveSink(*sinks[0]);
    NX_CHECK(logger->AddSink(*sink).IsOk());
    logger->Write(MakeRecord(LogLevel::Info, "x"));
    NX_CHECK(sinks[0]->Count() == 0);
    NX_CHECK(sink->Count() == 1);
}

NX_TEST(Logger, DestructionDeliversWhatIsQueued) {
    Memory::GuardAllocator allocator;
    // Sinks outlive the logger that calls them.
    const Memory::UniquePtr<RecordingSink> sink = Memory::MakeUnique<RecordingSink>(allocator);
    {
        const Memory::UniquePtr<Logger> logger = CreateLogger(allocator, LoggerDesc{.queueCapacity = 64});
        NX_REQUIRE(logger->AddSink(*sink).IsOk());
        for (u32 index = 0; index < 50; ++index) {
            logger->Write(MakeRecord(LogLevel::Info, "queued"));
        }
    }
    NX_CHECK(sink->Count() == 50);
    NX_CHECK(sink->Flushes() >= 1);
}
