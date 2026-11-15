#include <Nexyl/Log/ConsoleSink.h>

#include <Nexyl/Platform/Console.h>

namespace Nexyl::Log {

// Defined here, not in the header, so that only the module refers to its virtual table (hidden visibility).
ConsoleSink::ConsoleSink() = default;

Result<void, Error> ConsoleSink::Write(Span<const LogEntry> entries) {
    char line[kMaxLogLineSize] = {};
    for (const LogEntry& entry : entries) {
        const usize size = FormatLogEntry(entry, line);
        const Platform::ConsoleStream stream =
            entry.level >= LogLevel::Error ? Platform::ConsoleStream::Error : Platform::ConsoleStream::Output;
        Platform::WriteToConsole(stream, Span<const char>(line, size));
    }
    return {};
}

Result<void, Error> ConsoleSink::Flush() {
    return {};
}

} // namespace Nexyl::Log
