#include <Nexyl/Log/LogEntry.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Format.h>

namespace Nexyl::Log {

namespace {

// The part of a path after its last separator.
[[nodiscard]] Containers::StringView FileName(const char* path) {
    const Containers::StringView full = Containers::StringView::FromCString(path);
    usize start = 0;
    for (usize index = 0; index < full.Size(); ++index) {
        if (full[index] == '/' || full[index] == '\\') {
            start = index + 1;
        }
    }
    return full.Substring(start);
}

} // namespace

Containers::StringView GetLogLevelTag(LogLevel level) {
    switch (level) {
        case LogLevel::Trace:
            return "TRACE";
        case LogLevel::Debug:
            return "DEBUG";
        case LogLevel::Info:
            return "INFO ";
        case LogLevel::Warning:
            return "WARN ";
        case LogLevel::Error:
            return "ERROR";
        case LogLevel::Fatal:
            return "FATAL";
        case LogLevel::Off:
            break;
    }
    NX_ASSERT(false, "LogLevel::Off is not a record level.");
    return "?????";
}

usize FormatLogEntry(const LogEntry& entry, Span<char> buffer) {
    constexpr u64 kNanosecondsPerSecond = 1'000'000'000;
    constexpr u64 kNanosecondsPerMicrosecond = 1'000;
    const u64 seconds = entry.timeNanoseconds / kNanosecondsPerSecond;
    const u64 microseconds = (entry.timeNanoseconds % kNanosecondsPerSecond) / kNanosecondsPerMicrosecond;

    FormatWriter writer(buffer);
    FormatTo(writer, "[{:>6}.{:06}] {} {}", seconds, microseconds, GetLogLevelTag(entry.level), entry.text);
    if (entry.level >= LogLevel::Warning) {
        FormatTo(writer, " ({}:{})", FileName(entry.location.file), entry.location.line);
    }
    writer.Write("\n", 1);
    const usize size = writer.Size();
    if (writer.IsTruncated() && size > 0) {
        // A cut line still ends with its newline.
        buffer[size - 1] = '\n';
    }
    return size;
}

} // namespace Nexyl::Log
