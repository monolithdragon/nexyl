#include <Nexyl/Platform/SystemInfo.h>
#include <Nexyl/Platform/VirtualMemory.h>

#include <Nexyl/Base/Assert.h>

#include "Posix/PosixSupport.h"

#include <sys/mman.h>
#include <unistd.h>

namespace Nexyl::Platform {

namespace {

int ToProtection(MemoryAccess access) {
    switch (access) {
        case MemoryAccess::None:
            return PROT_NONE;
        case MemoryAccess::Read:
            return PROT_READ;
        case MemoryAccess::ReadWrite:
            return PROT_READ | PROT_WRITE;
    }
    NX_UNREACHABLE();
}

[[maybe_unused]] bool IsPageMultiple(usize value) {
    return value != 0 && value % GetSystemInfo().pageSize == 0;
}

// Flags of an inaccessible anonymous mapping that holds address space but no memory.
constexpr int kReserveFlags = MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE;

} // namespace

SystemInfo GetSystemInfo() {
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    const long processors = ::sysconf(_SC_NPROCESSORS_ONLN);

    SystemInfo result;
    result.pageSize = pageSize > 0 ? static_cast<usize>(pageSize) : 4096;
    // POSIX reservations are aligned to the page size only.
    result.allocationGranularity = result.pageSize;
    result.logicalProcessorCount = processors > 0 ? static_cast<u32>(processors) : 1;
    return result;
}

Result<void*, Error> ReserveAddressSpace(usize size) {
    NX_ASSERT(IsPageMultiple(size), "Reservation size must be a non-zero multiple of the page size.");
    void* const base = ::mmap(nullptr, size, PROT_NONE, kReserveFlags, -1, 0);
    if (base == MAP_FAILED) {
        const int code = errno;
        if (code == ENOMEM) {
            return Error(PlatformError::AddressSpaceExhausted, static_cast<u32>(code));
        }
        return Detail::MakePosixError(code);
    }
    return base;
}

void ReleaseAddressSpace(void* base, usize size) {
    NX_ASSERT(base != nullptr && IsPageMultiple(size), "Invalid reservation.");
    NX_VERIFY(::munmap(base, size) == 0);
}

Result<void, Error> CommitMemory(void* address, usize size) {
    NX_ASSERT(IsPageMultiple(size), "Commit size must be a non-zero multiple of the page size.");
    // Pages of an anonymous mapping are materialised, zero-filled, on first touch.
    if (::mprotect(address, size, PROT_READ | PROT_WRITE) != 0) {
        return Detail::LastPosixError();
    }
    return {};
}

Result<void, Error> DecommitMemory(void* address, usize size) {
    NX_ASSERT(IsPageMultiple(size), "Decommit size must be a non-zero multiple of the page size.");
    // Mapping fresh inaccessible pages over the range discards the old pages on every POSIX system,
    // unlike madvise, whose effect differs between Linux and macOS. The range stays reserved.
    if (::mmap(address, size, PROT_NONE, kReserveFlags | MAP_FIXED, -1, 0) == MAP_FAILED) {
        return Detail::LastPosixError();
    }
    return {};
}

Result<void, Error> ProtectMemory(void* address, usize size, MemoryAccess access) {
    NX_ASSERT(IsPageMultiple(size), "Protection size must be a non-zero multiple of the page size.");
    if (::mprotect(address, size, ToProtection(access)) != 0) {
        return Detail::LastPosixError();
    }
    return {};
}

} // namespace Nexyl::Platform
