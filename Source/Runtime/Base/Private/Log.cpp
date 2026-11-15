#include <Nexyl/Base/Log.h>

namespace Nexyl::Detail {

void WriteFormattedLog(LogLevel level, const char* text, usize size, Span<const FormatArg> args,
                       const SourceLocation& location) {
    char buffer[kMaxLogMessageSize] = {};
    FormatWriter writer(buffer);
    FormatArgs(writer, text, size, args);
    usize length = writer.Size();
    if (writer.IsTruncated()) {
        constexpr usize kMarkerSize = 3;
        length = kMaxLogMessageSize - kMarkerSize;
        // Do not leave half of a UTF-8 sequence before the marker: step back over continuation bytes and the
        // lead byte they belong to.
        while (length > 0 && (static_cast<u8>(buffer[length]) & 0xC0U) == 0x80U) {
            --length;
        }
        buffer[length++] = '.';
        buffer[length++] = '.';
        buffer[length++] = '.';
    }
    WriteLog(LogRecord{level, buffer, length, location});
}

} // namespace Nexyl::Detail
