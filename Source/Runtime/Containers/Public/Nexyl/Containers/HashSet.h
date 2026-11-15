#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Containers {

namespace Detail {

/// The value type of the HashMap inside a HashSet. @since 0.1
struct SetMember {};

} // namespace Detail

/// A set of unique keys, stored densely in insertion order: a HashMap without values.
///
/// Iteration, removal and lookup behave as in HashMap: iteration follows insertion order, Remove moves the last
/// key into the gap, RemoveOrdered keeps the order (decision D-32).
///
/// ```cpp
/// HashSet<u32> visited(allocator);
/// if (visited.Add(nodeId)) { Explore(nodeId); }
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename TKey>
class HashSet final {
    using Map = HashMap<TKey, Detail::SetMember>;

public:
    /// Iterates the keys in order. @since 0.1
    class Iterator final {
    public:
        /// Creates an iterator over map entries. @since 0.1
        explicit Iterator(typename Map::ConstIterator entry) : m_entry(entry) {}
        /// Returns the key. @since 0.1
        [[nodiscard]] const TKey& operator*() const {
            return (*m_entry).key;
        }
        /// Moves to the next key. @since 0.1
        Iterator& operator++() {
            ++m_entry;
            return *this;
        }
        /// Compares positions. @since 0.1
        [[nodiscard]] bool operator==(const Iterator&) const = default;

    private:
        typename Map::ConstIterator m_entry;
    };

    /// Creates an empty set without an allocator. @since 0.1
    HashSet() = default;

    /// Creates an empty set. @param allocator Supplies the memory. It must outlive the set. @since 0.1
    explicit HashSet(Memory::IAllocator& allocator) : m_map(allocator) {}

    /// Takes over the keys of `other`, which becomes empty and keeps no allocator. @since 0.1
    HashSet(HashSet&& other) noexcept = default;
    /// Releases the current keys, then takes over those of `other`. @since 0.1
    HashSet& operator=(HashSet&& other) noexcept = default;
    /// Destroys the keys. @since 0.1
    ~HashSet() = default;

    HashSet(const HashSet&) = delete;
    HashSet& operator=(const HashSet&) = delete;

    /// Copies the set into memory from the same allocator. @return The copy. @since 0.1
    [[nodiscard]] HashSet Clone() const
        requires Cloneable<TKey>
    {
        return HashSet(m_map.Clone());
    }

    /// Copies the set into memory from `allocator`. @param allocator For the copy. @return The copy. @since 0.1
    [[nodiscard]] HashSet Clone(Memory::IAllocator& allocator) const
        requires Cloneable<TKey>
    {
        return HashSet(m_map.Clone(allocator));
    }

    /// Returns the number of keys. @since 0.1
    [[nodiscard]] usize Size() const {
        return m_map.Size();
    }

    /// Checks whether the set has no keys. @since 0.1
    [[nodiscard]] bool IsEmpty() const {
        return m_map.IsEmpty();
    }

    /// Returns the allocator, or null for a set that has none. @since 0.1
    [[nodiscard]] Memory::IAllocator* GetAllocator() const {
        return m_map.GetAllocator();
    }

    /// Makes room for `count` keys, terminating if the memory is not available. @since 0.1
    void Reserve(usize count) {
        m_map.Reserve(count);
    }

    /// Makes room for `count` keys. @error OutOfMemory, CapacityExceeded As for HashMap::TryReserve. @since 0.1
    Result<void, Error> TryReserve(usize count) {
        return m_map.TryReserve(count);
    }

    /// Adds a key if it is absent. @return True if it was added. @since 0.1
    bool Add(TKey key) {
        return m_map.Insert(::Nexyl::Move(key), Detail::SetMember{});
    }

    /// Checks whether a key is present. @since 0.1
    template <KeyQuery<TKey> TQuery>
    [[nodiscard]] bool Contains(const TQuery& key) const {
        return m_map.Contains(key);
    }

    /// Removes a key in constant time; the last key moves into its place. @return True if it was present.
    /// @since 0.1
    template <KeyQuery<TKey> TQuery>
    bool Remove(const TQuery& key) {
        return m_map.Remove(key);
    }

    /// Removes a key and keeps the order of the others; linear time. @return True if it was present. @since 0.1
    template <KeyQuery<TKey> TQuery>
    bool RemoveOrdered(const TQuery& key) {
        return m_map.RemoveOrdered(key);
    }

    /// Removes every key and keeps the memory. @since 0.1
    void Clear() {
        m_map.Clear();
    }

    /// Returns the key at a position in iteration order. @pre `index` < Size(). @since 0.1
    [[nodiscard]] const TKey& GetKey(usize index) const {
        return m_map.GetEntry(index).key;
    }

    /// Iterator to the first key. @since 0.1
    [[nodiscard]] Iterator begin() const {
        return Iterator(m_map.begin());
    }

    /// Iterator past the last key. @since 0.1
    [[nodiscard]] Iterator end() const {
        return Iterator(m_map.end());
    }

private:
    explicit HashSet(Map&& map) : m_map(::Nexyl::Move(map)) {}

    Map m_map;
};

} // namespace Nexyl::Containers
