#include "TraceWriter.h"

#include <Nexyl/Base/Span.h>

namespace Nexyl::Profile::Detail {

void TraceWriter::Write(Containers::StringView text) {
    for (const char character : text) {
        if (m_buffered == kBufferSize) {
            WriteBuffer();
        }
        m_buffer[m_buffered++] = static_cast<u8>(character);
    }
}

void TraceWriter::WriteJsonString(const char* text) {
    constexpr char kHexDigits[] = "0123456789abcdef";
    for (const char* cursor = text; *cursor != '\0'; ++cursor) {
        const auto byte = static_cast<u8>(*cursor);
        if (*cursor == '"' || *cursor == '\\') {
            const char escaped[2] = {'\\', *cursor};
            Write(Containers::StringView(escaped, 2));
        } else if (byte < 0x20U) {
            const char escaped[6] = {'\\', 'u', '0', '0', kHexDigits[byte >> 4U], kHexDigits[byte & 0xFU]};
            Write(Containers::StringView(escaped, 6));
        } else {
            Write(Containers::StringView(cursor, 1));
        }
    }
}

Result<void, Error> TraceWriter::Finish() {
    WriteBuffer();
    if (m_status.IsOk()) {
        m_status = m_file.Flush();
    }
    return m_status;
}

void TraceWriter::WriteBuffer() {
    if (m_buffered == 0) {
        return;
    }
    if (m_status.IsOk()) {
        m_status = m_file.WriteAt(m_offset, Span<const u8>(m_buffer, m_buffered));
        m_offset += m_buffered;
    }
    m_buffered = 0;
}

} // namespace Nexyl::Profile::Detail
