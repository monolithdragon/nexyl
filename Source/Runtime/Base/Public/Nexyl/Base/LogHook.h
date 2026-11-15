#pragma once

#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// Severity of a log record.
/// @since 0.1
enum class LogLevel : u8 {
    /// Very detailed diagnostics, normally disabled.
    Trace,
    /// Diagnostics for developers.
    Debug,
    /// Normal operation worth recording.
    Info,
    /// Something unexpected that the engine handled.
    Warning,
    /// An operation failed.
    Error,
    /// The process cannot continue.
    Fatal,
};

/// One message to log. The text is UTF-8 and is not NUL-terminated.
/// @threadsafety safe
/// @since 0.1
struct LogRecord {
    /// Severity.
    LogLevel level = LogLevel::Info;
    /// Message text. Borrowed for the duration of the sink call.
    const char* text = "";
    /// Length of `text` in bytes.
    usize length = 0;
    /// Where the record was written.
    SourceLocation location;
};

/// Function that receives log records. It may be called from any thread, concurrently.
/// @since 0.1
using LogSink = void (*)(const LogRecord& record);

/// Installs the function that receives log records.
///
/// Base only forwards records; formatting, filtering and output belong to the Log module, which installs
/// its sink at startup (docs/NEXYL_ARCHITECTURE.md, section 4.4). Without a sink, records are dropped.
///
/// @param sink The new sink, or nullptr to remove the current one.
/// @return The previously installed sink, or nullptr.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API LogSink SetLogSink(LogSink sink);

/// Passes a record to the installed sink. Does nothing without a sink.
///
/// @param record The record. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API void WriteLog(const LogRecord& record);

} // namespace Nexyl
