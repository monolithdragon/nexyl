#pragma once

#include <Nexyl/Base/Log.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/StringView.h>

namespace Nexyl::Log {

/// One log record as a sink receives it: the record plus what the Logger adds.
/// @threadsafety safe
/// @since 0.1
struct LogEntry {
    /// Severity.
    LogLevel level = LogLevel::Info;
    /// The formatted message, UTF-8. Valid only for the duration of the sink call.
    Containers::StringView text;
    /// Where the record was written.
    SourceLocation location;
    /// Monotonic time of the record in nanoseconds since the Logger was created.
    u64 timeNanoseconds = 0;
    /// Platform identifier of the thread that wrote the record (Platform::Thread::GetCurrentId).
    u64 threadId = 0;
    /// Position of the record among all records the Logger received. A gap means records were dropped.
    u64 sequence = 0;
};

/// The largest line FormatLogEntry writes: the message plus the time, level and location around it.
/// @since 0.1
inline constexpr usize kMaxLogLineSize = kMaxLogMessageSize + 256;

/// Returns the fixed-width tag of a level: `TRACE`, `DEBUG`, `INFO `, `WARN `, `ERROR` or `FATAL`.
/// @param level The level. Not LogLevel::Off.
/// @return Five characters, not NUL-terminated as far as the view is concerned.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_LOG_API Containers::StringView GetLogLevelTag(LogLevel level);

/// Formats an entry as one line of text, the format of the built-in sinks.
///
/// The line is `[    12.345678] WARN  message`, followed for warnings and worse by ` (File.cpp:42)`, and ends
/// with a newline. The time is in seconds since the Logger was created.
///
/// @param entry  The entry. Borrowed for the duration of the call.
/// @param buffer Receives the line. At least kMaxLogLineSize bytes guarantee that nothing is cut.
/// @return The number of bytes written.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_LOG_API usize FormatLogEntry(const LogEntry& entry, Span<char> buffer);

} // namespace Nexyl::Log
