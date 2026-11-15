#pragma once

// Helpers shared by the POSIX implementation files. Not part of the public API.

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Platform/PlatformErrors.h>

#include <errno.h>

namespace Nexyl::Platform::Detail {

/// Converts an errno value into an Error, keeping the value as detail.
inline Error MakePosixError(int code, const SourceLocation& location = SourceLocation::Current()) {
    PlatformError mapped = PlatformError::SystemError;
    switch (code) {
        case ENOMEM:
            mapped = PlatformError::OutOfMemory;
            break;
        case EINVAL:
        case EBADF:
            mapped = PlatformError::InvalidArgument;
            break;
        case ENOENT:
            mapped = PlatformError::NotFound;
            break;
        case EEXIST:
            mapped = PlatformError::AlreadyExists;
            break;
        case EACCES:
        case EPERM:
        case EROFS:
            mapped = PlatformError::AccessDenied;
            break;
        case ENAMETOOLONG:
            mapped = PlatformError::PathTooLong;
            break;
        case EILSEQ:
            mapped = PlatformError::InvalidPath;
            break;
        case EISDIR:
        case ENOTDIR:
            mapped = PlatformError::WrongFileType;
            break;
        case ENOTEMPTY:
            mapped = PlatformError::DirectoryNotEmpty;
            break;
        case EMFILE:
        case ENFILE:
        case EAGAIN:
            mapped = PlatformError::ResourceLimit;
            break;
        case EIO:
        case ENOSPC:
        case EFBIG:
            mapped = PlatformError::IoError;
            break;
        default:
            break;
    }
    return Error(mapped, static_cast<u32>(code), location);
}

/// The calling thread's errno as an Error.
inline Error LastPosixError(const SourceLocation& location = SourceLocation::Current()) {
    return MakePosixError(errno, location);
}

} // namespace Nexyl::Platform::Detail
