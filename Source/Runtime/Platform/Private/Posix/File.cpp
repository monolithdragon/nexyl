#include <Nexyl/Platform/File.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Utility.h>

#include "Posix/PosixSupport.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace Nexyl::Platform {

namespace {

int AsDescriptor(isize native) {
    return static_cast<int>(native);
}

} // namespace

Result<File, Error> File::Open(const char* path, FileAccess access, FileOpenMode mode) {
    // The O_ flags are signed int constants; they are combined as unsigned bits and converted back for open().
    const auto bit = [](int flag) { return static_cast<u32>(flag); };
    u32 flags = bit(O_CLOEXEC);
    switch (access) {
        case FileAccess::Read:
            flags |= bit(O_RDONLY);
            break;
        case FileAccess::Write:
            flags |= bit(O_WRONLY);
            break;
        case FileAccess::ReadWrite:
            flags |= bit(O_RDWR);
            break;
    }
    switch (mode) {
        case FileOpenMode::OpenExisting:
            break;
        case FileOpenMode::CreateNew:
            flags |= bit(O_CREAT) | bit(O_EXCL);
            break;
        case FileOpenMode::CreateOrTruncate:
            flags |= bit(O_CREAT) | bit(O_TRUNC);
            break;
        case FileOpenMode::OpenOrCreate:
            flags |= bit(O_CREAT);
            break;
    }

    constexpr mode_t kNewFilePermissions = 0644;
    int descriptor = -1;
    do {
        descriptor = ::open(path, static_cast<int>(flags), kNewFilePermissions);
    } while (descriptor < 0 && errno == EINTR);
    if (descriptor < 0) {
        return Detail::LastPosixError();
    }

    File file;
    file.m_native = descriptor;

    // POSIX opens a directory read-only without complaint; report it the same way on every platform.
    struct stat status = {};
    if (::fstat(descriptor, &status) != 0) {
        return Detail::LastPosixError();
    }
    if (S_ISDIR(status.st_mode)) {
        return Error(PlatformError::WrongFileType, EISDIR);
    }
    return file;
}

File::File(File&& other) noexcept : m_native(Exchange(other.m_native, kInvalid)) {}

File& File::operator=(File&& other) noexcept {
    if (this != &other) {
        if (IsOpen()) {
            NX_IGNORE_RESULT(Close()); // A move assignment has no way to report a close error.
        }
        m_native = Exchange(other.m_native, kInvalid);
    }
    return *this;
}

File::~File() {
    if (IsOpen()) {
        NX_IGNORE_RESULT(Close()); // Destructors do not report errors (rule ERR-5); call Close to see them.
    }
}

Result<usize, Error> File::ReadAt(u64 offset, Span<u8> buffer) const {
    NX_ASSERT(IsOpen(), "File::ReadAt on a closed file.");
    usize total = 0;
    while (total < buffer.Size()) {
        const ssize_t count = ::pread(AsDescriptor(m_native), buffer.Data() + total, buffer.Size() - total,
                                      static_cast<off_t>(offset + total));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Detail::LastPosixError();
        }
        if (count == 0) {
            break;
        }
        total += static_cast<usize>(count);
    }
    return total;
}

Result<void, Error> File::WriteAt(u64 offset, Span<const u8> data) const {
    NX_ASSERT(IsOpen(), "File::WriteAt on a closed file.");
    usize total = 0;
    while (total < data.Size()) {
        const ssize_t count = ::pwrite(AsDescriptor(m_native), data.Data() + total, data.Size() - total,
                                       static_cast<off_t>(offset + total));
        if (count < 0) {
            if (errno == EINTR) {
                continue;
            }
            return Detail::LastPosixError();
        }
        if (count == 0) {
            return Error(PlatformError::IoError);
        }
        total += static_cast<usize>(count);
    }
    return {};
}

Result<u64, Error> File::GetSize() const {
    NX_ASSERT(IsOpen(), "File::GetSize on a closed file.");
    struct stat status = {};
    if (::fstat(AsDescriptor(m_native), &status) != 0) {
        return Detail::LastPosixError();
    }
    return static_cast<u64>(status.st_size);
}

Result<void, Error> File::SetSize(u64 size) const {
    NX_ASSERT(IsOpen(), "File::SetSize on a closed file.");
    int result = 0;
    do {
        result = ::ftruncate(AsDescriptor(m_native), static_cast<off_t>(size));
    } while (result != 0 && errno == EINTR);
    if (result != 0) {
        return Detail::LastPosixError();
    }
    return {};
}

Result<void, Error> File::Flush() const {
    NX_ASSERT(IsOpen(), "File::Flush on a closed file.");
#if NX_PLATFORM_APPLE
    // On Apple systems fsync only reaches the drive's cache; F_FULLFSYNC reaches the medium.
    if (::fcntl(AsDescriptor(m_native), F_FULLFSYNC) == 0) {
        return {};
    }
#endif
    if (::fsync(AsDescriptor(m_native)) != 0) {
        return Detail::LastPosixError();
    }
    return {};
}

Result<void, Error> File::Close() {
    NX_ASSERT(IsOpen(), "File::Close on a closed file.");
    // close must not be retried after EINTR: the descriptor is released either way.
    if (::close(AsDescriptor(Exchange(m_native, kInvalid))) != 0 && errno != EINTR) {
        return Detail::LastPosixError();
    }
    return {};
}

} // namespace Nexyl::Platform
