#include <Nexyl/Memory/MemoryOperations.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Memory/Alignment.h>

// The four memory functions of the C runtime. Compilers emit calls to them on their own, for example to copy large
// structures, so every binary links them whether or not the engine calls them; they are the one documented use of
// the C runtime (docs/CODING_STANDARDS.md, section 4.1). They are declared here instead of through <string.h>,
// which the engine does not include, and nowhere else.
extern "C" {
void* memcpy(void* destination, const void* source, Nexyl::usize size);
void* memmove(void* destination, const void* source, Nexyl::usize size);
void* memset(void* destination, int value, Nexyl::usize size);
int memcmp(const void* first, const void* second, Nexyl::usize size);
}

namespace Nexyl::Memory {

// The C functions require valid pointers even for zero sizes, so a zero size returns before calling them.

void Copy(void* destination, const void* source, usize size) {
    if (size == 0) {
        return;
    }
    NX_ASSERT(destination != nullptr && source != nullptr, "Memory::Copy needs valid pointers.");
    NX_ASSERT(ToAddress(destination) + size <= ToAddress(source) || ToAddress(source) + size <= ToAddress(destination),
              "Memory::Copy ranges overlap; use Memory::Move.");
    ::memcpy(destination, source, size);
}

void Move(void* destination, const void* source, usize size) {
    if (size == 0) {
        return;
    }
    NX_ASSERT(destination != nullptr && source != nullptr, "Memory::Move needs valid pointers.");
    ::memmove(destination, source, size);
}

void Set(void* destination, u8 value, usize size) {
    if (size == 0) {
        return;
    }
    NX_ASSERT(destination != nullptr, "Memory::Set needs a valid pointer.");
    ::memset(destination, value, size);
}

i32 Compare(const void* first, const void* second, usize size) {
    if (size == 0) {
        return 0;
    }
    NX_ASSERT(first != nullptr && second != nullptr, "Memory::Compare needs valid pointers.");
    // memcmp only promises the sign; its magnitude differs between C runtimes.
    const int result = ::memcmp(first, second, size);
    return result < 0 ? -1 : (result > 0 ? 1 : 0);
}

} // namespace Nexyl::Memory
