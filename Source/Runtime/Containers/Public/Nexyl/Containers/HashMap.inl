#pragma once

// Definitions of HashMap's longer members. Included at the end of HashMap.h only (rule HDR-8).

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Allocation.h>

namespace Nexyl::Containers {

template <typename TKey, typename TValue>
HashMap<TKey, TValue> HashMap<TKey, TValue>::Clone() const
    requires(Cloneable<TKey> && Cloneable<TValue>)
{
    if (GetAllocator() == nullptr) {
        return HashMap();
    }
    return Clone(*GetAllocator());
}

template <typename TKey, typename TValue>
HashMap<TKey, TValue> HashMap<TKey, TValue>::Clone(Memory::IAllocator& allocator) const
    requires(Cloneable<TKey> && Cloneable<TValue>)
{
    HashMap copy(allocator);
    copy.m_entries.Reserve(m_entries.Size());
    for (const Stored& stored : m_entries) {
        copy.m_entries.Emplace(Stored{Detail::CloneValue(stored.key, allocator),
                                      Detail::CloneValue(stored.value, allocator), stored.hash});
    }
    // The positions are the same, so the index carries over unchanged.
    copy.m_slots = m_slots.Clone(allocator);
    return copy;
}

template <typename TKey, typename TValue>
void HashMap<TKey, TValue>::Reserve(usize count) {
    m_entries.Reserve(count);
    GrowSlots(count);
}

template <typename TKey, typename TValue>
Result<void, Error> HashMap<TKey, TValue>::TryReserve(usize count) {
    NX_TRY(m_entries.TryReserve(count));
    return TryGrowSlots(count);
}

template <typename TKey, typename TValue>
template <KeyQuery<TKey> TQuery>
TValue* HashMap<TKey, TValue>::Find(const TQuery& key) {
    if (m_entries.IsEmpty()) {
        return nullptr;
    }
    const Option<usize> slot = FindSlot(key, HashValue(key));
    return slot.HasValue() ? &m_entries[m_slots[slot.GetValue()].entry - 1].value : nullptr;
}

template <typename TKey, typename TValue>
template <KeyQuery<TKey> TQuery>
const TValue* HashMap<TKey, TValue>::Find(const TQuery& key) const {
    if (m_entries.IsEmpty()) {
        return nullptr;
    }
    const Option<usize> slot = FindSlot(key, HashValue(key));
    return slot.HasValue() ? &m_entries[m_slots[slot.GetValue()].entry - 1].value : nullptr;
}

template <typename TKey, typename TValue>
bool HashMap<TKey, TValue>::Insert(TKey key, TValue value) {
    const u64 hash = HashValue(key);
    if (!m_entries.IsEmpty() && FindSlot(key, hash).HasValue()) {
        return false;
    }
    Add(::Nexyl::Move(key), ::Nexyl::Move(value), hash);
    return true;
}

template <typename TKey, typename TValue>
TValue& HashMap<TKey, TValue>::Set(TKey key, TValue value) {
    const u64 hash = HashValue(key);
    if (!m_entries.IsEmpty()) {
        const Option<usize> slot = FindSlot(key, hash);
        if (slot.HasValue()) {
            TValue& stored = m_entries[m_slots[slot.GetValue()].entry - 1].value;
            stored = ::Nexyl::Move(value);
            return stored;
        }
    }
    return Add(::Nexyl::Move(key), ::Nexyl::Move(value), hash);
}

template <typename TKey, typename TValue>
TValue& HashMap<TKey, TValue>::FindOrAdd(TKey key)
    requires ConstructibleFrom<TValue>
{
    const u64 hash = HashValue(key);
    if (!m_entries.IsEmpty()) {
        const Option<usize> slot = FindSlot(key, hash);
        if (slot.HasValue()) {
            return m_entries[m_slots[slot.GetValue()].entry - 1].value;
        }
    }
    return Add(::Nexyl::Move(key), TValue(), hash);
}

template <typename TKey, typename TValue>
template <KeyQuery<TKey> TQuery>
bool HashMap<TKey, TValue>::Remove(const TQuery& key) {
    if (m_entries.IsEmpty()) {
        return false;
    }
    const Option<usize> slot = FindSlot(key, HashValue(key));
    if (!slot.HasValue()) {
        return false;
    }
    const usize entry = m_slots[slot.GetValue()].entry - 1;
    EraseSlot(slot.GetValue());
    const usize last = m_entries.Size() - 1;
    if (entry != last) {
        // The last entry moves into the gap; its slot follows it.
        m_slots[SlotOfEntry(last)].entry = static_cast<u32>(entry + 1);
    }
    m_entries.RemoveAtSwap(entry);
    return true;
}

template <typename TKey, typename TValue>
template <KeyQuery<TKey> TQuery>
bool HashMap<TKey, TValue>::RemoveOrdered(const TQuery& key) {
    if (m_entries.IsEmpty()) {
        return false;
    }
    const Option<usize> slot = FindSlot(key, HashValue(key));
    if (!slot.HasValue()) {
        return false;
    }
    const usize entry = m_slots[slot.GetValue()].entry - 1;
    EraseSlot(slot.GetValue());
    m_entries.RemoveAt(entry);
    // Every later entry moved one place forward.
    for (Slot& other : m_slots) {
        if (other.entry > entry + 1) {
            --other.entry;
        }
    }
    return true;
}

template <typename TKey, typename TValue>
void HashMap<TKey, TValue>::Clear() {
    m_entries.Clear();
    for (Slot& slot : m_slots) {
        slot = Slot{};
    }
}

template <typename TKey, typename TValue>
template <typename TQuery>
Option<usize> HashMap<TKey, TValue>::FindSlot(const TQuery& key, u64 hash) const {
    const usize mask = Mask();
    const u32 tag = TagOf(hash);
    for (usize slot = static_cast<usize>(hash) & mask;; slot = (slot + 1) & mask) {
        const Slot& candidate = m_slots[slot];
        if (candidate.entry == 0) {
            return kNone;
        }
        if (candidate.tag == tag && m_entries[candidate.entry - 1].key == key) {
            return slot;
        }
    }
}

template <typename TKey, typename TValue>
usize HashMap<TKey, TValue>::SlotOfEntry(usize entry) const {
    const usize mask = Mask();
    for (usize slot = static_cast<usize>(m_entries[entry].hash) & mask;; slot = (slot + 1) & mask) {
        if (m_slots[slot].entry == entry + 1) {
            return slot;
        }
        NX_ASSERT(m_slots[slot].entry != 0, "Every entry has a slot.");
    }
}

template <typename TKey, typename TValue>
void HashMap<TKey, TValue>::PlaceSlot(usize entry, u64 hash) {
    const usize mask = Mask();
    usize slot = static_cast<usize>(hash) & mask;
    while (m_slots[slot].entry != 0) {
        slot = (slot + 1) & mask;
    }
    m_slots[slot] = Slot{static_cast<u32>(entry + 1), TagOf(hash)};
}

template <typename TKey, typename TValue>
void HashMap<TKey, TValue>::EraseSlot(usize slot) {
    // Backward-shift deletion: slots after the hole move back if the hole lies between their home and their
    // current position, so a probe never meets a gap before its key. No tombstones are needed.
    const usize mask = Mask();
    usize hole = slot;
    for (usize next = (hole + 1) & mask; m_slots[next].entry != 0; next = (next + 1) & mask) {
        const usize home = static_cast<usize>(m_entries[m_slots[next].entry - 1].hash) & mask;
        if (((next - home) & mask) >= ((next - hole) & mask)) {
            m_slots[hole] = m_slots[next];
            hole = next;
        }
    }
    m_slots[hole] = Slot{};
}

template <typename TKey, typename TValue>
Result<void, Error> HashMap<TKey, TValue>::TryGrowSlots(usize entryCount) {
    constexpr usize kSmallestTable = 16;
    if (m_slots.Size() != 0 && entryCount <= m_slots.Size() / 4 * 3) {
        return {};
    }
    usize size = m_slots.Size() != 0 ? m_slots.Size() : kSmallestTable;
    while (entryCount > size / 4 * 3) {
        size *= 2;
    }
    if (size == m_slots.Size()) {
        return {};
    }
    NX_TRY(m_slots.TryReserve(size));
    m_slots.Clear();
    m_slots.Resize(size);
    for (usize entry = 0; entry < m_entries.Size(); ++entry) {
        PlaceSlot(entry, m_entries[entry].hash);
    }
    return {};
}

template <typename TKey, typename TValue>
void HashMap<TKey, TValue>::GrowSlots(usize entryCount) {
    const Result<void, Error> grown = TryGrowSlots(entryCount);
    if (grown.IsError()) [[unlikely]] {
        Memory::ReportOutOfMemory(
            Memory::OutOfMemoryInfo{GetAllocator(), entryCount * sizeof(Slot), alignof(Slot), grown.GetError()});
    }
}

template <typename TKey, typename TValue>
TValue& HashMap<TKey, TValue>::Add(TKey key, TValue value, u64 hash) {
    NX_ASSERT(m_entries.Size() < 0xFFFF'FFFEU, "A HashMap holds at most 2^32 - 2 entries.");
    GrowSlots(m_entries.Size() + 1);
    Stored& stored = m_entries.Emplace(Stored{::Nexyl::Move(key), ::Nexyl::Move(value), hash});
    PlaceSlot(m_entries.Size() - 1, hash);
    return stored.value;
}

} // namespace Nexyl::Containers
