#include <Nexyl/Platform/SystemInfo.h>
#include <Nexyl/Platform/VirtualMemory.h>

#include <Nexyl/Base/Assert.h>

#include "Windows/WindowsSupport.h"

namespace Nexyl::Platform {

namespace {

DWORD ToProtection(MemoryAccess access) {
    switch (access) {
        case MemoryAccess::None:
            return PAGE_NOACCESS;
        case MemoryAccess::Read:
            return PAGE_READONLY;
        case MemoryAccess::ReadWrite:
            return PAGE_READWRITE;
    }
    NX_UNREACHABLE();
}

bool IsPageMultiple(usize value) {
    return value != 0 && value % GetSystemInfo().pageSize == 0;
}

} // namespace

SystemInfo GetSystemInfo() {
    SYSTEM_INFO info = {};
    ::GetSystemInfo(&info);

    SystemInfo result;
    result.pageSize = info.dwPageSize;
    result.allocationGranularity = info.dwAllocationGranularity;
    result.logicalProcessorCount = static_cast<u32>(::GetActiveProcessorCount(ALL_PROCESSOR_GROUPS));
    return result;
}

Result<void*, Error> ReserveAddressSpace(usize size) {
    NX_ASSERT(IsPageMultiple(size), "Reservation size must be a non-zero multiple of the page size.");
    void* const base = ::VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
    if (base == nullptr) {
        const DWORD code = ::GetLastError();
        if (code == ERROR_NOT_ENOUGH_MEMORY) {
            return Error(PlatformError::AddressSpaceExhausted, code);
        }
        return Detail::MakeWindowsError(code);
    }
    return base;
}

void ReleaseAddressSpace(void* base, usize size) {
    NX_ASSERT(base != nullptr && IsPageMultiple(size), "Invalid reservation.");
    // MEM_RELEASE requires a size of zero and releases the whole reservation made at `base`.
    NX_VERIFY(::VirtualFree(base, 0, MEM_RELEASE) != 0);
}

Result<void, Error> CommitMemory(void* address, usize size) {
    NX_ASSERT(IsPageMultiple(size), "Commit size must be a non-zero multiple of the page size.");
    if (::VirtualAlloc(address, size, MEM_COMMIT, PAGE_READWRITE) == nullptr) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> DecommitMemory(void* address, usize size) {
    NX_ASSERT(IsPageMultiple(size), "Decommit size must be a non-zero multiple of the page size.");
    if (::VirtualFree(address, size, MEM_DECOMMIT) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

Result<void, Error> ProtectMemory(void* address, usize size, MemoryAccess access) {
    NX_ASSERT(IsPageMultiple(size), "Protection size must be a non-zero multiple of the page size.");
    DWORD previous = 0;
    if (::VirtualProtect(address, size, ToProtection(access), &previous) == 0) {
        return Detail::LastWindowsError();
    }
    return {};
}

} // namespace Nexyl::Platform
