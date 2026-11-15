#include <Nexyl/Platform/Console.h>

#include <Windows.h>

namespace Nexyl::Platform {

void WriteToConsole(ConsoleStream stream, Span<const char> text) {
    // HANDLE is a typedef for void*; spelling the type out makes the constness unambiguous.
    void* const handle = ::GetStdHandle(stream == ConsoleStream::Error ? STD_ERROR_HANDLE : STD_OUTPUT_HANDLE);
    if (handle == nullptr || handle == INVALID_HANDLE_VALUE) {
        return;
    }

    // WriteFile takes a 32-bit length, so very long text goes out in chunks.
    constexpr usize kMaxChunk = 0x4000'0000;
    usize written = 0;
    while (written < text.Size()) {
        const usize remaining = text.Size() - written;
        const DWORD chunk = static_cast<DWORD>(remaining < kMaxChunk ? remaining : kMaxChunk);
        DWORD count = 0;
        if (::WriteFile(handle, text.Data() + written, chunk, &count, nullptr) == 0 || count == 0) {
            return;
        }
        written += count;
    }
}

} // namespace Nexyl::Platform
