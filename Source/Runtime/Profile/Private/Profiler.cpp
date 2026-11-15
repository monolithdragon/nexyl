#include <Nexyl/Profile/Profiler.h>

#include "TraceWriter.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Platform/File.h>
#include <Nexyl/Platform/Thread.h>
#include <Nexyl/Platform/Time.h>

namespace Nexyl::Profile {

namespace Detail {

struct StoredEvent {
    u64 timeNanoseconds = 0;
    const char* name = "";
    const char* file = "";
    f64 value = 0.0;
    u32 line = 0;
    ProfileEventKind kind = ProfileEventKind::ScopeBegin;
};

struct ThreadBuffer {
    static constexpr usize kMaxNameSize = 64;

    u64 threadId = 0;
    // Set by the owning thread around every recording write. EndCapture waits until it is clear.
    Atomic<u32> writing{0};
    // Written by the owning thread while capturing; read by EndCapture after the writes have stopped.
    u32 count = 0;
    u64 dropped = 0;
    StoredEvent* events = nullptr;
    // Protected by the profiler's registry mutex.
    char name[kMaxNameSize] = {};
};

} // namespace Detail

namespace {

// The buffer of the current thread for one profiler at a time.
struct ThreadCache {
    u64 instance = 0;
    Detail::ThreadBuffer* buffer = nullptr;
};

thread_local constinit ThreadCache g_threadCache{};
constinit Atomic<u64> g_nextInstance{1};

constexpr u64 kNanosecondsPerMicrosecond = 1'000;

[[nodiscard]] bool IsFinite(f64 value) {
    constexpr u64 kExponentMask = 0x7FF0'0000'0000'0000ULL;
    return (BitCast<u64>(value) & kExponentMask) != kExponentMask;
}

} // namespace

Result<Memory::UniquePtr<Profiler>, Error> Profiler::Create(Memory::IAllocator& allocator, const ProfilerDesc& desc) {
    NX_ASSERT(desc.eventsPerThread >= 1 && desc.maxThreads >= 1, "A profiler needs room for events and threads.");
    Memory::UniquePtr<Profiler> profiler = Memory::MakeUnique<Profiler>(allocator, CreateKey{}, allocator, desc);
    NX_TRY(profiler->Start());
    return profiler;
}

Profiler::Profiler(CreateKey /*key*/, Memory::IAllocator& allocator, const ProfilerDesc& desc)
    : m_memory(allocator, kProfileMemoryTag), m_desc(desc), m_binding{&WriteThroughHook, this},
      m_instance(g_nextInstance.FetchAdd(1, MemoryOrder::Relaxed)) {}

Profiler::~Profiler() {
    Uninstall();
    m_capturing.Store(false, MemoryOrder::SequentiallyConsistent);
    if (m_buffers == nullptr) {
        return;
    }
    for (u32 index = 0; index < m_bufferCount; ++index) {
        Detail::ThreadBuffer* const buffer = m_buffers[index];
        m_memory.Deallocate(buffer->events, sizeof(Detail::StoredEvent) * m_desc.eventsPerThread);
        DestroyAt(buffer);
        m_memory.Deallocate(buffer, sizeof(Detail::ThreadBuffer));
    }
    m_memory.Deallocate(static_cast<void*>(m_buffers), sizeof(Detail::ThreadBuffer*) * m_desc.maxThreads);
}

Result<void, Error> Profiler::Start() {
    NX_TRY_ASSIGN(void* const table, m_memory.TryAllocate(sizeof(Detail::ThreadBuffer*) * m_desc.maxThreads,
                                                          alignof(Detail::ThreadBuffer*)));
    m_buffers = static_cast<Detail::ThreadBuffer**>(table);
    for (u32 index = 0; index < m_desc.maxThreads; ++index) {
        m_buffers[index] = nullptr;
    }
    return {};
}

void Profiler::Install() {
    NX_ASSERT(!m_installed, "The profiler is already installed.");
    m_previousBinding = SetProfileSink(&m_binding);
    m_installed = true;
}

void Profiler::Uninstall() {
    if (!m_installed) {
        return;
    }
    const ProfileSinkBinding* const replaced = SetProfileSink(m_previousBinding);
    NX_ASSERT(replaced == &m_binding, "Another profile binding was installed after this profiler.");
    static_cast<void>(replaced);
    m_previousBinding = nullptr;
    m_installed = false;
}

void Profiler::BeginCapture() {
    NX_ASSERT(!IsCapturing(), "A capture is already in progress.");
    {
        const Platform::MutexLock lock(m_registryMutex);
        for (u32 index = 0; index < m_bufferCount; ++index) {
            m_buffers[index]->count = 0;
            m_buffers[index]->dropped = 0;
        }
        m_captureStartNanoseconds = Platform::GetMonotonicNanoseconds();
    }
    m_capturing.Store(true, MemoryOrder::SequentiallyConsistent);
}

Result<void, Error> Profiler::EndCapture(const char* path) {
    NX_ASSERT(IsCapturing(), "No capture is in progress.");
    // Sequentially consistent on both sides: a writer either sees the capture end and records nothing, or has
    // set its flag, which is waited for here.
    m_capturing.Store(false, MemoryOrder::SequentiallyConsistent);
    const Platform::MutexLock lock(m_registryMutex);
    for (u32 index = 0; index < m_bufferCount; ++index) {
        while (m_buffers[index]->writing.Load(MemoryOrder::SequentiallyConsistent) != 0) {
            Platform::Thread::YieldTimeSlice();
        }
    }
    const Result<void, Error> written = WriteTrace(path);
    for (u32 index = 0; index < m_bufferCount; ++index) {
        Detail::ThreadBuffer& buffer = *m_buffers[index];
        m_recorded += buffer.count;
        m_dropped += buffer.dropped;
        buffer.count = 0;
        buffer.dropped = 0;
    }
    return written;
}

bool Profiler::IsCapturing() const {
    return m_capturing.Load(MemoryOrder::Acquire);
}

void Profiler::WriteThroughHook(void* context, const ProfileEvent& event) {
    static_cast<Profiler*>(context)->Write(event);
}

void Profiler::Write(const ProfileEvent& event) {
    Detail::ThreadBuffer* const buffer = AcquireBuffer();
    if (buffer == nullptr) {
        m_droppedUnregistered.FetchAdd(1, MemoryOrder::Relaxed);
        return;
    }
    if (event.kind == ProfileEventKind::ThreadName) {
        const Platform::MutexLock lock(m_registryMutex);
        usize length = 0;
        while (event.name[length] != '\0' && length + 1 < Detail::ThreadBuffer::kMaxNameSize) {
            buffer->name[length] = event.name[length];
            ++length;
        }
        buffer->name[length] = '\0';
        return;
    }
    buffer->writing.Store(1, MemoryOrder::SequentiallyConsistent);
    if (m_capturing.Load(MemoryOrder::SequentiallyConsistent)) {
        if (buffer->count < m_desc.eventsPerThread) {
            Detail::StoredEvent& stored = buffer->events[buffer->count];
            stored.timeNanoseconds = Platform::GetMonotonicNanoseconds();
            stored.name = event.name;
            stored.file = event.location.file;
            stored.line = event.location.line;
            stored.value = event.value;
            stored.kind = event.kind;
            ++buffer->count;
        } else {
            ++buffer->dropped;
        }
    }
    buffer->writing.Store(0, MemoryOrder::Release);
}

ProfilerStatistics Profiler::GetStatistics() const {
    const Platform::MutexLock lock(m_registryMutex);
    ProfilerStatistics statistics;
    statistics.recorded = m_recorded;
    statistics.dropped = m_dropped + m_droppedUnregistered.Load(MemoryOrder::Relaxed);
    statistics.threads = m_bufferCount;
    return statistics;
}

Detail::ThreadBuffer* Profiler::AcquireBuffer() {
    if (g_threadCache.instance == m_instance) {
        return g_threadCache.buffer;
    }
    Detail::ThreadBuffer* const buffer = RegisterThread();
    g_threadCache = ThreadCache{m_instance, buffer};
    return buffer;
}

Detail::ThreadBuffer* Profiler::RegisterThread() {
    const Platform::MutexLock lock(m_registryMutex);
    if (m_bufferCount == m_desc.maxThreads) {
        return nullptr;
    }
    Result<void*, Error> events =
        m_memory.TryAllocate(sizeof(Detail::StoredEvent) * m_desc.eventsPerThread, alignof(Detail::StoredEvent));
    if (events.IsError()) {
        return nullptr;
    }
    Result<void*, Error> memory = m_memory.TryAllocate(sizeof(Detail::ThreadBuffer), alignof(Detail::ThreadBuffer));
    if (memory.IsError()) {
        m_memory.Deallocate(events.GetValue(), sizeof(Detail::StoredEvent) * m_desc.eventsPerThread);
        return nullptr;
    }
    Detail::ThreadBuffer* const buffer = ConstructAt<Detail::ThreadBuffer>(memory.GetValue());
    buffer->threadId = Platform::Thread::GetCurrentId();
    buffer->events = static_cast<Detail::StoredEvent*>(events.GetValue());
    m_buffers[m_bufferCount++] = buffer;
    return buffer;
}

Result<void, Error> Profiler::WriteTrace(const char* path) {
    NX_TRY_ASSIGN(Platform::File file,
                  Platform::File::Open(path, Platform::FileAccess::Write, Platform::FileOpenMode::CreateOrTruncate));
    Detail::TraceWriter writer(file);
    writer.Write(R"({"displayTimeUnit":"ns","traceEvents":[)");
    bool first = true;
    const auto separate = [&writer, &first] {
        writer.Write(first ? Containers::StringView("\n") : Containers::StringView(",\n"));
        first = false;
    };

    for (u32 index = 0; index < m_bufferCount; ++index) {
        const Detail::ThreadBuffer& buffer = *m_buffers[index];
        if (buffer.name[0] == '\0') {
            continue;
        }
        separate();
        writer.WriteFormatted(R"({{"ph":"M","name":"thread_name","pid":1,"tid":{},"args":{{"name":")", buffer.threadId);
        writer.WriteJsonString(buffer.name);
        writer.Write(R"("}})");
    }

    for (u32 index = 0; index < m_bufferCount; ++index) {
        const Detail::ThreadBuffer& buffer = *m_buffers[index];
        for (u32 eventIndex = 0; eventIndex < buffer.count; ++eventIndex) {
            const Detail::StoredEvent& event = buffer.events[eventIndex];
            const u64 elapsed = event.timeNanoseconds > m_captureStartNanoseconds
                                    ? event.timeNanoseconds - m_captureStartNanoseconds
                                    : 0;
            const u64 microseconds = elapsed / kNanosecondsPerMicrosecond;
            const u64 nanoseconds = elapsed % kNanosecondsPerMicrosecond;
            separate();
            switch (event.kind) {
                case ProfileEventKind::ScopeBegin:
                    writer.Write(R"({"ph":"B","name":")");
                    writer.WriteJsonString(event.name);
                    writer.WriteFormatted(R"(","pid":1,"tid":{},"ts":{}.{:03},"args":{{"file":")", buffer.threadId,
                                          microseconds, nanoseconds);
                    writer.WriteJsonString(event.file);
                    writer.WriteFormatted(R"(","line":{}}}}})", event.line);
                    break;
                case ProfileEventKind::ScopeEnd:
                    writer.WriteFormatted(R"({{"ph":"E","pid":1,"tid":{},"ts":{}.{:03}}})", buffer.threadId,
                                          microseconds, nanoseconds);
                    break;
                case ProfileEventKind::Counter:
                    writer.Write(R"({"ph":"C","name":")");
                    writer.WriteJsonString(event.name);
                    // JSON has no infinity or NaN.
                    writer.WriteFormatted(R"(","pid":1,"tid":{},"ts":{}.{:03},"args":{{"value":{}}}}})",
                                          buffer.threadId, microseconds, nanoseconds,
                                          IsFinite(event.value) ? event.value : 0.0);
                    break;
                case ProfileEventKind::FrameMark:
                    writer.WriteFormatted(R"({{"ph":"i","s":"g","name":"Frame","pid":1,"tid":{},"ts":{}.{:03}}})",
                                          buffer.threadId, microseconds, nanoseconds);
                    break;
                case ProfileEventKind::ThreadName:
                    NX_UNREACHABLE();
            }
        }
    }
    writer.Write("\n]}\n");
    NX_TRY(writer.Finish());
    return file.Close();
}

} // namespace Nexyl::Profile
