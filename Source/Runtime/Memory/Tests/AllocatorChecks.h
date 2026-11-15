#pragma once

// Checks shared by the allocator tests: every allocator must meet the IAllocator contract.

#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Test/Test.h>

namespace Nexyl::Memory::Tests {

/// A small deterministic random generator for tests (xorshift64*). Fixed seeds keep tests reproducible (TEST-4).
class TestRandom final {
public:
    explicit TestRandom(u64 seed) : m_state(seed != 0 ? seed : 1) {}

    u64 Next() {
        m_state ^= m_state >> 12U;
        m_state ^= m_state << 25U;
        m_state ^= m_state >> 27U;
        return m_state * u64{0x2545F4914F6CDD1D};
    }

    /// A value in [minimum, maximum].
    usize Between(usize minimum, usize maximum) {
        return minimum + static_cast<usize>(Next() % (maximum - minimum + 1));
    }

private:
    u64 m_state = 1;
};

/// Fills a block with a pattern derived from `seed`.
inline void Fill(void* block, usize size, u8 seed) {
    u8* const bytes = static_cast<u8*>(block);
    for (usize index = 0; index < size; ++index) {
        bytes[index] = static_cast<u8>(seed + index * 31U);
    }
}

/// Checks the pattern written by Fill.
inline bool HasPattern(const void* block, usize size, u8 seed) {
    const u8* const bytes = static_cast<const u8*>(block);
    for (usize index = 0; index < size; ++index) {
        if (bytes[index] != static_cast<u8>(seed + index * 31U)) {
            return false;
        }
    }
    return true;
}

/// Allocates blocks of several sizes and alignments, resizes some, and checks that every block is aligned,
/// owned, and keeps its contents, so no block overlaps another. Releases everything at the end.
/// @param maxSize      Largest block to request.
/// @param maxAlignment Largest alignment to request.
inline void CheckContract(IAllocator& allocator, usize maxSize, usize maxAlignment) {
    struct Block {
        void* address = nullptr;
        usize size = 0;
        usize alignment = 0;
    };
    constexpr usize kCount = 24;
    Block blocks[kCount] = {};
    TestRandom random(7);
    const u64 totalBefore = allocator.GetStatistics().totalAllocationCount;

    for (usize index = 0; index < kCount; ++index) {
        Block& block = blocks[index];
        block.size = random.Between(1, maxSize);
        const usize shift = random.Between(0, HighestSetBit(maxAlignment)) % 64U;
        block.alignment = usize{1} << shift;
        const Result<void*, Error> allocated = allocator.TryAllocate(block.size, block.alignment);
        NX_REQUIRE(allocated.IsOk());
        block.address = allocated.GetValue();
        NX_CHECK(IsAligned(block.address, block.alignment));
        NX_CHECK(allocator.Owns(block.address));
        Fill(block.address, block.size, static_cast<u8>(index));
    }
    NX_CHECK(allocator.GetStatistics().totalAllocationCount == totalBefore + kCount);

    // Resize every third block, both directions; contents survive up to the smaller size.
    for (usize index = 0; index < kCount; index += 3) {
        Block& block = blocks[index];
        const usize newSize = index % 2 == 0 ? block.size + random.Between(1, maxSize) : (block.size + 1) / 2;
        const Result<void*, Error> resized =
            allocator.TryReallocate(block.address, block.size, newSize, block.alignment);
        NX_REQUIRE(resized.IsOk());
        NX_CHECK(HasPattern(resized.GetValue(), newSize < block.size ? newSize : block.size, static_cast<u8>(index)));
        block.address = resized.GetValue();
        block.size = newSize;
        NX_CHECK(IsAligned(block.address, block.alignment));
        Fill(block.address, block.size, static_cast<u8>(index));
    }

    for (usize index = 0; index < kCount; ++index) {
        NX_CHECK(HasPattern(blocks[index].address, blocks[index].size, static_cast<u8>(index)));
    }
    for (const Block& block : blocks) {
        allocator.Deallocate(block.address, block.size);
    }
    allocator.Deallocate(nullptr, 16); // Null is ignored.
}

} // namespace Nexyl::Memory::Tests
