#pragma once

// Helpers shared by the Windows implementation files. Not part of the public API.

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Platform/PlatformErrors.h>

#include <Windows.h>

namespace Nexyl::Platform::Detail {

/// Longest path, in UTF-16 code units including the terminator, that the Windows layer converts.
inline constexpr usize kMaxWidePath = 4096;

/// Converts a Win32 error code into an Error, keeping the code as detail.
inline Error MakeWindowsError(DWORD code, const SourceLocation& location = SourceLocation::Current()) {
    PlatformError mapped = PlatformError::SystemError;
    switch (code) {
        case ERROR_NOT_ENOUGH_MEMORY:
        case ERROR_OUTOFMEMORY:
        case ERROR_COMMITMENT_LIMIT:
            mapped = PlatformError::OutOfMemory;
            break;
        case ERROR_INVALID_PARAMETER:
        case ERROR_INVALID_ADDRESS:
            mapped = PlatformError::InvalidArgument;
            break;
        case ERROR_FILE_NOT_FOUND:
        case ERROR_PATH_NOT_FOUND:
        case ERROR_MOD_NOT_FOUND:
        case ERROR_PROC_NOT_FOUND:
            mapped = PlatformError::NotFound;
            break;
        case ERROR_FILE_EXISTS:
        case ERROR_ALREADY_EXISTS:
            mapped = PlatformError::AlreadyExists;
            break;
        case ERROR_ACCESS_DENIED:
        case ERROR_SHARING_VIOLATION:
        case ERROR_LOCK_VIOLATION:
            mapped = PlatformError::AccessDenied;
            break;
        case ERROR_FILENAME_EXCED_RANGE:
            mapped = PlatformError::PathTooLong;
            break;
        case ERROR_INVALID_NAME:
        case ERROR_NO_UNICODE_TRANSLATION:
            mapped = PlatformError::InvalidPath;
            break;
        case ERROR_DIRECTORY:
            mapped = PlatformError::WrongFileType;
            break;
        case ERROR_DIR_NOT_EMPTY:
            mapped = PlatformError::DirectoryNotEmpty;
            break;
        case ERROR_TOO_MANY_OPEN_FILES:
        case ERROR_NO_SYSTEM_RESOURCES:
            mapped = PlatformError::ResourceLimit;
            break;
        case ERROR_HANDLE_EOF:
        case ERROR_CRC:
        case ERROR_DISK_FULL:
        case ERROR_HANDLE_DISK_FULL:
            mapped = PlatformError::IoError;
            break;
        default:
            break;
    }
    return Error(mapped, static_cast<u32>(code), location);
}

/// The calling thread's last Win32 error as an Error.
inline Error LastWindowsError(const SourceLocation& location = SourceLocation::Current()) {
    return MakeWindowsError(::GetLastError(), location);
}

/// Converts a NUL-terminated UTF-8 string into a NUL-terminated UTF-16 string in `output`.
/// Fails with PathTooLong if it does not fit and InvalidPath if the input is not valid UTF-8.
inline Result<void, Error> ToWide(const char* utf8, Span<wchar_t> output,
                                  const SourceLocation& location = SourceLocation::Current()) {
    const int capacity = static_cast<int>(output.Size());
    const int written = ::MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, utf8, -1, output.Data(), capacity);
    if (written == 0) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_INSUFFICIENT_BUFFER) {
            return Error(PlatformError::PathTooLong, static_cast<u32>(code), location);
        }
        return MakeWindowsError(code, location);
    }
    return {};
}

/// Converts a NUL-terminated UTF-16 string into UTF-8 in `output`, without a terminator.
/// @return The number of bytes written, or zero if the text does not fit or is invalid.
inline usize ToUtf8(const wchar_t* wide, Span<char> output) {
    const int written = ::WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide, -1, output.Data(),
                                              static_cast<int>(output.Size()), nullptr, nullptr);
    return written > 0 ? static_cast<usize>(written - 1) : 0;
}

} // namespace Nexyl::Platform::Detail
