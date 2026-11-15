#include <Nexyl/Platform/DynamicLibrary.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Utility.h>

#include "Posix/PosixSupport.h"

#include <dlfcn.h>

// TODO(android-port): Android keeps dlopen in libdl, which the build must link for Platform there.

namespace Nexyl::Platform {

namespace {

void* AsHandle(usize native) {
    return BitCast<void*>(native);
}

} // namespace

Result<DynamicLibrary, Error> DynamicLibrary::Open(const char* path) {
    // RTLD_LOCAL keeps the library's symbols out of the global namespace, so plugins cannot clash.
    void* const handle = ::dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        // dlopen reports failures as text through dlerror, not through errno. Read it to clear it.
        static_cast<void>(::dlerror());
        return Error(PlatformError::NotFound);
    }
    DynamicLibrary library;
    library.m_native = BitCast<usize>(handle);
    return library;
}

const char* DynamicLibrary::GetFileExtension() {
#if NX_PLATFORM_APPLE
    return ".dylib";
#else
    return ".so";
#endif
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
    void* const address = ::dlsym(AsHandle(m_native), name);
    if (address == nullptr) {
        static_cast<void>(::dlerror());
        return kNone;
    }
    return address;
}

void DynamicLibrary::Close() {
    NX_ASSERT(IsOpen(), "DynamicLibrary::Close on a closed library.");
    NX_VERIFY(::dlclose(AsHandle(Exchange(m_native, usize{0}))) == 0);
}

} // namespace Nexyl::Platform
