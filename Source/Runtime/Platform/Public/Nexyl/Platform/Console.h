#pragma once

#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// The standard streams of the process.
/// @since 0.1
enum class ConsoleStream : u8 {
    /// Standard output.
    Output,
    /// Standard error.
    Error,
};

/// Writes UTF-8 text to a standard stream of the process.
///
/// The bytes are written unbuffered and unmodified, so redirection to a file or pipe works. The call is
/// best-effort: if the stream is closed or the write fails, the remaining text is dropped, because a
/// console failure has nowhere to be reported. The function does not allocate.
///
/// @param stream The stream to write to.
/// @param text   The bytes to write. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
NX_PLATFORM_API void WriteToConsole(ConsoleStream stream, Span<const char> text);

} // namespace Nexyl::Platform
