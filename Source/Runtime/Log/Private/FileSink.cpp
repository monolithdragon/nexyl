#include <Nexyl/Log/FileSink.h>

#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/MemoryOperations.h>

namespace Nexyl::Log {

Result<Memory::UniquePtr<FileSink>, Error> FileSink::Create(Memory::IAllocator& allocator, const char* path) {
    NX_TRY_ASSIGN(Platform::File file,
                  Platform::File::Open(path, Platform::FileAccess::Write, Platform::FileOpenMode::CreateOrTruncate));
    return Memory::MakeUnique<FileSink>(allocator, CreateKey{}, Move(file));
}

FileSink::FileSink(CreateKey /*key*/, Platform::File file) : m_file(Move(file)) {}

FileSink::~FileSink() {
    NX_IGNORE_RESULT(WriteBuffer()); // A destructor cannot report the error (rule ERR-5); Flush can.
}

Result<void, Error> FileSink::Write(Span<const LogEntry> entries) {
    char line[kMaxLogLineSize] = {};
    for (const LogEntry& entry : entries) {
        const usize size = FormatLogEntry(entry, line);
        if (m_buffered + size > kFileSinkBufferSize) {
            NX_TRY(WriteBuffer());
        }
        Memory::Copy(m_buffer + m_buffered, line, size);
        m_buffered += size;
    }
    return {};
}

Result<void, Error> FileSink::Flush() {
    NX_TRY(WriteBuffer());
    return m_file.Flush();
}

Result<void, Error> FileSink::WriteBuffer() {
    if (m_buffered == 0) {
        return {};
    }
    // The buffer is emptied even if the write fails, so that one failure does not repeat on every call.
    const usize size = Exchange(m_buffered, usize{0});
    NX_TRY(m_file.WriteAt(m_offset, Span<const u8>(m_buffer, size)));
    m_offset += size;
    return {};
}

} // namespace Nexyl::Log
