#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Log/ConsoleSink.h>
#include <Nexyl/Log/FileSink.h>
#include <Nexyl/Log/LogEntry.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/File.h>
#include <Nexyl/Platform/FileSystem.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Log;

namespace {

[[nodiscard]] LogEntry MakeEntry(LogLevel level, Containers::StringView text, u64 timeNanoseconds) {
    LogEntry entry;
    entry.level = level;
    entry.text = text;
    entry.location = SourceLocation{"C:/Engine/Source/Physics/World.cpp", "Step", 42};
    entry.timeNanoseconds = timeNanoseconds;
    return entry;
}

[[nodiscard]] Containers::StringView Line(const char* buffer, usize size) {
    return Containers::StringView(buffer, size);
}

} // namespace

NX_TEST(LogEntry, FormatsOneLine) {
    char buffer[kMaxLogLineSize] = {};
    const usize info = FormatLogEntry(MakeEntry(LogLevel::Info, "ready", 12'345'678'901ULL), buffer);
    NX_CHECK(Line(buffer, info) == Containers::StringView("[    12.345678] INFO  ready\n"));

    const usize warning = FormatLogEntry(MakeEntry(LogLevel::Warning, "slow step", 1'000ULL), buffer);
    NX_CHECK(Line(buffer, warning) == Containers::StringView("[     0.000001] WARN  slow step (World.cpp:42)\n"));
}

NX_TEST(LogEntry, CutLinesStillEndWithANewline) {
    char buffer[16] = {};
    const usize size = FormatLogEntry(MakeEntry(LogLevel::Info, "a long message", 0), buffer);
    NX_REQUIRE(size == sizeof(buffer));
    NX_CHECK(buffer[size - 1] == '\n');
}

NX_TEST(LogEntry, NamesEveryLevel) {
    NX_CHECK(GetLogLevelTag(LogLevel::Trace) == Containers::StringView("TRACE"));
    NX_CHECK(GetLogLevelTag(LogLevel::Debug) == Containers::StringView("DEBUG"));
    NX_CHECK(GetLogLevelTag(LogLevel::Error) == Containers::StringView("ERROR"));
    NX_CHECK(GetLogLevelTag(LogLevel::Fatal) == Containers::StringView("FATAL"));
}

NX_TEST(ConsoleSink, WritesWithoutFailing) {
    ConsoleSink sink;
    const LogEntry entries[] = {MakeEntry(LogLevel::Info, "console sink test line", 0)};
    NX_CHECK(sink.Write(entries).IsOk());
    NX_CHECK(sink.Flush().IsOk());
}

NX_TEST(FileSink, WritesLinesToTheFile) {
    const char* const path = "NexylLogFileSinkTest.log";
    Memory::GuardAllocator allocator;
    {
        Result<Memory::UniquePtr<FileSink>, Error> created = FileSink::Create(allocator, path);
        NX_REQUIRE(created.IsOk());
        const Memory::UniquePtr<FileSink> sink = Move(created).TakeValue();
        const LogEntry first[] = {MakeEntry(LogLevel::Info, "one", 0), MakeEntry(LogLevel::Info, "two", 0)};
        NX_CHECK(sink->Write(first).IsOk());
        NX_CHECK(sink->Flush().IsOk());
        const LogEntry second[] = {MakeEntry(LogLevel::Info, "three", 0)};
        NX_CHECK(sink->Write(second).IsOk());
        // The destructor writes what is still buffered.
    }

    Result<Platform::File, Error> opened =
        Platform::File::Open(path, Platform::FileAccess::Read, Platform::FileOpenMode::OpenExisting);
    NX_REQUIRE(opened.IsOk());
    const Platform::File file = Move(opened).TakeValue();
    u8 bytes[256] = {};
    const Result<usize, Error> read = file.ReadAt(0, bytes);
    NX_REQUIRE(read.IsOk());
    char text[256] = {};
    for (usize index = 0; index < read.GetValue(); ++index) {
        text[index] = static_cast<char>(bytes[index]);
    }
    NX_CHECK(Line(text, read.GetValue()) == Containers::StringView("[     0.000000] INFO  one\n"
                                                                   "[     0.000000] INFO  two\n"
                                                                   "[     0.000000] INFO  three\n"));
    NX_IGNORE_RESULT(Platform::RemoveFile(path)); // Best effort; a leftover file is truncated by the next run.
}

NX_TEST(FileSink, ReportsAMissingDirectory) {
    Memory::GuardAllocator allocator;
    const Result<Memory::UniquePtr<FileSink>, Error> created =
        FileSink::Create(allocator, "NexylLogMissingDirectory/test.log");
    NX_CHECK(created.IsError());
}
