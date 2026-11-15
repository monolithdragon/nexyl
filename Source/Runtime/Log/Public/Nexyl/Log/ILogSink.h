#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Log/LogEntry.h>

namespace Nexyl::Log {

/// A destination for log entries: the console, a file, a network connection, an in-game console.
///
/// A Logger calls its sinks from one thread at a time, so a sink needs no synchronisation of its own. Entries
/// arrive in batches, in sequence order. A sink must not log: the Logger drops records written while a sink runs
/// on the Logger's thread.
///
/// ```cpp
/// class CountingSink final : public ILogSink {
/// public:
///     Result<void, Error> Write(Span<const LogEntry> entries) override { m_count += entries.Size(); return {}; }
///     Result<void, Error> Flush() override { return {}; }
/// private:
///     usize m_count = 0;
/// };
/// ```
/// @threadsafety compatible
/// @since 0.1
class ILogSink {
public:
    /// Receives a batch of entries.
    /// @param entries The entries, in sequence order. Borrowed, with their text, for the duration of the call.
    /// @return Success, or the error that stopped the sink from recording them.
    /// @error Any The sink's own failure, for example a failed file write. The Logger counts it and goes on.
    /// @since 0.1
    virtual Result<void, Error> Write(Span<const LogEntry> entries) = 0;

    /// Makes everything written so far durable or visible.
    /// @return Success, or the sink's error.
    /// @error Any The sink's own failure. The Logger counts it and goes on.
    /// @since 0.1
    virtual Result<void, Error> Flush() = 0;

protected:
    ILogSink() = default;
    ~ILogSink() = default;
    ILogSink(const ILogSink&) = default;
    ILogSink& operator=(const ILogSink&) = default;
    ILogSink(ILogSink&&) = default;
    ILogSink& operator=(ILogSink&&) = default;
};

} // namespace Nexyl::Log
