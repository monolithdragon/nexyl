#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// Errors reported by the Platform module. The operating system's own error number is kept in
/// Error::GetDetail(), so a failure can be traced to the exact system call result.
/// @since 0.1
enum class PlatformError : u16 {
    /// Physical memory or the system commit limit is exhausted.
    OutOfMemory = 1,
    /// No free address range of the requested size exists.
    AddressSpaceExhausted,
    /// An argument was rejected by the operating system.
    InvalidArgument,
    /// The file, directory, library or symbol does not exist.
    NotFound,
    /// The file or directory already exists.
    AlreadyExists,
    /// The operating system denied access.
    AccessDenied,
    /// A path is longer than the platform layer supports.
    PathTooLong,
    /// A path is not valid UTF-8 or contains characters the file system rejects.
    InvalidPath,
    /// A directory was expected but the path names something else, or the reverse.
    WrongFileType,
    /// The directory is not empty.
    DirectoryNotEmpty,
    /// A per-process or system-wide limit was reached, such as open files or threads.
    ResourceLimit,
    /// A read or write failed at the device or file-system level.
    IoError,
    /// Any other operating-system failure. The detail holds the system error number.
    SystemError,
};

/// Associates PlatformError with the Platform module (required by the ErrorEnum concept).
///
/// @return ModuleId::Platform.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr ModuleId GetErrorModule(PlatformError /*code*/) noexcept {
    return ModuleId::Platform;
}

} // namespace Nexyl::Platform
