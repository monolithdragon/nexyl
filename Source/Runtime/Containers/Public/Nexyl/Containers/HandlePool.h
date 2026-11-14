#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Containers {

/// Owns objects and hands out Handles to them (docs/CODING_STANDARDS.md, section 6.1).
///
/// Objects live in chunks of 64 slots that never move, so a pointer from Get stays valid until its object is
/// removed. Each slot counts a generation that changes whenever its object is removed; a handle carries the
/// generation it was made with, so a handle to a removed object is detected as stale instead of reaching a newer
/// object in the same slot. A slot whose generation would wrap around is retired for good. Freed slots are
/// reused last-in, first-out, and iteration visits the live objects in slot order, so both are deterministic.
///
/// ```cpp
/// HandlePool<Texture> textures(allocator);
/// const Handle<Texture> handle = textures.Add(width, height);
/// if (Texture* texture = textures.Get(handle)) { texture->Upload(); }
/// textures.Remove(handle);
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename T>
class HandlePool final {
public:
    /// Slots per chunk.
    /// @since 0.1
    static constexpr usize kSlotsPerChunk = 64;

    /// One live object as iteration presents it.
    /// @since 0.1
    struct Entry {
        /// The object's handle.
        Handle<T> handle;
        /// The object.
        T& value;
    };

private:
    static constexpr u32 kNoSlot = 0xFFFF'FFFFU;

    // A slot holds an object while its generation is odd. The union is tagged by that parity.
    struct Slot {
        Slot() : unused(0) {}
        ~Slot() {}
        Slot(const Slot&) = delete;
        Slot& operator=(const Slot&) = delete;
        Slot(Slot&&) = delete;
        Slot& operator=(Slot&&) = delete;

        union {
            u8 unused;
            T value;
        };
        u32 generation = 0;
        u32 nextFree = kNoSlot;
    };

    struct Chunk {
        Slot slots[kSlotsPerChunk];
    };

public:
    /// Iterates the live objects in slot order. @since 0.1
    class Iterator final {
    public:
        /// Creates an iterator at the first live slot from `index`. @since 0.1
        Iterator(HandlePool* pool, u32 index) : m_pool(pool), m_index(index) {
            SkipFree();
        }
        /// Returns the object and its handle. @since 0.1
        [[nodiscard]] Entry operator*() const {
            Slot& slot = m_pool->SlotAt(m_index);
            return Entry{Handle<T>{m_index, slot.generation}, slot.value};
        }
        /// Moves to the next live object. @since 0.1
        Iterator& operator++() {
            ++m_index;
            SkipFree();
            return *this;
        }
        /// Compares positions. @since 0.1
        [[nodiscard]] bool operator==(const Iterator& other) const {
            return m_index == other.m_index;
        }

    private:
        void SkipFree() {
            while (m_index < m_pool->m_slotCount && (m_pool->SlotAt(m_index).generation & 1U) == 0) {
                ++m_index;
            }
        }

        HandlePool* m_pool = nullptr;
        u32 m_index = 0;
    };

    /// Creates an empty pool without an allocator. @since 0.1
    HandlePool() = default;

    /// Creates an empty pool. It takes no memory until the first object is added.
    /// @param allocator Supplies the memory. It must outlive the pool.
    /// @since 0.1
    explicit HandlePool(Memory::IAllocator& allocator) : m_chunks(allocator) {}

    /// Takes over the objects of `other`, which becomes empty and keeps no allocator. Handles stay valid.
    /// @since 0.1
    HandlePool(HandlePool&& other) noexcept
        : m_chunks(::Nexyl::Move(other.m_chunks)), m_slotCount(Exchange(other.m_slotCount, 0U)),
          m_freeHead(Exchange(other.m_freeHead, kNoSlot)), m_size(Exchange(other.m_size, usize{0})) {}

    /// Destroys the current objects, then takes over those of `other`. @since 0.1
    HandlePool& operator=(HandlePool&& other) noexcept {
        if (this != &other) {
            Release();
            m_chunks = ::Nexyl::Move(other.m_chunks);
            m_slotCount = Exchange(other.m_slotCount, 0U);
            m_freeHead = Exchange(other.m_freeHead, kNoSlot);
            m_size = Exchange(other.m_size, usize{0});
        }
        return *this;
    }

    /// Destroys every object and releases the memory. @since 0.1
    ~HandlePool() {
        Release();
    }

    HandlePool(const HandlePool&) = delete;
    HandlePool& operator=(const HandlePool&) = delete;

    /// Copies the pool, objects, generations and free slots included, into memory from `allocator`. Handles to
    /// this pool are valid for the copy. @param allocator For the copy. @return The copy. @since 0.1
    [[nodiscard]] HandlePool Clone(Memory::IAllocator& allocator) const
        requires Cloneable<T>
    {
        HandlePool copy(allocator);
        copy.m_chunks.Reserve(m_chunks.Size());
        for (usize chunk = 0; chunk < m_chunks.Size(); ++chunk) {
            copy.m_chunks.Push(Memory::New<Chunk>(allocator));
        }
        for (u32 index = 0; index < m_slotCount; ++index) {
            const Slot& source = SlotAt(index);
            Slot& target = copy.SlotAt(index);
            if ((source.generation & 1U) != 0) {
                ConstructAt<T>(&target.value, Detail::CloneValue(source.value, allocator));
            }
            target.generation = source.generation;
            target.nextFree = source.nextFree;
        }
        copy.m_slotCount = m_slotCount;
        copy.m_freeHead = m_freeHead;
        copy.m_size = m_size;
        return copy;
    }

