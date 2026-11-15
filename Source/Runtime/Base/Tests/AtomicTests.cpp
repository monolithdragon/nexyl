#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

static_assert(IsPowerOfTwo(u32{1}) && IsPowerOfTwo(u64{1} << 40U) && IsPowerOfTwo(usize{4096}));
static_assert(!IsPowerOfTwo(u32{0}) && !IsPowerOfTwo(u32{12}));
static_assert(CountLeadingZeros(u32{1}) == 31 && CountLeadingZeros(u64{0}) == 64);
static_assert(CountTrailingZeros(u32{8}) == 3 && CountTrailingZeros(u16{0}) == 16);
static_assert(HighestSetBit(u32{1}) == 0 && HighestSetBit(u32{8}) == 3 && HighestSetBit(u32{15}) == 3);
static_assert(HighestSetBit(usize{1} << 63U) == 63);

namespace {

// Constant-initialised: proves that Atomic needs no static constructor (rule INIT-1).
constinit Atomic<u64> g_counter{5};

} // namespace

NX_TEST(Bits, MultiplyHighReturnsTheUpperHalf) {
    NX_CHECK(MultiplyHigh(u64{1} << 32U, u64{1} << 32U) == 1);
    NX_CHECK(MultiplyHigh(~u64{0}, ~u64{0}) == ~u64{0} - 1);
    NX_CHECK(MultiplyHigh(u64{0x123456789ABCDEF0}, 0x10) == 0x1);
    NX_CHECK(MultiplyHigh(12345, 67890) == 0);
}

NX_TEST(Atomic, LoadsAndStores) {
    Atomic<i32> value{3};
    NX_CHECK(value.Load(MemoryOrder::Acquire) == 3);
    value.Store(-4, MemoryOrder::Release);
    NX_CHECK(value.Load(MemoryOrder::SequentiallyConsistent) == -4);
    NX_CHECK(g_counter.Load(MemoryOrder::Relaxed) == 5);

    const Atomic<u32> zero;
    NX_CHECK(zero.Load(MemoryOrder::Relaxed) == 0);
}

NX_TEST(Atomic, ReadModifyWriteReturnsThePreviousValue) {
    Atomic<u32> value{10};
    NX_CHECK(value.FetchAdd(5, MemoryOrder::Relaxed) == 10);
    NX_CHECK(value.FetchSub(3, MemoryOrder::AcquireRelease) == 15);
    NX_CHECK(value.Exchange(100, MemoryOrder::SequentiallyConsistent) == 12);
    NX_CHECK(value.Load(MemoryOrder::Relaxed) == 100);
}

NX_TEST(Atomic, CompareExchangeReportsTheActualValueOnFailure) {
    i32 storage = 0;
    i32 other = 0;
    Atomic<i32*> pointer{&storage};

    i32* expected = &other;
    NX_CHECK(!pointer.CompareExchange(expected, nullptr, MemoryOrder::AcquireRelease));
    NX_CHECK(expected == &storage);

    NX_CHECK(pointer.CompareExchange(expected, &other, MemoryOrder::AcquireRelease));
    NX_CHECK(pointer.Load(MemoryOrder::Acquire) == &other);
}
