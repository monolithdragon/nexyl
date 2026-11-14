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

// Converts a found entry into a DirectoryEntry.
Result<void, Error> ToEntry(const WIN32_FIND_DATAW& data, DirectoryEntry& outEntry) {
    const usize length = Detail::ToUtf8(data.cFileName, Span<char>(outEntry.name));
    if (length == 0) {
        return Error(PlatformError::PathTooLong);
    }
    outEntry.nameLength = length;
    outEntry.isDirectory = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
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
    if (::RemoveDirectoryW(widePath) == 0) {
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
    if (length > 0 && pattern[length - 1] != L'\\' && pattern[length - 1] != L'/') {
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
        return false;
    }
    return Detail::MakeWindowsError(code);
}

} // namespace Nexyl::Platform