    /// Returns the number of live objects. @since 0.1
    [[nodiscard]] usize Size() const {
        return m_size;
    }

    /// Checks whether the pool has no live objects. @since 0.1
    [[nodiscard]] bool IsEmpty() const {
        return m_size == 0;
    }

    /// Returns the allocator, or null for a pool that has none. @since 0.1
    [[nodiscard]] Memory::IAllocator* GetAllocator() const {
        return m_chunks.GetAllocator();
    }

    /// Creates an object from `arguments` and returns its handle. Terminates if the memory is not available.
    /// @since 0.1
    template <typename... TArgs>
    Handle<T> Add(TArgs&&... arguments) {
        u32 index = m_freeHead;
        if (index != kNoSlot) {
            m_freeHead = SlotAt(index).nextFree;
        } else {
            NX_ASSERT(m_slotCount < kNoSlot, "A HandlePool holds at most 2^32 - 1 slots.");
            if (m_slotCount == m_chunks.Size() * kSlotsPerChunk) {
                NX_ASSERT(GetAllocator() != nullptr, "A HandlePool needs an allocator before it can hold objects.");
                m_chunks.Push(Memory::New<Chunk>(*GetAllocator()));
            }
            index = m_slotCount++;
        }
        Slot& slot = SlotAt(index);
        ConstructAt<T>(&slot.value, ::Nexyl::Forward<TArgs>(arguments)...);
        ++slot.generation;
        ++m_size;
        return Handle<T>{index, slot.generation};
    }

    /// Destroys the object of a handle. @return False if the handle is null or stale; the pool is unchanged then.
    /// @since 0.1
    bool Remove(Handle<T> handle) {
        if (!Contains(handle)) {
            return false;
        }
        Free(handle.index);
        return true;
    }

    /// Returns the object of a handle. @return The object, or null if the handle is null or stale. @since 0.1
    [[nodiscard]] T* Get(Handle<T> handle) {
        return Contains(handle) ? &SlotAt(handle.index).value : nullptr;
    }

    /// Returns the object of a handle. @return The object, or null if the handle is null or stale. @since 0.1
    [[nodiscard]] const T* Get(Handle<T> handle) const {
        return Contains(handle) ? &SlotAt(handle.index).value : nullptr;
    }

    /// Checks whether a handle refers to a live object of this pool. @since 0.1
    [[nodiscard]] bool Contains(Handle<T> handle) const {
        return !handle.IsNull() && handle.index < m_slotCount && SlotAt(handle.index).generation == handle.generation;
    }

    /// Destroys every object; every handle becomes stale. Keeps the memory. @since 0.1
    void Clear() {
        m_freeHead = kNoSlot;
        // From the last slot to the first, so that the lowest free slot is reused first.
        for (u32 index = m_slotCount; index > 0; --index) {
            Slot& slot = SlotAt(index - 1);
            if ((slot.generation & 1U) != 0) {
                DestroyAt(&slot.value);
                ++slot.generation;
                --m_size;
            }
            // Every used slot has a non-zero generation, except one retired after its generation wrapped.
            if (slot.generation != 0) {
                slot.nextFree = m_freeHead;
                m_freeHead = index - 1;
            }
        }
    }

    /// Iterator to the first live object. @since 0.1
    [[nodiscard]] Iterator begin() {
        return Iterator(this, 0);
    }

    /// Iterator past the last slot. @since 0.1
    [[nodiscard]] Iterator end() {
        return Iterator(this, m_slotCount);
    }

private:
    [[nodiscard]] Slot& SlotAt(u32 index) {
        return m_chunks[index / kSlotsPerChunk]->slots[index % kSlotsPerChunk];
    }

    [[nodiscard]] const Slot& SlotAt(u32 index) const {
        return m_chunks[index / kSlotsPerChunk]->slots[index % kSlotsPerChunk];
    }

    void Free(u32 index) {
        Slot& slot = SlotAt(index);
        DestroyAt(&slot.value);
        ++slot.generation;
        --m_size;
        // A generation that wrapped to zero could make an old handle match again: retire the slot instead.
        if (slot.generation != 0) {
            slot.nextFree = m_freeHead;
            m_freeHead = index;
        }
    }

    void Release() {
        for (u32 index = 0; index < m_slotCount; ++index) {
            Slot& slot = SlotAt(index);
            if ((slot.generation & 1U) != 0) {
                DestroyAt(&slot.value);
            }
        }
        for (Chunk* const chunk : m_chunks) {
            Memory::Delete(*GetAllocator(), chunk);
        }
        m_chunks.Clear();
        m_slotCount = 0;
        m_freeHead = kNoSlot;
        m_size = 0;
    }

    Array<Chunk*> m_chunks;
    // Slots ever used; slots beyond this have never held an object.
    u32 m_slotCount = 0;
    u32 m_freeHead = kNoSlot;
    usize m_size = 0;
};

} // namespace Nexyl::Containers
