#include <Nexyl/Platform/File.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>

#include "Windows/WindowsSupport.h"

namespace Nexyl::Platform {

namespace {

HANDLE AsHandle(isize native) {
    return BitCast<HANDLE>(native);
}

// ReadFile and WriteFile take 32-bit lengths, so large transfers go in chunks.
constexpr usize kMaxChunk = 0x4000'0000;

OVERLAPPED AtOffset(u64 offset) {
    OVERLAPPED overlapped = {};
    overlapped.Offset = static_cast<DWORD>(offset & 0xFFFF'FFFFU);
    overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);
    return overlapped;
}

} // namespace

Result<File, Error> File::Open(const char* path, FileAccess access, FileOpenMode mode) {
    wchar_t widePath[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(widePath)));

    DWORD desiredAccess = 0;
    switch (access) {
        case FileAccess::Read:
            desiredAccess = GENERIC_READ;
            break;
        case FileAccess::Write:
            desiredAccess = GENERIC_WRITE;
            break;
        case FileAccess::ReadWrite:
            desiredAccess = GENERIC_READ | GENERIC_WRITE;
            break;
    }

    DWORD disposition = OPEN_EXISTING;
    switch (mode) {
        case FileOpenMode::OpenExisting:
            disposition = OPEN_EXISTING;
            break;
        case FileOpenMode::CreateNew:
            disposition = CREATE_NEW;
            break;
        case FileOpenMode::CreateOrTruncate:
            disposition = CREATE_ALWAYS;
            break;
        case FileOpenMode::OpenOrCreate:
            disposition = OPEN_ALWAYS;
            break;
    }

    // Sharing everything matches POSIX: other handles may read, write, rename and delete the file.
    constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    void* const handle =
        ::CreateFileW(widePath, desiredAccess, kShareAll, nullptr, disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        // Windows reports a directory as "access denied"; report it the same way as POSIX does.
        const DWORD attributes = ::GetFileAttributesW(widePath);
        if (code == ERROR_ACCESS_DENIED && attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            return Error(PlatformError::WrongFileType, code);
        }
        return Detail::MakeWindowsError(code);
    }

    File file;
    file.m_native = BitCast<isize>(handle);
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
        const usize remaining = buffer.Size() - total;
        const auto chunk = static_cast<DWORD>(remaining < kMaxChunk ? remaining : kMaxChunk);
        OVERLAPPED overlapped = AtOffset(offset + total);
        DWORD count = 0;
        if (::ReadFile(AsHandle(m_native), buffer.Data() + total, chunk, &count, &overlapped) == 0) {
            const DWORD code = ::GetLastError();
            if (code == ERROR_HANDLE_EOF) {
                break;
            }
            return Detail::MakeWindowsError(code);
        }
        if (count == 0) {
            break;
        }
        total += count;
    }
    return total;
}

Result<void, Error> File::WriteAt(u64 offset, Span<const u8> data) const {
    NX_ASSERT(IsOpen(), "File::WriteAt on a closed file.");
    usize total = 0;
    while (total < data.Size()) {
        const usize remaining = data.Size() - total;
        const auto chunk = static_cast<DWORD>(remaining < kMaxChunk ? remaining : kMaxChunk);
        OVERLAPPED overlapped = AtOffset(offset + total);
        DWORD count = 0;
        if (::WriteFile(AsHandle(m_native), data.Data() + total, chunk, &count, &overlapped) == 0) {
            return Detail::LastWindowsError();
        }
        if (count == 0) {
            return Error(PlatformError::IoError);
        }
        total += count;
    }
    return {};
}

Result<u64, Error> File::GetSize() const {
    NX_ASSERT(IsOpen(), "File::GetSize on a closed file.");
    LARGE_INTEGER size = {};
    if (::GetFileSizeEx(AsHandle(m_native), &size) == 0) {
        return Detail::LastWindowsError();
    }
    return static_cast<u64>(size.QuadPart);
}

Result<void, Error> File::SetSize(u64 size) const {
    NX_ASSERT(IsOpen(), "File::SetSize on a closed file.");
    FILE_END_OF_FILE_INFO info = {};
    info.EndOfFile.QuadPart = static_cast<LONGLONG>(size);
    if (::SetFileInformationByHandle(AsHandle(m_native), FileEndOfFileInfo, &info, sizeof(info)) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> File::Flush() const {
    NX_ASSERT(IsOpen(), "File::Flush on a closed file.");
    if (::FlushFileBuffers(AsHandle(m_native)) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> File::Close() {
    NX_ASSERT(IsOpen(), "File::Close on a closed file.");
    void* const handle = AsHandle(Exchange(m_native, kInvalid));
    if (::CloseHandle(handle) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

} // namespace Nexyl::Platform
