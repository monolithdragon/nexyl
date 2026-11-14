#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// What a file is opened for.
/// @since 0.1
enum class FileAccess : u8 {
    /// Reading only.
    Read,
    /// Writing only.
    Write,
    /// Reading and writing.
    ReadWrite,
};

/// What File::Open does depending on whether the file exists.
/// @since 0.1
enum class FileOpenMode : u8 {
    /// Opens an existing file; fails with NotFound otherwise.
    OpenExisting,
    /// Creates a new file; fails with AlreadyExists if it exists.
    CreateNew,
    /// Creates the file, or truncates it to zero length if it exists.
    CreateOrTruncate,
    /// Opens the file, or creates it empty if it does not exist.
    OpenOrCreate,
};

/// An open file with positional reads and writes.
///
/// There is no file position: every read and write names its offset, so one File can serve concurrent readers
/// and asynchronous I/O later. For the same reason the I/O functions are `const`: `const` means the handle does
/// not change, not that the file contents cannot. Paths are NUL-terminated UTF-8. Other processes may read, write,
/// rename and delete the file while it is open. A File is a movable handle and closes itself when destroyed; use Close
/// to observe errors on closing (rule ERR-5).
/// @threadsafety compatible
/// @since 0.1
class File final {
public:
    /// Opens or creates a file.
    ///
    /// @param path   The file, NUL-terminated UTF-8.
    /// @param access What the file is opened for.
    /// @param mode   What to do depending on whether the file exists.
    /// @return The open file.
    /// @error NotFound The file or a parent directory does not exist.
    /// @error AlreadyExists The file exists and `mode` is CreateNew.
    /// @error WrongFileType The path names a directory.
    /// @error AccessDenied The operating system denied access.
    /// @error PathTooLong, InvalidPath The path cannot be used.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API static Result<File, Error> Open(const char* path, FileAccess access,
                                                                  FileOpenMode mode);

    /// Creates a handle that refers to no file. @since 0.1
    File() noexcept = default;
    /// Moves a handle; `other` becomes empty. @since 0.1
    NX_PLATFORM_API File(File&& other) noexcept;
    /// Closes this file and takes over `other`. @since 0.1
    NX_PLATFORM_API File& operator=(File&& other) noexcept;
    /// Closes the file, ignoring errors. @since 0.1
    NX_PLATFORM_API ~File();

    File(const File&) = delete;
    File& operator=(const File&) = delete;

    /// Checks whether the handle refers to an open file. @since 0.1
    [[nodiscard]] bool IsOpen() const noexcept {
        return m_native != kInvalid;
    }

    /// Reads bytes starting at `offset`.
    ///
    /// @param offset Position of the first byte to read.
    /// @param buffer Receives the bytes.
    /// @return The number of bytes read. It is less than the buffer size only when the end of the file was reached.
    /// @error IoError The device reported a failure.
    /// @pre IsOpen(), and the file was opened for reading.
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<usize, Error> ReadAt(u64 offset, Span<u8> buffer) const;

    /// Writes all of `data` starting at `offset`, extending the file if needed.
    ///
    /// @param offset Position of the first byte to write.
    /// @param data   The bytes to write.
    /// @error IoError The device reported a failure or is full.
    /// @pre IsOpen(), and the file was opened for writing.
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<void, Error> WriteAt(u64 offset, Span<const u8> data) const;

    /// Returns the size of the file in bytes.
    /// @return The size.
    /// @pre IsOpen().
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<u64, Error> GetSize() const;

    /// Truncates or extends the file. Bytes added by extending read as zero.
    /// @param size The new size in bytes.
    /// @pre IsOpen(), and the file was opened for writing.
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<void, Error> SetSize(u64 size) const;

    /// Writes buffered data through to the storage device.
    /// @pre IsOpen().
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<void, Error> Flush() const;

    /// Closes the file and reports any error the operating system returns. The handle becomes empty either way.
    /// @pre IsOpen().
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<void, Error> Close();

private:
    // Windows HANDLE or POSIX file descriptor; both use -1 for "no file".
    static constexpr isize kInvalid = -1;
    isize m_native = kInvalid;
};

} // namespace Nexyl::Platform
