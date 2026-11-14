#include <Nexyl/Platform/FileSystem.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>

#include "Windows/WindowsSupport.h"

namespace Nexyl::Platform {

namespace {

// FILETIME counts 100-nanosecond intervals since 1601-01-01; this is the count at 1970-01-01.
constexpr u64 kUnixEpochInFileTime = 116'444'736'000'000'000;

u64 ToUnixNanoseconds(const FILETIME& time) {
    const u64 ticks = (u64{time.dwHighDateTime} << 32U) | time.dwLowDateTime;
    return ticks > kUnixEpochInFileTime ? (ticks - kUnixEpochInFileTime) * 100 : 0;
}

bool IsDotOrDotDot(const wchar_t* name) {
    return name[0] == L'.' && (name[1] == L'\0' || (name[1] == L'.' && name[2] == L'\0'));
}

HANDLE AsHandle(usize native) {
    return BitCast<HANDLE>(native);
}

// True for a found entry that links elsewhere: a symbolic link or a junction. Other reparse points, such as
// cloud placeholders, are ordinary files and directories.
bool IsLink(const WIN32_FIND_DATAW& data) {
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0 &&
           (data.dwReserved0 == IO_REPARSE_TAG_SYMLINK || data.dwReserved0 == IO_REPARSE_TAG_MOUNT_POINT);
}

// True if `path` itself, not what it points to, is a symbolic link or a junction.
bool IsLinkPath(const wchar_t* path) {
    const DWORD attributes = ::GetFileAttributesW(path);
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0) {
        return false;
    }
    // Only a find result carries the reparse tag. Without a wildcard, the search returns the entry itself.
    WIN32_FIND_DATAW data = {};
    void* const handle = ::FindFirstFileExW(path, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        return false;
    }
    NX_VERIFY(::FindClose(handle) != 0);
    return IsLink(data);
}

// Describes what a reparse point resolves to, as POSIX stat does. Fails with NotFound for a dangling link.
Result<WIN32_FILE_ATTRIBUTE_DATA, Error> DescribeTarget(const wchar_t* path) {
    // Backup semantics allow opening directories; the open follows links.
    constexpr DWORD kShareAll = FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE;
    void* const handle = ::CreateFileW(path, FILE_READ_ATTRIBUTES, kShareAll, nullptr, OPEN_EXISTING,
                                       FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return Detail::LastWindowsError();
    }
    BY_HANDLE_FILE_INFORMATION info = {};
    const bool described = ::GetFileInformationByHandle(handle, &info) != 0;
    const DWORD code = described ? ERROR_SUCCESS : ::GetLastError();
    NX_VERIFY(::CloseHandle(handle) != 0);
    if (!described) {
        return Detail::MakeWindowsError(code);
    }

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    data.dwFileAttributes = info.dwFileAttributes;
    data.ftLastWriteTime = info.ftLastWriteTime;
    data.nFileSizeHigh = info.nFileSizeHigh;
    data.nFileSizeLow = info.nFileSizeLow;
    return data;
}

// Converts a found entry into a DirectoryEntry. On error `outEntry` is untouched.
Result<void, Error> ToEntry(const WIN32_FIND_DATAW& data, DirectoryEntry& outEntry) {
    DirectoryEntry entry;
    NX_TRY_ASSIGN(const usize length, Detail::ToUtf8(data.cFileName, Span<char>(entry.name)));
    entry.nameLength = length;
    // A link is never a subdirectory, as on POSIX, so a recursive walk cannot loop through one.
    entry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0 && !IsLink(data);
    outEntry = entry;
    return {};
}

} // namespace

Result<FileInfo, Error> GetFileInfo(const char* path) {
    wchar_t widePath[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(widePath)));

    WIN32_FILE_ATTRIBUTE_DATA data = {};
    if (::GetFileAttributesExW(widePath, GetFileExInfoStandard, &data) == 0) {
        return Detail::LastWindowsError();
    }
    if ((data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0) {
        // These attributes describe the link itself; report what it points to instead.
        const Result<WIN32_FILE_ATTRIBUTE_DATA, Error> target = DescribeTarget(widePath);
        if (target.IsError()) {
            return target.GetError();
        }
        data = target.GetValue();
    }

    FileInfo info;
    info.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    info.size = info.isDirectory ? 0 : (u64{data.nFileSizeHigh} << 32U) | data.nFileSizeLow;
    info.modificationTime = ToUnixNanoseconds(data.ftLastWriteTime);
    return info;
}

