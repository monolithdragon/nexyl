#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Platform/File.h>

namespace Nexyl::Profile::Detail {

// Writes text to a file through a buffer, remembering the first error.
class TraceWriter final {
public:
    explicit TraceWriter(Platform::File& file) : m_file(file) {}

    TraceWriter(const TraceWriter&) = delete;
    TraceWriter& operator=(const TraceWriter&) = delete;
    TraceWriter(TraceWriter&&) = delete;
    TraceWriter& operator=(TraceWriter&&) = delete;
    ~TraceWriter() = default;

    void Write(Containers::StringView text);

    // Writes the text as the inside of a JSON string: quotes, backslashes and control characters escaped.
    void WriteJsonString(const char* text);

    template <Formattable... T>
    void WriteFormatted(FormatString<TypeIdentity<T>...> format, const T&... args) {
        char line[256] = {};
        const FormatResult result = Format<T...>(line, format, args...);
        Write(Containers::StringView(line, result.size));
    }

    // Writes what is buffered and flushes the file; returns the first error of the whole run.
    [[nodiscard]] Result<void, Error> Finish();

private:
    void WriteBuffer();

    static constexpr usize kBufferSize = usize{64} * 1024;
    Platform::File& m_file;
    u64 m_offset = 0;
    usize m_buffered = 0;
    Result<void, Error> m_status;
    u8 m_buffer[kBufferSize] = {};
};

} // namespace Nexyl::Profile::Detail
