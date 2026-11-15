#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Platform {

/// Access rights of committed pages.
/// @since 0.1
enum class MemoryAccess : u8 {
    /// Any access faults. Used for guard pages.
    None,
    /// Read-only.
    Read,
    /// Read and write. The state of newly committed pages.
    ReadWrite,
};

/// Reserves a range of address space without backing it with memory.
///
/// Reserved pages cannot be accessed until they are committed. Reserving is cheap, so an allocator can reserve
/// the most it may ever need and commit as it grows (docs/NEXYL_ARCHITECTURE.md, section 5.7).
///
/// @param size Bytes to reserve. Must be a non-zero multiple of SystemInfo::pageSize.
/// @return The base address, aligned to SystemInfo::allocationGranularity.
/// @error AddressSpaceExhausted No free range of that size exists.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void*, Error> ReserveAddressSpace(usize size);

/// Releases a whole reservation, including any committed pages in it.
///
/// @param base The address returned by ReserveAddressSpace.
/// @param size The size passed to ReserveAddressSpace.
/// @threadsafety safe
/// @since 0.1
NX_PLATFORM_API void ReleaseAddressSpace(void* base, usize size);

/// Backs reserved pages with memory and makes them readable and writable.
///
/// Pages that were not committed before read as zero afterwards. Committing pages that are already
/// committed leaves their contents unchanged.
///
/// @param address First page. Must be page-aligned and inside a reservation.
/// @param size    Bytes. Must be a non-zero multiple of the page size.
/// @error OutOfMemory The system cannot provide the memory.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> CommitMemory(void* address, usize size);

/// Returns the memory behind committed pages to the system and keeps the address range reserved.
/// The pages become inaccessible; committing them again yields zeroed pages.
///
/// @param address First page. Must be page-aligned and inside a reservation.
/// @param size    Bytes. Must be a non-zero multiple of the page size.
/// @error InvalidArgument The range is not inside a reservation.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> DecommitMemory(void* address, usize size);

/// Changes the access rights of committed pages. MemoryAccess::None turns them into guard pages.
///
/// @param address First page. Must be page-aligned and committed.
/// @param size    Bytes. Must be a non-zero multiple of the page size.
/// @param access  The new access rights.
/// @error InvalidArgument The range is not committed memory.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_PLATFORM_API Result<void, Error> ProtectMemory(void* address, usize size, MemoryAccess access);

} // namespace Nexyl::Platform
