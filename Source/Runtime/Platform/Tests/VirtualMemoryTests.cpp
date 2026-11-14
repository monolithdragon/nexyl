#include <Nexyl/Base/Types.h>
#include <Nexyl/Platform/SystemInfo.h>
#include <Nexyl/Platform/VirtualMemory.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Platform;

NX_TEST(SystemInfo, ReportsPlausibleValues) {
    const SystemInfo info = GetSystemInfo();
    NX_CHECK(info.pageSize >= 4096);
    NX_CHECK((info.pageSize & (info.pageSize - 1)) == 0);
    NX_CHECK(info.allocationGranularity >= info.pageSize);
    NX_CHECK(info.allocationGranularity % info.pageSize == 0);
    NX_CHECK(info.logicalProcessorCount >= 1);
}

NX_TEST(VirtualMemory, ReserveCommitWriteDecommitRelease) {
    const SystemInfo info = GetSystemInfo();
    const usize size = info.allocationGranularity * 4;

    Result<void*, Error> reserved = ReserveAddressSpace(size);
    NX_REQUIRE(reserved.IsOk());
    auto* const base = static_cast<u8*>(reserved.GetValue());
    NX_CHECK(reinterpret_cast<usize>(base) % info.allocationGranularity == 0);

    // Commit the second page only; it must read as zero and accept writes.
    u8* const page = base + info.pageSize;
    NX_REQUIRE(CommitMemory(page, info.pageSize).IsOk());
    NX_CHECK(page[0] == 0 && page[info.pageSize - 1] == 0);
    page[0] = 0xAB;
    page[info.pageSize - 1] = 0xCD;
    NX_CHECK(page[0] == 0xAB && page[info.pageSize - 1] == 0xCD);

    // Committing again keeps the contents.
    NX_REQUIRE(CommitMemory(page, info.pageSize).IsOk());
    NX_CHECK(page[0] == 0xAB);

    // After decommit and recommit the page is zero again.
    NX_REQUIRE(DecommitMemory(page, info.pageSize).IsOk());
    NX_REQUIRE(CommitMemory(page, info.pageSize).IsOk());
    NX_CHECK(page[0] == 0 && page[info.pageSize - 1] == 0);

    ReleaseAddressSpace(base, size);
}

NX_TEST(VirtualMemory, ProtectChangesAccessRights) {
    const SystemInfo info = GetSystemInfo();
    const usize size = info.allocationGranularity;

    Result<void*, Error> reserved = ReserveAddressSpace(size);
    NX_REQUIRE(reserved.IsOk());
    auto* const base = static_cast<u8*>(reserved.GetValue());
    NX_REQUIRE(CommitMemory(base, info.pageSize).IsOk());

    base[1] = 7;
    // Faulting accesses cannot be tested in-process; the test proves the calls succeed and that
    // restoring write access keeps the data.
    NX_CHECK(ProtectMemory(base, info.pageSize, MemoryAccess::Read).IsOk());
    NX_CHECK(base[1] == 7);
    NX_CHECK(ProtectMemory(base, info.pageSize, MemoryAccess::None).IsOk());
    NX_CHECK(ProtectMemory(base, info.pageSize, MemoryAccess::ReadWrite).IsOk());
    base[1] = 8;
    NX_CHECK(base[1] == 8);

    ReleaseAddressSpace(base, size);
}

NX_TEST(VirtualMemory, LargeReservationCostsNoMemory) {
    // 64 GiB of address space is far more than the test machine's memory; reserving must still succeed,
    // because nothing is committed (docs/NEXYL_ARCHITECTURE.md, section 5.7).
    const usize size = usize{64} << 30U;
    Result<void*, Error> reserved = ReserveAddressSpace(size);
    NX_REQUIRE(reserved.IsOk());
    ReleaseAddressSpace(reserved.GetValue(), size);
}
