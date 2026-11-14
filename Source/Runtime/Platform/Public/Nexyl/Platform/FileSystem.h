#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>

// File-system operations other than reading and writing. All paths are NUL-terminated UTF-8.
// The names avoid those of Windows macros (CreateDirectory, DeleteFile, RemoveDirectory), which would rename
// the functions inside the Windows implementation.

namespace Nexyl::Platform {

/// Facts about a file or directory.
/// @since 0.1
struct FileInfo {
    /// Size in bytes; zero for directories.
    u64 size = 0;
    /// Time of the last modification, in nanoseconds since 1970-01-01 00:00 UTC. Wall-clock time:
    /// simulation code MUST NOT use it (rule DET-6).
    u64 modificationTime = 0;
    /// True for a directory.
    bool isDirectory = false;
};

/// Returns information about a file or directory.
///
/// @param path The file or directory.
/// @return Its size, modification time and type.
/// @error NotFound Nothing exists at `path`.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<FileInfo, Error> GetFileInfo(const char* path);

/// Deletes a file.
/// @param path The file.
/// @error NotFound The file does not exist.
/// @error WrongFileType The path names a directory.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> RemoveFile(const char* path);

/// Renames or moves a file within one volume, replacing a file that already exists at `to`.
/// @param from The existing file.
/// @param to   The new path.
/// @error NotFound `from` does not exist.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> RenameFile(const char* from, const char* to);

/// Creates one directory. Its parent must exist.
/// @param path The directory to create.
/// @error AlreadyExists Something already exists at `path`.
/// @error NotFound The parent directory does not exist.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> MakeDirectory(const char* path);

/// Deletes an empty directory.
/// @param path The directory.
/// @error DirectoryNotEmpty The directory has entries.
/// @error NotFound The directory does not exist.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> RemoveEmptyDirectory(const char* path);

/// One entry of a directory, as returned by DirectoryIterator.
/// @since 0.1
struct DirectoryEntry {
    /// Longest entry name in UTF-8 bytes. Windows names have up to 255 UTF-16 units, which is at most 765 bytes.
    static constexpr usize kMaxNameLength = 1024;

    /// The entry name, UTF-8, not NUL-terminated. Only the first `nameLength` bytes are valid.
    char name[kMaxNameLength] = {};
    /// Number of valid bytes in `name`.
    usize nameLength = 0;
    /// True for a subdirectory.
    bool isDirectory = false;

    /// Returns the name as a span. @since 0.1
    [[nodiscard]] Span<const char> GetName() const noexcept {
        return Span<const char>(name, nameLength);
    }
};

/// Lists the entries of one directory, without "." and "..", in the order the file system returns them.
///
/// The order is unspecified and differs between platforms: sort the names if order matters (rule DET-7).
/// The iterator is a movable handle and allocates nothing in the engine.
/// @threadsafety compatible
/// @since 0.1
class DirectoryIterator final {
public:
    /// Opens a directory for listing.
    /// @param path The directory.
    /// @return An iterator positioned before the first entry.
    /// @error NotFound The directory does not exist.
    /// @error WrongFileType The path names a file.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API static Result<DirectoryIterator, Error> Open(const char* path);

    /// Creates an iterator over nothing. @since 0.1
    DirectoryIterator() noexcept = default;
    /// Moves an iterator; `other` becomes empty. @since 0.1
    NX_PLATFORM_API DirectoryIterator(DirectoryIterator&& other) noexcept;
    /// Closes this iterator and takes over `other`. @since 0.1
    NX_PLATFORM_API DirectoryIterator& operator=(DirectoryIterator&& other) noexcept;
    /// Closes the directory. @since 0.1
    NX_PLATFORM_API ~DirectoryIterator();

    DirectoryIterator(const DirectoryIterator&) = delete;
    DirectoryIterator& operator=(const DirectoryIterator&) = delete;

    /// Advances to the next entry.
    /// @param outEntry Receives the entry. Untouched when the function returns false or an error.
    /// @return True if an entry was produced, false at the end of the directory.
    /// @error PathTooLong An entry name does not fit DirectoryEntry::kMaxNameLength.
    /// @error IoError Reading the directory failed.
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Result<bool, Error> Next(DirectoryEntry& outEntry);

private:
    void Close() noexcept;

    // Windows find handle or POSIX DIR*; zero when closed.
    usize m_native = 0;
    // Windows reports the first entry when the directory is opened; it waits here until Next is called.
    bool m_hasPending = false;
    DirectoryEntry m_pending;
};

} // namespace Nexyl::Platform
