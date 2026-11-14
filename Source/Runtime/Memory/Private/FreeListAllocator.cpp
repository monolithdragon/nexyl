#include <Nexyl/Memory/FreeListAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/Sanitizer.h>

// Two-level segregated fit, after M. Masmano, I. Ripoll, A. Crespo and J. Real, "TLSF: a new dynamic memory
// allocator for real-time systems" (ECRTS 2004).
//
// Memory is a sequence of blocks. Each block has a 16-byte header followed by its payload; payload sizes are
// multiples of 16. A pool ends with a zero-size sentinel block that is never free, so every real block has a
// successor. Free blocks are never adjacent: release merges a block with its free neighbours at once. Each free
// block sits in the list of its size class, and two bitmaps record which lists are non-empty, so the smallest
// suitable class is found with two bit scans.

namespace Nexyl::Memory {

namespace Detail {

// Every block starts with this header.
struct FreeListBlock {
    // The block just before this one in memory. Valid only while that block is free (kPreviousFreeFlag).
    FreeListBlock* previousPhysical = nullptr;
    // The payload size, a multiple of 16, with kFreeFlag and kPreviousFreeFlag in the two lowest bits.
    usize sizeAndFlags = 0;
};

// Every pool starts with this header; it links the pools for Owns.
struct FreeListPool {
    FreeListPool* next = nullptr;
    // Bytes from this header to the end of the pool.
    usize size = 0;
};

} // namespace Detail

namespace {

using Block = Detail::FreeListBlock;

// The first payload bytes of a free block.
struct FreeLinks {
    Block* next = nullptr;
    Block* previous = nullptr;
};

constexpr usize kBlockAlignmentLog2 = 4;
constexpr usize kBlockAlignment = usize{1} << kBlockAlignmentLog2;
constexpr usize kHeaderSize = sizeof(Block);
constexpr usize kMinimumPayload = sizeof(FreeLinks);
constexpr usize kFreeFlag = 1;
constexpr usize kPreviousFreeFlag = 2;
constexpr usize kFlagMask = kFreeFlag | kPreviousFreeFlag;

static_assert(kHeaderSize == kBlockAlignment && kMinimumPayload == kBlockAlignment);
static_assert(sizeof(Detail::FreeListPool) == kBlockAlignment);

// Size classes. Sizes below kSmallBlockSize have exact classes 16 bytes apart in the first row; above it, each row
// covers one power of two, split into kSubdivisions equal parts.
constexpr u32 kSubdivisionLog2 = 5;
constexpr u32 kSubdivisions = 1U << kSubdivisionLog2;
constexpr u32 kRows = 40;
constexpr u32 kFirstRowShift = kSubdivisionLog2 + static_cast<u32>(kBlockAlignmentLog2);
constexpr usize kSmallBlockSize = usize{1} << kFirstRowShift;

struct SizeClass {
    u32 row = 0;
    u32 column = 0;
};

SizeClass Classify(usize size) {
    if (size < kSmallBlockSize) {
        return SizeClass{0, static_cast<u32>(size / (kSmallBlockSize / kSubdivisions))};
    }
    const u32 highest = HighestSetBit(size);
    const auto column = static_cast<u32>((size >> (highest - kSubdivisionLog2)) ^ (usize{1} << kSubdivisionLog2));
    return SizeClass{highest - (kFirstRowShift - 1), column};
}

// Rounds a request up so that every block in its size class is at least as large as the request.
usize RoundUpToClass(usize size) {
    if (size < kSmallBlockSize) {
        return size;
    }
    return size + (usize{1} << (HighestSetBit(size) - kSubdivisionLog2)) - 1;
}

usize SizeOf(const Block* block) {
    return block->sizeAndFlags & ~kFlagMask;
}

void SetSize(Block* block, usize size) {
    block->sizeAndFlags = size | (block->sizeAndFlags & kFlagMask);
}

bool HasFlag(const Block* block, usize flag) {
    return (block->sizeAndFlags & flag) != 0;
}

void SetFlag(Block* block, usize flag, bool value) {
    block->sizeAndFlags = value ? (block->sizeAndFlags | flag) : (block->sizeAndFlags & ~flag);
}

u8* PayloadOf(Block* block) {
    return reinterpret_cast<u8*>(block) + kHeaderSize;
}

Block* FromPayload(void* payload) {
    return reinterpret_cast<Block*>(static_cast<u8*>(payload) - kHeaderSize);
}

Block* NextOf(Block* block) {
    return reinterpret_cast<Block*>(PayloadOf(block) + SizeOf(block));
}

FreeLinks* LinksOf(Block* block) {
    return reinterpret_cast<FreeLinks*>(PayloadOf(block));
}

Block* MakeBlock(void* address, usize payloadSize) {
    // A new block starts with both flags clear; a multiple of 16 leaves the flag bits free.
    NX_ASSERT((payloadSize & (kBlockAlignment - 1)) == 0, "Payload sizes are multiples of 16.");
    Block* const block = ConstructAt<Block>(address);
    block->sizeAndFlags = payloadSize;
    return block;
}

// Marks a block free and tells the block after it where its free neighbour starts.
void MarkFree(Block* block) {
    SetFlag(block, kFreeFlag, true);
    Block* const next = NextOf(block);
    next->previousPhysical = block;
    SetFlag(next, kPreviousFreeFlag, true);
}

void MarkUsed(Block* block) {
    SetFlag(block, kFreeFlag, false);
    SetFlag(NextOf(block), kPreviousFreeFlag, false);
}

usize PayloadSizeFor(usize size) {
    const usize payload = AlignUp(size, kBlockAlignment);
    return payload < kMinimumPayload ? kMinimumPayload : payload;
}

} // namespace

FreeListAllocator::FreeListAllocator() {
    static_assert(kRows == kFirstLevelCount && kSubdivisions == kSecondLevelCount,
                  "The free-list index in the header must match the size classes.");
}

FreeListAllocator::FreeListAllocator(Span<u8> pool) : FreeListAllocator() {
    AddPool(pool);
}

FreeListAllocator::~FreeListAllocator() {
    NX_ASSERT(m_counters.Get().liveAllocationCount == 0, "FreeListAllocator destroyed while blocks from it are alive.");
}

void FreeListAllocator::AddPool(Span<u8> pool) {
    NX_ASSERT(pool.Size() >= kMinimumPoolSize && pool.Size() <= kMaxAllocationSize,
              "A pool must hold between kMinimumPoolSize and kMaxAllocationSize bytes.");
    u8* const start = AlignUp(pool.Data(), kBlockAlignment);
    const usize end = AlignDown(ToAddress(pool.Data()) + pool.Size(), kBlockAlignment);
    const usize size = end - ToAddress(start);

    Detail::FreeListPool* const header = ConstructAt<Detail::FreeListPool>(start);
    header->next = m_pools;
    header->size = size;
    m_pools = header;

    // One free block spans the pool, followed by the zero-size sentinel.
    const usize payload = size - sizeof(Detail::FreeListPool) - 2 * kHeaderSize;
    Block* const first = MakeBlock(start + sizeof(Detail::FreeListPool), payload);
    MakeBlock(PayloadOf(first) + payload, 0);
    MarkFree(first);
    InsertFree(first);
    m_counters.AddReserved(pool.Size());
}

Result<void*, Error> FreeListAllocator::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0 && IsPowerOfTwo(alignment), "Allocation needs a non-zero size and a power-of-two alignment.");
    if (size > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }
    const usize payloadSize = PayloadSizeFor(size);
    // A stricter alignment may need a gap in front of the block, large enough to become a free block itself.
    const usize gapAllowance = alignment > kBlockAlignment ? alignment + kHeaderSize + kMinimumPayload : 0;
    Block* block = FindFree(payloadSize + gapAllowance);
    if (block == nullptr) {
        return Error(MemoryError::CapacityExceeded);
    }
    RemoveFree(block);

    if (alignment > kBlockAlignment) {
        u8* const payload = PayloadOf(block);
        usize gap = ToAddress(AlignUp(payload, alignment)) - ToAddress(payload);
        if (gap != 0 && gap < kHeaderSize + kMinimumPayload) {
            gap = ToAddress(AlignUp(payload + kHeaderSize + kMinimumPayload, alignment)) - ToAddress(payload);
        }
        if (gap != 0) {
            // The gap stays free as a block of its own; its predecessor is in use, so it cannot be merged.
            Block* const aligned = MakeBlock(payload + gap - kHeaderSize, SizeOf(block) - gap);
            SetSize(block, gap - kHeaderSize);
            MarkFree(block);
            InsertFree(block);
            block = aligned;
        }
    }

    SplitTail(block, payloadSize);
    MarkUsed(block);
    m_counters.OnAllocate(size);
    return static_cast<void*>(PayloadOf(block));
}