Result<void, Error> RemoveFile(const char* path) {
    wchar_t widePath[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(widePath)));
    if (::DeleteFileW(widePath) == 0) {
        const DWORD code = ::GetLastError();
        const DWORD attributes = ::GetFileAttributesW(widePath);
        if (code == ERROR_ACCESS_DENIED && attributes != INVALID_FILE_ATTRIBUTES &&
            (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
            // A link to a directory is removed like a file, as unlink does on POSIX. The target is untouched.
            if (IsLinkPath(widePath)) {
                if (::RemoveDirectoryW(widePath) == 0) {
                    return Detail::LastWindowsError();
                }
                return {};
            }
            return Error(PlatformError::WrongFileType, code);
        }
        return Detail::MakeWindowsError(code);
    }
    return {};
}

Result<void, Error> RenameFile(const char* from, const char* to) {
    wchar_t wideFrom[Detail::kMaxWidePath] = {};
    wchar_t wideTo[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(from, Span<wchar_t>(wideFrom)));
    NX_TRY(Detail::ToWide(to, Span<wchar_t>(wideTo)));
    if (::MoveFileExW(wideFrom, wideTo, MOVEFILE_REPLACE_EXISTING) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> MakeDirectory(const char* path) {
    wchar_t widePath[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(widePath)));
    if (::CreateDirectoryW(widePath, nullptr) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> RemoveEmptyDirectory(const char* path) {
    wchar_t widePath[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(widePath)));
    // RemoveDirectory would delete a link to a directory. A link is not a directory, as rmdir reports on POSIX.
    if (IsLinkPath(widePath)) {
        return Error(PlatformError::WrongFileType, ERROR_DIRECTORY);
    }
    if (::RemoveDirectoryW(widePath) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> MakeSymbolicLink(const char* target, const char* linkPath, LinkTargetKind kind) {
    wchar_t wideTarget[Detail::kMaxWidePath] = {};
    wchar_t wideLink[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(target, Span<wchar_t>(wideTarget)));
    NX_TRY(Detail::ToWide(linkPath, Span<wchar_t>(wideLink)));
    // The target is stored as written, and Windows resolves only backslashes in it.
    for (wchar_t& character : wideTarget) {
        if (character == L'/') {
            character = L'\\';
        }
    }

    // Without the flag, only elevated processes may create links, even in Developer Mode.
    DWORD flags = SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE;
    if (kind == LinkTargetKind::Directory) {
        flags |= SYMBOLIC_LINK_FLAG_DIRECTORY;
    }
    if (::CreateSymbolicLinkW(wideLink, wideTarget, flags) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<DirectoryIterator, Error> DirectoryIterator::Open(const char* path) {
    // FindFirstFile takes a pattern: append "\*" to the directory path.
    wchar_t pattern[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(pattern).First(Detail::kMaxWidePath - 2)));
    usize length = 0;
    while (pattern[length] != L'\0') {
        ++length;
    }
    // An empty path would become the pattern "*" and list the current directory; POSIX reports it as missing.
    if (length == 0) {
        return Error(PlatformError::NotFound, ERROR_PATH_NOT_FOUND);
    }
    if (pattern[length - 1] != L'\\' && pattern[length - 1] != L'/') {
        pattern[length++] = L'\\';
    }
    pattern[length] = L'*';

    WIN32_FIND_DATAW data = {};
    void* const handle =
        ::FindFirstFileExW(pattern, FindExInfoBasic, &data, FindExSearchNameMatch, nullptr, FIND_FIRST_EX_LARGE_FETCH);
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_DIRECTORY) {
            return Error(PlatformError::WrongFileType, code);
        }
        return Detail::MakeWindowsError(code);
    }

    DirectoryIterator iterator;
    iterator.m_native = BitCast<usize>(handle);
    // The first entry is already here. Keep it unless it is "." or "..".
    if (!IsDotOrDotDot(data.cFileName)) {
        NX_TRY(ToEntry(data, iterator.m_pending));
        iterator.m_hasPending = true;
    }
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
        NX_VERIFY(::FindClose(AsHandle(m_native)) != 0);
        m_native = 0;
    }
    m_hasPending = false;
}

Result<bool, Error> DirectoryIterator::Next(DirectoryEntry& outEntry) {
    if (m_native == 0) {
        return false;
    }
    if (m_hasPending) {
        m_hasPending = false;
        outEntry = m_pending;
        return true;
    }

    WIN32_FIND_DATAW data = {};
    while (::FindNextFileW(AsHandle(m_native), &data) != 0) {
        if (!IsDotOrDotDot(data.cFileName)) {
            NX_TRY(ToEntry(data, outEntry));
            return true;
        }
    }
    const DWORD code = ::GetLastError();
    if (code == ERROR_NO_MORE_FILES) {
        // Release the directory as soon as the listing ends; later calls keep returning false.
        Close();
        return false;
    }
    return Detail::MakeWindowsError(code);
}

} // namespace Nexyl::Platform
