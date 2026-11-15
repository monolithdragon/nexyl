#include <Nexyl/Platform/Sanitizer.h>

#include <Nexyl/Base/Config.h>

#if NX_ASAN_ENABLED

#include <sanitizer/asan_interface.h>

namespace Nexyl::Platform {

void PoisonMemoryRegion(const void* address, usize size) {
    __asan_poison_memory_region(address, size);
}

void UnpoisonMemoryRegion(const void* address, usize size) {
    __asan_unpoison_memory_region(address, size);
}

} // namespace Nexyl::Platform

#endif
