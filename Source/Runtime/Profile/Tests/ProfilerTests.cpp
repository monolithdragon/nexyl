#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Profile.h>
#include <Nexyl/Base/ProfileHook.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/File.h>
#include <Nexyl/Platform/FileSystem.h>
#include <Nexyl/Platform/Thread.h>
#include <Nexyl/Profile/Profiler.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Profile;

namespace {

constexpr const char* kTracePath = "NexylProfileTest.json";

[[nodiscard]] Memory::UniquePtr<Profiler> CreateProfiler(Memory::IAllocator& allocator, const ProfilerDesc& desc) {
    Result<Memory::UniquePtr<Profiler>, Error> created = Profiler::Create(allocator, desc);
    NX_ASSERT(created.IsOk(), "Profiler creation failed in a test.");
    return Move(created).TakeValue();
}

// The whole trace file as text, in memory from `allocator`.
class TraceText final {
public:
    TraceText(Memory::IAllocator& allocator, const char* path) : m_allocator(allocator) {
        Result<Platform::File, Error> opened =
            Platform::File::Open(path, Platform::FileAccess::Read, Platform::FileOpenMode::OpenExisting);
        if (opened.IsError()) {
            return;
        }
        const Platform::File file = Move(opened).TakeValue();
        const Result<u64, Error> size = file.GetSize();
        if (size.IsError() || size.GetValue() == 0) {
            return;
        }
        const auto byteCount = static_cast<usize>(size.GetValue());
        u8* const bytes = static_cast<u8*>(Memory::Allocate(m_allocator, byteCount, 1));
        const Result<usize, Error> read = file.ReadAt(0, Span<u8>(bytes, byteCount));
        if (read.IsOk() && read.GetValue() == byteCount) {
            m_text = static_cast<char*>(Memory::Allocate(m_allocator, byteCount, 1));
            Memory::Copy(m_text, bytes, byteCount);
            m_size = byteCount;
        }
        m_allocator.Deallocate(bytes, byteCount);
    }

    ~TraceText() {
        if (m_text != nullptr) {
            m_allocator.Deallocate(m_text, m_size);
        }
    }

    TraceText(const TraceText&) = delete;
    TraceText& operator=(const TraceText&) = delete;
    TraceText(TraceText&&) = delete;
    TraceText& operator=(TraceText&&) = delete;

    [[nodiscard]] Containers::StringView View() const {
        return m_text == nullptr ? Containers::StringView() : Containers::StringView(m_text, m_size);
    }

    [[nodiscard]] usize Count(Containers::StringView needle) const {
        const Containers::StringView text = View();
        usize count = 0;
        usize from = 0;
        for (;;) {
            const Option<usize> found = text.Find(needle, from);
            if (!found.HasValue()) {
                return count;
            }
            ++count;
            from = found.GetValue() + needle.Size();
        }
    }

    // Brackets and braces balance outside strings, and the text is one object.
    [[nodiscard]] bool IsBalancedJson() const {
        const Containers::StringView text = View();
        i64 depth = 0;
        bool inString = false;
        for (usize index = 0; index < text.Size(); ++index) {
            const char character = text[index];
            if (inString) {
                if (character == '\\') {
                    ++index;
                } else if (character == '"') {
                    inString = false;
                }
                continue;
            }
            if (character == '"') {
                inString = true;
            } else if (character == '{' || character == '[') {
                ++depth;
            } else if (character == '}' || character == ']') {
                --depth;
                if (depth < 0) {
                    return false;
                }
            }
        }
        return depth == 0 && !inString && text.StartsWith("{");
    }

private:
    Memory::IAllocator& m_allocator;
    char* m_text = nullptr;
    usize m_size = 0;
};

void EmitFromAThread(void* argument) {
    static_cast<Atomic<u32>*>(argument)->FetchAdd(1, MemoryOrder::Relaxed);
    ProfileThreadName("Worker \"A\"");
    const ProfileScope scope("Test.ThreadScope");
    ProfileCounter("Test.ThreadCounter", 7.0);
}

} // namespace

