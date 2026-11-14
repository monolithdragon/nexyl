#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Hash.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Containers {

/// Satisfied when `TQuery` can look up keys of type `TKey`: it hashes to the same value as an equal key and
/// compares with one, as StringView does with String.
/// @since 0.1
template <typename TQuery, typename TKey>
concept KeyQuery = requires(const TQuery& query, const TKey& key) {
    { HashValue(query) } -> SameAs<u64>;
    { key == query } -> ConvertibleTo<bool>;
};

/// Maps unique keys to values, with entries stored densely in insertion order (docs/NEXYL_ARCHITECTURE.md,
/// section 7.7).
///
/// Iteration visits the entries in the order they were inserted, so it is the same on every run and platform
/// (rule DET-7). Remove takes constant time and moves the last entry into the gap, which changes the order in a
/// deterministic way; RemoveOrdered keeps the order and takes linear time (decision D-32). Lookup uses the key's
/// HashValue function with the fixed seed. Like Array, the map takes all memory from the allocator it was created
/// with, and copies are explicit, with Clone.
///
/// Pointers to values stay valid only until the next insertion or removal.
///
/// ```cpp
/// HashMap<String, u32> scores(allocator);
/// scores.Set(String(allocator, "ada"), 3);
/// if (const u32* score = scores.Find(StringView("ada"))) { Use(*score); }
/// for (const auto [name, score] : scores) { Print(name, score); }
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename TKey, typename TValue>
class HashMap final {
    static_assert(HashableKey<TKey>, "HashMap keys need a HashValue function and operator==.");

public:
    /// One entry as iteration presents it. The key is read-only, because changing it would break the index.
    /// @since 0.1
    struct Entry {
        /// The key.
        const TKey& key;
        /// The value.
        TValue& value;
    };

    /// One entry of a const map.
    /// @since 0.1
    struct ConstEntry {
        /// The key.
        const TKey& key;
        /// The value.
        const TValue& value;
    };

private:
    struct Stored {
        TKey key;
        TValue value;
        u64 hash = 0;
    };

    // One slot of the open-addressing index: the entry's position plus one (zero marks an empty slot), and the
    // high half of its hash, which rules out most mismatches without comparing keys.
    struct Slot {
        u32 entry = 0;
        u32 tag = 0;
    };

public:
    /// Iterates the entries in order. @since 0.1
    class Iterator final {
    public:
        /// Creates an iterator at `entry`. @since 0.1
        explicit Iterator(Stored* entry) : m_entry(entry) {}
        /// Returns the entry. @since 0.1
        [[nodiscard]] Entry operator*() const {
            return Entry{m_entry->key, m_entry->value};
        }
        /// Moves to the next entry. @since 0.1
        Iterator& operator++() {
            ++m_entry;
            return *this;
        }
        /// Compares positions. @since 0.1
        [[nodiscard]] bool operator==(const Iterator&) const = default;

    private:
        Stored* m_entry = nullptr;
    };

    /// Iterates the entries of a const map in order. @since 0.1
    class ConstIterator final {
    public:
        /// Creates an iterator at `entry`. @since 0.1
        explicit ConstIterator(const Stored* entry) : m_entry(entry) {}
        /// Returns the entry. @since 0.1
        [[nodiscard]] ConstEntry operator*() const {
            return ConstEntry{m_entry->key, m_entry->value};
        }
        /// Moves to the next entry. @since 0.1
        ConstIterator& operator++() {
            ++m_entry;
            return *this;
        }
        /// Compares positions. @since 0.1
        [[nodiscard]] bool operator==(const ConstIterator&) const = default;

    private:
        const Stored* m_entry = nullptr;
    };

    /// Creates an empty map without an allocator. @since 0.1
    HashMap() = default;

    /// Creates an empty map. It takes no memory until the first insertion.
    /// @param allocator Supplies the memory. It must outlive the map.
    /// @since 0.1
    explicit HashMap(Memory::IAllocator& allocator) : m_entries(allocator), m_slots(allocator) {}

    /// Takes over the entries of `other`, which becomes empty and keeps no allocator. @since 0.1
    HashMap(HashMap&& other) noexcept = default;
    /// Releases the current entries, then takes over those of `other`. @since 0.1
    HashMap& operator=(HashMap&& other) noexcept = default;
    /// Destroys the entries. @since 0.1
    ~HashMap() = default;

    HashMap(const HashMap&) = delete;
    HashMap& operator=(const HashMap&) = delete;

    /// Copies the map into memory from the same allocator. @return The copy. @since 0.1
    [[nodiscard]] HashMap Clone() const
        requires(Cloneable<TKey> && Cloneable<TValue>);

