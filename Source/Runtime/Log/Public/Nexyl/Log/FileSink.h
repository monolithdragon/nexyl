#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Log/ILogSink.h>
#include <Nexyl/Log/LogEntry.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/File.h>

namespace Nexyl::Log {

/// Writes entries to a file, one line each (FormatLogEntry), through a buffer of kFileSinkBufferSize bytes.
///
/// The file is created or truncated when the sink is created. The buffer is written when it fills and on
/// Flush; the Logger flushes after a fatal record and when it is destroyed.
///
/// ```cpp
/// NX_TRY_ASSIGN(UniquePtr<FileSink> file, FileSink::Create(logMemory, "Saved/Logs/Game.log"));
/// NX_TRY(logger->AddSink(*file));
/// ```
/// @threadsafety compatible
/// @since 0.1
class FileSink final : public ILogSink {
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    /// The size of the write buffer in bytes.
    /// @since 0.1
    static constexpr usize kFileSinkBufferSize = usize{16} * 1024;

    /// Creates or truncates the file and creates a sink that writes to it.
    /// @param allocator Supplies the sink. It must outlive the sink.
    /// @param path      UTF-8 path of the file. Borrowed for the duration of the call.
    /// @return The sink.
    /// @error Any Platform error of File::Open, for example NotFound for a missing directory or AccessDenied.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_LOG_API static Result<Memory::UniquePtr<FileSink>, Error> Create(Memory::IAllocator& allocator,
                                                                                      const char* path);

    /// Takes ownership of an open file. Only Create can call it.
    /// @param key  Restricts construction to Create.
    /// @param file The open file.
    /// @since 0.1
    NX_LOG_API FileSink(CreateKey key, Platform::File file);

    /// Writes what is buffered, ignoring errors (rule ERR-5), and closes the file. Call Flush first to see
    /// errors. @since 0.1
    NX_LOG_API ~FileSink();

    FileSink(const FileSink&) = delete;
    FileSink& operator=(const FileSink&) = delete;
    FileSink(FileSink&&) = delete;
    FileSink& operator=(FileSink&&) = delete;

    /// See ILogSink::Write.
    /// @error Any Platform error of File::WriteAt when the buffer is written.
    /// @since 0.1
    NX_LOG_API Result<void, Error> Write(Span<const LogEntry> entries) override;

    /// See ILogSink::Flush. Writes the buffer and flushes the file to the device.
    /// @error Any Platform error of File::WriteAt or File::Flush.
    /// @since 0.1
    NX_LOG_API Result<void, Error> Flush() override;

private:
    Result<void, Error> WriteBuffer();

    Platform::File m_file;
    u64 m_offset = 0;
    usize m_buffered = 0;
    u8 m_buffer[kFileSinkBufferSize] = {};
};

} // namespace Nexyl::Log
