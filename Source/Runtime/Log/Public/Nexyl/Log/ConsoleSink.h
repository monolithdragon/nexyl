#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Log/ILogSink.h>
#include <Nexyl/Log/LogEntry.h>

namespace Nexyl::Log {

/// Writes entries to the process console, one line each (FormatLogEntry): errors and fatal records to the error
/// stream, everything else to the output stream.
///
/// ```cpp
/// ConsoleSink console;
/// NX_TRY(logger->AddSink(console));
/// ```
/// @threadsafety compatible
/// @since 0.1
class ConsoleSink final : public ILogSink {
public:
    /// Creates the sink. It holds no resources. @since 0.1
    NX_LOG_API ConsoleSink();
    /// Destroys the sink. @since 0.1
    ~ConsoleSink() = default;

    ConsoleSink(const ConsoleSink&) = delete;
    ConsoleSink& operator=(const ConsoleSink&) = delete;
    ConsoleSink(ConsoleSink&&) = delete;
    ConsoleSink& operator=(ConsoleSink&&) = delete;

    /// See ILogSink::Write. Never fails. @since 0.1
    NX_LOG_API Result<void, Error> Write(Span<const LogEntry> entries) override;
    /// See ILogSink::Flush. The console is unbuffered; never fails. @since 0.1
    NX_LOG_API Result<void, Error> Flush() override;
};

} // namespace Nexyl::Log
