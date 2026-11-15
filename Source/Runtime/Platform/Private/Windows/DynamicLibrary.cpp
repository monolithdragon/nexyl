#include <Nexyl/Platform/DynamicLibrary.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>

#include "Windows/WindowsSupport.h"

namespace Nexyl::Platform {

namespace {

HMODULE AsModule(usize native) {
    return BitCast<HMODULE>(native);
}

// A fully qualified path starts with a drive ("C:\") or is a UNC path ("\\server").
bool IsFullyQualified(const wchar_t* path) {
    const bool hasDrive = path[0] != L'\0' && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/');
    const bool isUnc = path[0] == L'\\' && path[1] == L'\\';
    return hasDrive || isUnc;
}

} // namespace

Result<DynamicLibrary, Error> DynamicLibrary::Open(const char* path) {
    wchar_t widePath[Detail::kMaxWidePath] = {};
    NX_TRY(Detail::ToWide(path, Span<wchar_t>(widePath)));

    // The default safe search never includes the current directory. A library loaded by full path also
    // finds the libraries it depends on next to itself.
    DWORD flags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS;
    if (IsFullyQualified(widePath)) {
        flags |= LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
    }
    const HMODULE module = ::LoadLibraryExW(widePath, nullptr, flags);
    if (module == nullptr) {
        return Detail::LastWindowsError();
    }

    DynamicLibrary library;
    library.m_native = BitCast<usize>(module);
    return library;
}

const char* DynamicLibrary::GetFileExtension() {
    return ".dll";
}

DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept : m_native(Exchange(other.m_native, usize{0})) {}

DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept {
    if (this != &other) {
        if (IsOpen()) {
            Close();
        }
        m_native = Exchange(other.m_native, usize{0});
    }
    return *this;
}

DynamicLibrary::~DynamicLibrary() {
    if (IsOpen()) {
        Close();
    }
}

Option<void*> DynamicLibrary::FindSymbol(const char* name) const {
    NX_ASSERT(IsOpen(), "DynamicLibrary::FindSymbol on a closed library.");
    auto* const address = ::GetProcAddress(AsModule(m_native), name);
    if (address == nullptr) {
        return kNone;
    }
    return reinterpret_cast<void*>(address);
}

void DynamicLibrary::Close() {
    NX_ASSERT(IsOpen(), "DynamicLibrary::Close on a closed library.");
    NX_VERIFY(::FreeLibrary(AsModule(Exchange(m_native, usize{0}))) != 0);
}

} // namespace Nexyl::Platform
