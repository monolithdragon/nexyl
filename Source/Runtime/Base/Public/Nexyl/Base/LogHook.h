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
    /// Not a record level: as the minimum level, it disables logging.
    Off,
};

/// One message to log. The text is UTF-8 and is not NUL-terminated.
/// @threadsafety safe
/// @since 0.1
struct LogRecord {
    /// Severity. Never `LogLevel::Off`.
    LogLevel level = LogLevel::Info;
    /// Message text. Borrowed for the duration of the sink call.
    const char* text = "";
    /// Length of `text` in bytes.
    usize length = 0;
    /// Where the record was written.
    SourceLocation location;
};

/// The function that receives log records, with the context it needs.
///
/// The installer owns the binding. It must stay valid while it is installed; SetLogSink returns only after every
/// call into the binding it replaced has finished, so the previous owner may then destroy it.
/// @threadsafety safe
/// @since 0.1
struct LogSinkBinding {
    /// Receives one record. It may be called from any thread, concurrently, and must not call SetLogSink.
    void (*write)(void* context, const LogRecord& record) = nullptr;
    /// Passed unchanged to `write`.
    void* context = nullptr;
};

/// Installs the binding that receives log records.
///
/// Base only forwards records; formatting, filtering and output belong to the Log module, which installs its
/// binding at startup (docs/NEXYL_ARCHITECTURE.md, section 4.4). Without a binding, records are dropped.
///
/// The function returns after every WriteLog call that may still use the previous binding has returned, so the
/// caller may destroy the previous binding and its context afterwards.
///
/// @param binding The new binding, or nullptr to remove the current one. It must stay valid until it is replaced.
/// @return The previously installed binding, or nullptr.
/// @pre `binding` is nullptr or has a `write` function.
/// @pre Not called from inside a binding's `write` function.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API const LogSinkBinding* SetLogSink(const LogSinkBinding* binding);

/// Sets the lowest level that is passed to the binding. Records below it are dropped before they are formatted.
///
/// The default is `LogLevel::Trace`: everything is passed on.
///
/// @param level The lowest level to keep; `LogLevel::Off` drops every record.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API void SetLogMinimumLevel(LogLevel level);

/// Returns the lowest level that is passed to the binding.
/// @return The level set by SetLogMinimumLevel.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API LogLevel GetLogMinimumLevel();

/// Tells whether a record of `level` would reach a binding. Callers check it before formatting a message.
/// @param level The level of the record. Not `LogLevel::Off`.
/// @return True if a binding is installed and `level` is at least the minimum level.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API bool IsLogEnabled(LogLevel level);

/// Passes a record to the installed binding. Does nothing without a binding or below the minimum level.
///
/// @param record The record. Borrowed for the duration of the call.
/// @pre `record.level` is not `LogLevel::Off`.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API void WriteLog(const LogRecord& record);

} // namespace Nexyl