    /// Copies the map into memory from `allocator`; keys and values with a Clone function are cloned into it too.
    /// @param allocator For the copy.
    /// @return The copy.
    /// @since 0.1
    [[nodiscard]] HashMap Clone(Memory::IAllocator& allocator) const
        requires(Cloneable<TKey> && Cloneable<TValue>);

    /// Returns the number of entries. @since 0.1
    [[nodiscard]] usize Size() const {
        return m_entries.Size();
    }

    /// Checks whether the map has no entries. @since 0.1
    [[nodiscard]] bool IsEmpty() const {
        return m_entries.IsEmpty();
    }

    /// Returns the allocator, or null for a map that has none. @since 0.1
    [[nodiscard]] Memory::IAllocator* GetAllocator() const {
        return m_entries.GetAllocator();
    }

    /// Makes room for `count` entries, terminating if the memory is not available. @since 0.1
    void Reserve(usize count);

    /// Makes room for `count` entries.
    /// @param count The number of entries.
    /// @error OutOfMemory, CapacityExceeded The allocator cannot provide the memory.
    /// @since 0.1
    Result<void, Error> TryReserve(usize count);

    /// Finds the value of a key. @return The value, or null if the key is absent. @since 0.1
    template <KeyQuery<TKey> TQuery>
    [[nodiscard]] TValue* Find(const TQuery& key);

    /// Finds the value of a key. @return The value, or null if the key is absent. @since 0.1
    template <KeyQuery<TKey> TQuery>
    [[nodiscard]] const TValue* Find(const TQuery& key) const;

    /// Checks whether a key is present. @since 0.1
    template <KeyQuery<TKey> TQuery>
    [[nodiscard]] bool Contains(const TQuery& key) const {
        return Find(key) != nullptr;
    }

    /// Adds an entry if the key is absent; otherwise leaves the map unchanged.
    /// @param key   The key.
    /// @param value The value.
    /// @return True if the entry was added.
    /// @since 0.1
    bool Insert(TKey key, TValue value);

    /// Adds an entry, or replaces the value if the key is present.
    /// @param key   The key.
    /// @param value The value.
    /// @return The stored value.
    /// @since 0.1
    TValue& Set(TKey key, TValue value);

    /// Returns the value of a key, adding a value-initialised entry if the key is absent. @since 0.1
    TValue& FindOrAdd(TKey key)
        requires ConstructibleFrom<TValue>;

    /// Removes a key in constant time. The last entry moves into its place in the iteration order.
    /// @return True if the key was present.
    /// @since 0.1
    template <KeyQuery<TKey> TQuery>
    bool Remove(const TQuery& key);

    /// Removes a key and keeps the order of the other entries. Takes time linear in the size of the map.
    /// @return True if the key was present.
    /// @since 0.1
    template <KeyQuery<TKey> TQuery>
    bool RemoveOrdered(const TQuery& key);

    /// Removes every entry and keeps the memory. @since 0.1
    void Clear();

    /// Returns the entry at a position in iteration order. @pre `index` < Size(). @since 0.1
    [[nodiscard]] Entry GetEntry(usize index) {
        Stored& stored = m_entries[index];
        return Entry{stored.key, stored.value};
    }

    /// Returns the entry at a position in iteration order. @pre `index` < Size(). @since 0.1
    [[nodiscard]] ConstEntry GetEntry(usize index) const {
        const Stored& stored = m_entries[index];
        return ConstEntry{stored.key, stored.value};
    }

    /// Iterator to the first entry. @since 0.1
    [[nodiscard]] Iterator begin() {
        return Iterator(m_entries.begin());
    }

    /// Iterator past the last entry. @since 0.1
    [[nodiscard]] Iterator end() {
        return Iterator(m_entries.end());
    }

    /// Iterator to the first entry. @since 0.1
    [[nodiscard]] ConstIterator begin() const {
        return ConstIterator(m_entries.begin());
    }

    /// Iterator past the last entry. @since 0.1
    [[nodiscard]] ConstIterator end() const {
        return ConstIterator(m_entries.end());
    }

private:
    [[nodiscard]] static u32 TagOf(u64 hash) {
        return static_cast<u32>(hash >> 32U);
    }

    [[nodiscard]] usize Mask() const {
        return m_slots.Size() - 1;
    }

    template <typename TQuery>
    [[nodiscard]] Option<usize> FindSlot(const TQuery& key, u64 hash) const;
    [[nodiscard]] usize SlotOfEntry(usize entry) const;
    void PlaceSlot(usize entry, u64 hash);
    void EraseSlot(usize slot);
    Result<void, Error> TryGrowSlots(usize entryCount);
    void GrowSlots(usize entryCount);
    TValue& Add(TKey key, TValue value, u64 hash);

    Array<Stored> m_entries;
    // Size is zero or a power of two; at most three quarters of the slots are used.
    Array<Slot> m_slots;
};

} // namespace Nexyl::Containers

#include <Nexyl/Containers/HashMap.inl>
