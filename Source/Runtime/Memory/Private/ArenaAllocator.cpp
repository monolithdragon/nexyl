#include <Nexyl/Memory/ArenaAllocator.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Bits.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Memory/Alignment.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/Sanitizer.h>

#include "BumpRegion.h"

namespace Nexyl::Memory {

// Sits at the start of every block taken from the parent.
struct ArenaAllocator::Block {
    Block* previous = nullptr;
    // Size of the whole block, header included, as requested from the parent.
    usize size = 0;
};

namespace {

// Blocks are aligned at least this much, so the header is followed by 16-byte-aligned space.
constexpr usize kBlockAlignment = 16;

} // namespace

ArenaAllocator::ArenaAllocator(IAllocator& parent, usize blockSize) : m_parent(&parent), m_blockSize(blockSize) {
    NX_ASSERT(blockSize > sizeof(Block), "An arena block must have room after its header.");
}

ArenaAllocator::~ArenaAllocator() {
    Release();
}

Result<void*, Error> ArenaAllocator::TryAllocate(usize size, usize alignment) {
    NX_ASSERT(size != 0 && IsPowerOfTwo(alignment), "Allocation needs a non-zero size and a power-of-two alignment.");
    if (size > kMaxAllocationSize) {
        return Error(MemoryError::OutOfMemory);
    }

    Option<usize> start;
    if (m_current != nullptr) {
        start = Detail::FitBlock(reinterpret_cast<u8*>(m_current), m_current->size, m_used, size, alignment);
    }
    if (!start.HasValue()) {
        // A request that does not fit the standard block gets a block of its own; the rest of the current block
        // is left unused until the next reset.
        const usize blockAlignment = alignment > kBlockAlignment ? alignment : kBlockAlignment;
        const usize needed = AlignUp(sizeof(Block), blockAlignment) + size;
        const usize blockSize = needed > m_blockSize ? needed : m_blockSize;
        NX_TRY_ASSIGN(void* const memory, m_parent->TryAllocate(blockSize, blockAlignment));

        Block* const block = ConstructAt<Block>(memory);
        block->previous = m_current;
        block->size = blockSize;
        Platform::PoisonMemoryRegion(reinterpret_cast<u8*>(block) + sizeof(Block), blockSize - sizeof(Block));
        m_current = block;
        m_used = sizeof(Block);
        m_counters.AddReserved(blockSize);
        start = Detail::FitBlock(reinterpret_cast<u8*>(m_current), m_current->size, m_used, size, alignment);
        NX_ASSERT(start.HasValue(), "A new arena block is too small for the request it was made for.");
    }

    u8* const result = reinterpret_cast<u8*>(m_current) + start.GetValue();
    Platform::UnpoisonMemoryRegion(result, size);
    m_lastBlockOffset = start.GetValue();
    m_hasLastBlock = true;
    m_used = start.GetValue() + size;
    m_counters.OnAllocate(size);
    return static_cast<void*>(result);
}

Result<void*, Error> ArenaAllocator::TryReallocate(void* block, usize oldSize, usize newSize, usize alignment) {
    NX_ASSERT(block != nullptr && newSize != 0, "Reallocate needs a block and a non-zero size.");
    const u8* const bytes = static_cast<const u8*>(block);
    if (m_hasLastBlock && bytes == reinterpret_cast<const u8*>(m_current) + m_lastBlockOffset &&
        newSize <= m_current->size - m_lastBlockOffset) {
        if (newSize < oldSize) {
            Platform::PoisonMemoryRegion(bytes + newSize, oldSize - newSize);
        } else {
            Platform::UnpoisonMemoryRegion(bytes + oldSize, newSize - oldSize);
        }
        m_used = m_lastBlockOffset + newSize;
        m_counters.OnResize(oldSize, newSize);
        return block;
    }
    NX_TRY_ASSIGN(void* const moved, TryAllocate(newSize, alignment));
    Copy(moved, block, oldSize < newSize ? oldSize : newSize);
    return moved;
}

void ArenaAllocator::Deallocate(void* block, usize /*size*/) {
    NX_ASSERT(block == nullptr || Owns(block), "Block does not come from this ArenaAllocator.");
}

bool ArenaAllocator::Owns(const void* block) const {
    const usize address = ToAddress(block);
    for (const Block* current = m_current; current != nullptr; current = current->previous) {
        const usize start = ToAddress(current) + sizeof(Block);
        if (address >= start && address < ToAddress(current) + current->size) {
            return true;
        }
    }
    return false;
}

AllocatorStatistics ArenaAllocator::GetStatistics() const {
    return m_counters.Get();
}

void ArenaAllocator::Reset() {
    if (m_current == nullptr) {
        return;
    }
    Block* older = m_current->previous;
    while (older != nullptr) {
        Block* const previous = older->previous;
        ReturnBlock(older);
        older = previous;
    }
    m_current->previous = nullptr;
    Platform::PoisonMemoryRegion(reinterpret_cast<u8*>(m_current) + sizeof(Block), m_current->size - sizeof(Block));
    m_used = sizeof(Block);
    m_hasLastBlock = false;
    m_counters.OnReset();
}

void ArenaAllocator::Release() {
    while (m_current != nullptr) {
        Block* const previous = m_current->previous;
        ReturnBlock(m_current);
        m_current = previous;
    }
    m_used = 0;
    m_hasLastBlock = false;
    m_counters.OnReset();
}

void ArenaAllocator::ReturnBlock(Block* block) {
    const usize size = block->size;
    // The parent may hand this memory to anyone, so the sanitizer must see all of it as usable again.
    Platform::UnpoisonMemoryRegion(block, size);
    m_counters.RemoveReserved(size);
    m_parent->Deallocate(block, size);
}

} // namespace Nexyl::Memory