NX_TEST(Profiler, RecordsOnlyDuringACapture) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<Profiler> profiler = CreateProfiler(allocator, ProfilerDesc{.eventsPerThread = 64});
    profiler->Install();
    { const ProfileScope ignored("Test.BeforeCapture"); }
    profiler->BeginCapture();
    NX_CHECK(profiler->IsCapturing());
    {
        const ProfileScope outer("Test.Outer");
        const ProfileScope inner("Test.Inner");
        ProfileCounter("Test.Counter", 1.5);
    }
    ProfileFrameMark();
    NX_REQUIRE(profiler->EndCapture(kTracePath).IsOk());
    NX_CHECK(!profiler->IsCapturing());
    { const ProfileScope ignored("Test.AfterCapture"); }
    profiler->Uninstall();

    const ProfilerStatistics statistics = profiler->GetStatistics();
    NX_CHECK(statistics.recorded == 6);
    NX_CHECK(statistics.dropped == 0);
    NX_CHECK(statistics.threads == 1);
    NX_IGNORE_RESULT(Platform::RemoveFile(kTracePath)); // Best effort; the next run truncates it.
}

NX_TEST(Profiler, DropsEventsBeyondTheThreadBuffer) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<Profiler> profiler = CreateProfiler(allocator, ProfilerDesc{.eventsPerThread = 4});
    profiler->Install();
    profiler->BeginCapture();
    for (u32 index = 0; index < 5; ++index) {
        const ProfileScope scope("Test.Many");
    }
    NX_REQUIRE(profiler->EndCapture(kTracePath).IsOk());
    profiler->Uninstall();
    const ProfilerStatistics statistics = profiler->GetStatistics();
    NX_CHECK(statistics.recorded == 4);
    NX_CHECK(statistics.dropped == 6);
    NX_IGNORE_RESULT(Platform::RemoveFile(kTracePath)); // Best effort.
}

NX_TEST(Profiler, ReportsAnUnwritablePath) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<Profiler> profiler = CreateProfiler(allocator, ProfilerDesc{.eventsPerThread = 8});
    profiler->BeginCapture();
    profiler->Write(ProfileEvent{ProfileEventKind::FrameMark, "Frame", {}, 0.0});
    NX_CHECK(profiler->EndCapture("NexylProfileMissingDirectory/trace.json").IsError());
    NX_CHECK(!profiler->IsCapturing());
}

NX_TEST(Profiler, WritesAChromeTraceWithEveryThread) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<Profiler> profiler = CreateProfiler(allocator, ProfilerDesc{.eventsPerThread = 256});
    profiler->Install();
    ProfileThreadName("Main");
    profiler->BeginCapture();
    {
        const ProfileScope scope("Test.Main");
        constexpr u32 kThreads = 3;
        Atomic<u32> started{0};
        Platform::Thread threads[kThreads];
        for (Platform::Thread& thread : threads) {
            Result<Platform::Thread, Error> created =
                Platform::Thread::Create(Platform::ThreadDesc{&EmitFromAThread, &started, "ProfileTest"});
            NX_REQUIRE(created.IsOk());
            thread = Move(created).TakeValue();
        }
        for (Platform::Thread& thread : threads) {
            thread.Join();
        }
        NX_CHECK(started.Load(MemoryOrder::Relaxed) == kThreads);
    }
    ProfileFrameMark();
    NX_REQUIRE(profiler->EndCapture(kTracePath).IsOk());
    profiler->Uninstall();

    const TraceText trace(allocator, kTracePath);
    NX_REQUIRE(!trace.View().IsEmpty());
    NX_CHECK(trace.IsBalancedJson());
    NX_CHECK(trace.View().StartsWith(R"({"displayTimeUnit":"ns","traceEvents":[)"));
    NX_CHECK(trace.Count(R"("ph":"B")") == 4);
    NX_CHECK(trace.Count(R"("ph":"E")") == 4);
    NX_CHECK(trace.Count(R"("ph":"C")") == 3);
    NX_CHECK(trace.Count(R"("ph":"i")") == 1);
    NX_CHECK(trace.Count(R"("name":"Test.ThreadScope")") == 3);
    NX_CHECK(trace.Count(R"("args":{"value":7})") == 3);
    NX_CHECK(trace.Count(R"("name":"thread_name")") == 4);
    // The quotes in the thread name are escaped.
    NX_CHECK(trace.Count(R"("name":"Worker \"A\"")") == 3);
    NX_CHECK(profiler->GetStatistics().threads == 4);
    NX_IGNORE_RESULT(Platform::RemoveFile(kTracePath)); // Best effort.
}