Result<void*, Error> FreeListAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    NX_ASSERT(Owns(block) && newSize != 0, "Reallocate needs a block from this allocator and a non-zero size.");
    if (newSize > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }
    Block* const header = FromPayload(block);
    NX_ASSERT(!HasFlag(header, kFreeFlag) && oldSize <= SizeOf(header), "Reallocate of a block that is not in use.");
    const usize payloadSize = PayloadSizeFor(newSize);

    if (payloadSize <= SizeOf(header)) {
        SplitTail(header, payloadSize);
        m_counters.OnResize(oldSize, newSize);
        return block;
    }
    Block* const next = NextOf(header);
    if (HasFlag(next, kFreeFlag) && SizeOf(header) + kHeaderSize + SizeOf(next) >= payloadSize) {
        RemoveFree(next);
        SetSize(header, SizeOf(header) + kHeaderSize + SizeOf(next));
        // The block after the absorbed neighbour now follows a block in use.
        SetFlag(NextOf(header), kPreviousFreeFlag, false);
        SplitTail(header, payloadSize);
        m_counters.OnResize(oldSize, newSize);
        return block;
    }

    NX_TRY_ASSIGN(void* const moved, TryAllocate(newSize, alignment));
    Copy(moved, block, oldSize < newSize ? oldSize : newSize);
    Deallocate(block, oldSize);
    return moved;
}

