#include <Nexyl/Platform/FileSystem.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Utility.h>

#include "Posix/PosixSupport.h"

#include <dirent.h>
#include <fcntl.h>
// NX-DEVIATION(STL-3): POSIX declares rename() only in <stdio.h>; nothing else from the header is used.
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Nexyl::Platform {

namespace {

constexpr u64 kNanosecondsPerSecond = 1'000'000'000;

DIR* AsDirectory(usize native) {
    return BitCast<DIR*>(native);
}

bool IsDotOrDotDot(const char* name) {
    return name[0] == '.' && (name[1] == '\0' || (name[1] == '.' && name[2] == '\0'));
}

} // namespace

Result<FileInfo, Error> GetFileInfo(const char* path) {
    struct stat status = {};
    if (::stat(path, &status) != 0) {
        return Detail::LastPosixError();
    }

#if NX_PLATFORM_APPLE
    const timespec& modified = status.st_mtimespec;
#else
    const timespec& modified = status.st_mtim;
#endif

    FileInfo info;
    info.isDirectory = S_ISDIR(status.st_mode);
    info.size = info.isDirectory ? 0 : static_cast<u64>(status.st_size);
    info.modificationTime =
        static_cast<u64>(modified.tv_sec) * kNanosecondsPerSecond + static_cast<u64>(modified.tv_nsec);
    return info;
}

Result<void, Error> RemoveFile(const char* path) {
    if (::unlink(path) != 0) {
        // Linux reports a directory as EISDIR, macOS as EPERM; report both as WrongFileType.
        const int code = errno;
        struct stat status = {};
        if (::stat(path, &status) == 0 && S_ISDIR(status.st_mode)) {
            return Error(PlatformError::WrongFileType, static_cast<u32>(code));
        }
        return Detail::MakePosixError(code);
    }
    return {};
}

Result<void, Error> RenameFile(const char* from, const char* to) {
    if (::rename(from, to) != 0) {
        return Detail::LastPosixError();
    }
    return {};
}

Result<void, Error> MakeDirectory(const char* path) {
    constexpr mode_t kNewDirectoryPermissions = 0755;
    if (::mkdir(path, kNewDirectoryPermissions) != 0) {
        return Detail::LastPosixError();
    }
    return {};
}

Result<void, Error> RemoveEmptyDirectory(const char* path) {
    if (::rmdir(path) != 0) {
        const int code = errno;
        // POSIX allows EEXIST for a non-empty directory as well.
        if (code == EEXIST) {
            return Error(PlatformError::DirectoryNotEmpty, static_cast<u32>(code));
        }
        return Detail::MakePosixError(code);
    }
    return {};
}

Result<DirectoryIterator, Error> DirectoryIterator::Open(const char* path) {
    DIR* const directory = ::opendir(path);
    if (directory == nullptr) {
        return Detail::LastPosixError();
    }
    DirectoryIterator iterator;
    iterator.m_native = BitCast<usize>(directory);
    return iterator;
}

DirectoryIterator::DirectoryIterator(DirectoryIterator&& other) noexcept
    : m_native(Exchange(other.m_native, usize{0})), m_hasPending(Exchange(other.m_hasPending, false)),
      m_pending(other.m_pending) {}

DirectoryIterator& DirectoryIterator::operator=(DirectoryIterator&& other) noexcept {
    if (this != &other) {
        Close();
        m_native = Exchange(other.m_native, usize{0});
        m_hasPending = Exchange(other.m_hasPending, false);
        m_pending = other.m_pending;
    }
    return *this;
}

DirectoryIterator::~DirectoryIterator() {
    Close();
}

void DirectoryIterator::Close() noexcept {
    if (m_native != 0) {
        NX_VERIFY(::closedir(AsDirectory(m_native)) == 0);
        m_native = 0;
    }
    m_hasPending = false;
}

Result<bool, Error> DirectoryIterator::Next(DirectoryEntry& outEntry) {
    if (m_native == 0) {
        return false;
    }
    DIR* const directory = AsDirectory(m_native);
    while (true) {
        // readdir signals both the end and an error with null; only an error changes errno.
        errno = 0;
        const dirent* const entry = ::readdir(directory);
        if (entry == nullptr) {
            if (errno != 0) {
                return Detail::LastPosixError();
            }
            // Release the directory as soon as the listing ends; later calls keep returning false.
            Close();
            return false;
        }
        if (IsDotOrDotDot(entry->d_name)) {
            continue;
        }

        usize length = 0;
        while (entry->d_name[length] != '\0') {
            if (length == DirectoryEntry::kMaxNameLength) {
                return Error(PlatformError::PathTooLong);
            }
            outEntry.name[length] = entry->d_name[length];
            ++length;
        }
        outEntry.nameLength = length;

        // Some file systems do not report the type in the entry; ask for it then.
        if (entry->d_type == DT_UNKNOWN) {
            const int descriptor = ::dirfd(directory);
            if (descriptor < 0) {
                return Detail::LastPosixError();
            }
            struct stat status = {};
            if (::fstatat(descriptor, entry->d_name, &status, 0) != 0) {
                return Detail::LastPosixError();
            }
            outEntry.isDirectory = S_ISDIR(status.st_mode);
        } else {
            outEntry.isDirectory = entry->d_type == DT_DIR;
        }
        return true;
    }
}

} // namespace Nexyl::Platform
