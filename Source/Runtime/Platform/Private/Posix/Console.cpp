#include <Nexyl/Platform/Console.h>

#include <errno.h>
#include <unistd.h>

namespace Nexyl::Platform {

void WriteToConsole(ConsoleStream stream, Span<const char> text) {
    const int descriptor = stream == ConsoleStream::Error ? STDERR_FILENO : STDOUT_FILENO;

    usize written = 0;
    while (written < text.Size()) {
        const ssize_t count = ::write(descriptor, text.Data() + written, text.Size() - written);
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (count == 0) {
            return;
        }
        written += static_cast<usize>(count);
    }
}

} // namespace Nexyl::Platform