void FreeListAllocator::Deallocate(void* block, usize size) {
    if (block == nullptr) {
        return;
    }
    NX_ASSERT(Owns(block), "Block does not come from this FreeListAllocator.");
    Block* const header = FromPayload(block);
    NX_ASSERT(!HasFlag(header, kFreeFlag), "Block released twice.");
    NX_ASSERT(size <= SizeOf(header), "Released size exceeds the block.");
    m_counters.OnDeallocate(size);
    Release(header);
}

bool FreeListAllocator::Owns(const void* block) const {
    const usize address = ToAddress(block);
    for (const Detail::FreeListPool* pool = m_pools; pool != nullptr; pool = pool->next) {
        if (address >= ToAddress(pool) && address < ToAddress(pool) + pool->size) {
            return true;
        }
    }
    return false;
}

AllocatorStatistics FreeListAllocator::GetStatistics() const {
    return m_counters.Get();
}

void FreeListAllocator::InsertFree(Block* block) {
    const SizeClass sizeClass = Classify(SizeOf(block));
    Block*& head = m_freeLists[sizeClass.row][sizeClass.column];
    FreeLinks* const links = ConstructAt<FreeLinks>(PayloadOf(block));
    links->next = head;
    links->previous = nullptr;
    if (head != nullptr) {
        LinksOf(head)->previous = block;
    }
    head = block;
    m_firstLevelBitmap |= u64{1} << sizeClass.row;
    m_secondLevelBitmaps[sizeClass.row] |= 1U << sizeClass.column;
    // Everything after the links is untouchable until the block is handed out again.
    Platform::PoisonMemoryRegion(PayloadOf(block) + kMinimumPayload, SizeOf(block) - kMinimumPayload);
}

void FreeListAllocator::RemoveFree(Block* block) {
    Platform::UnpoisonMemoryRegion(PayloadOf(block), SizeOf(block));
    const SizeClass sizeClass = Classify(SizeOf(block));
    const FreeLinks* const links = LinksOf(block);
    if (links->previous != nullptr) {
        LinksOf(links->previous)->next = links->next;
    }
    if (links->next != nullptr) {
        LinksOf(links->next)->previous = links->previous;
    }
    Block*& head = m_freeLists[sizeClass.row][sizeClass.column];
    if (head == block) {
        head = links->next;
        if (head == nullptr) {
            m_secondLevelBitmaps[sizeClass.row] &= ~(1U << sizeClass.column);
            if (m_secondLevelBitmaps[sizeClass.row] == 0) {
                m_firstLevelBitmap &= ~(u64{1} << sizeClass.row);
            }
        }
    }
}

Block* FreeListAllocator::FindFree(usize payloadSize) const {
    const usize rounded = RoundUpToClass(payloadSize);
    if (rounded > kMaxAllocationSize) {
        return nullptr;
    }
    SizeClass sizeClass = Classify(rounded);
    NX_ASSERT(sizeClass.row < kRows && sizeClass.column < kSubdivisions, "Size class out of range.");
    u32 columns = m_secondLevelBitmaps[sizeClass.row] & (~0U << sizeClass.column);
    if (columns == 0) {
        const u64 rows = sizeClass.row + 1 < kRows ? m_firstLevelBitmap & (~u64{0} << (sizeClass.row + 1)) : 0;
        if (rows == 0) {
            return nullptr;
        }
        sizeClass.row = CountTrailingZeros(rows);
        // Only the bits of existing rows are ever set.
        NX_ASSERT(sizeClass.row < kRows, "First-level bitmap has a bit beyond the last row.");
        columns = m_secondLevelBitmaps[sizeClass.row];
    }
    sizeClass.column = CountTrailingZeros(columns);
    return m_freeLists[sizeClass.row][sizeClass.column];
}

void FreeListAllocator::SplitTail(Block* block, usize payloadSize) {
    const usize size = SizeOf(block);
    if (size < payloadSize + kHeaderSize + kMinimumPayload) {
        return;
    }
    Block* const rest = MakeBlock(PayloadOf(block) + payloadSize, size - payloadSize - kHeaderSize);
    SetSize(block, payloadSize);
    Release(rest);
}

void FreeListAllocator::Release(Block* block) {
    if (HasFlag(block, kPreviousFreeFlag)) {
        Block* const previous = block->previousPhysical;
        NX_ASSERT(previous != nullptr, "A free predecessor must be linked.");
        RemoveFree(previous);
        SetSize(previous, SizeOf(previous) + kHeaderSize + SizeOf(block));
        block = previous;
    }
    Block* const next = NextOf(block);
    if (HasFlag(next, kFreeFlag)) {
        RemoveFree(next);
        SetSize(block, SizeOf(block) + kHeaderSize + SizeOf(next));
    }
    MarkFree(block);
    InsertFree(block);
}

} // namespace Nexyl::Memory
