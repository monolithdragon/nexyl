#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/InitializerList.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Clone.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Containers {

/// A growable sequence of `T` in one contiguous block of memory (docs/CODING_STANDARDS.md, section 6.1).
///
/// The array stores the allocator it was created with (decision D-15) and takes all its memory from it. A
/// default-constructed array has no allocator and must receive one, by construction or move assignment, before
/// it holds elements: there is no global default allocator (rules OWN-2, INIT-3). Growing terminates through the
/// out-of-memory handler when memory runs out; TryReserve reports the failure instead. Copies are explicit, with
/// Clone (rule OWN-7). An array converts to a Span implicitly.
///
/// Growing moves the elements, so pointers and references to them stay valid only until the next operation that
/// adds or removes elements. Iteration is in index order, which makes it deterministic (rule DET-7).
///
/// ```cpp
/// Array<u32> ids(allocator);
/// ids.Push(7);
/// for (const u32 id : ids) { Use(id); }
/// ```
/// @threadsafety compatible
/// @since 0.1
template <typename T>
class Array final {
    static_assert(!LvalueReference<T>, "Array does not hold references. Use a pointer or a handle.");
    static_assert(MoveConstructible<T>, "Array elements must be movable.");

public:
    /// Creates an empty array without an allocator. @since 0.1
    constexpr Array() = default;

    /// Creates an empty array. It takes no memory until it grows.
    /// @param allocator Supplies the memory. It must outlive the array.
    /// @since 0.1
    explicit Array(Memory::IAllocator& allocator) : m_allocator(&allocator) {}

    /// Creates an array holding copies of `values`.
    /// @param allocator Supplies the memory. It must outlive the array.
    /// @param values    The elements.
    /// @since 0.1
    Array(Memory::IAllocator& allocator, InitializerList<T> values)
        requires CopyConstructible<T>;

    /// Takes over the elements and the allocator of `other`, which becomes empty and keeps no allocator. @since 0.1
    Array(Array&& other) noexcept;

    /// Releases the current elements, then takes over those of `other`, like the move constructor. @since 0.1
    Array& operator=(Array&& other) noexcept;

    /// Destroys the elements and releases the memory. @since 0.1
    ~Array();

    Array(const Array&) = delete;
    Array& operator=(const Array&) = delete;

    /// Copies the array into memory from the same allocator. @return The copy. @since 0.1
    [[nodiscard]] Array Clone() const
        requires Cloneable<T>;

    /// Copies the array into memory from `allocator`; elements with a Clone function are cloned into it too.
    /// @param allocator For the copy.
    /// @return The copy.
    /// @since 0.1
    [[nodiscard]] Array Clone(Memory::IAllocator& allocator) const
        requires Cloneable<T>;

    /// Returns the first element, or null if no memory is held. @since 0.1
    [[nodiscard]] T* Data() {
        return m_data;
    }

    /// Returns the first element, or null if no memory is held. @since 0.1
    [[nodiscard]] const T* Data() const {
        return m_data;
    }

    /// Returns the number of elements. @since 0.1
    [[nodiscard]] usize Size() const {
        return m_size;
    }

    /// Returns how many elements fit before the array has to grow. @since 0.1
    [[nodiscard]] usize Capacity() const {
        return m_capacity;
    }

    /// Checks whether the array has no elements. @since 0.1
    [[nodiscard]] bool IsEmpty() const {
        return m_size == 0;
    }

    /// Returns the allocator, or null for an array that has none. @since 0.1
    [[nodiscard]] Memory::IAllocator* GetAllocator() const {
        return m_allocator;
    }

    /// Returns the element at `index`. @pre `index` < Size(). @since 0.1
    [[nodiscard]] T& operator[](usize index) {
        NX_ASSERT(index < m_size, "Array index out of range.");
        return m_data[index];
    }

    /// Returns the element at `index`. @pre `index` < Size(). @since 0.1
    [[nodiscard]] const T& operator[](usize index) const {
        NX_ASSERT(index < m_size, "Array index out of range.");
        return m_data[index];
    }

    /// Returns the first element. @pre The array is not empty. @since 0.1
    [[nodiscard]] T& Front() {
        return (*this)[0];
    }

    /// Returns the first element. @pre The array is not empty. @since 0.1
    [[nodiscard]] const T& Front() const {
        return (*this)[0];
    }

    /// Returns the last element. @pre The array is not empty. @since 0.1
    [[nodiscard]] T& Back() {
        NX_ASSERT(m_size != 0, "Back of an empty Array.");
        return m_data[m_size - 1];
    }

    /// Returns the last element. @pre The array is not empty. @since 0.1
    [[nodiscard]] const T& Back() const {
        NX_ASSERT(m_size != 0, "Back of an empty Array.");
        return m_data[m_size - 1];
    }

    /// Makes room for at least `capacity` elements, terminating if the memory is not available. @since 0.1
    void Reserve(usize capacity);

    /// Makes room for at least `capacity` elements.
    /// @param capacity The number of elements.
    /// @error OutOfMemory, CapacityExceeded The allocator cannot provide the memory; the array is unchanged.
    /// @since 0.1
    Result<void, Error> TryReserve(usize capacity);

    /// Changes the number of elements; new elements are value-initialised. @param size The new size. @since 0.1
    void Resize(usize size)
        requires ConstructibleFrom<T>;

    /// Changes the number of elements; new elements are copies of `value`. @since 0.1
    void Resize(usize size, const T& value)
        requires CopyConstructible<T>;

    /// Adds a copy of `value` at the end. `value` may be an element of this array. @return The new element.
    /// @since 0.1
    T& Push(const T& value)
        requires CopyConstructible<T>;

    /// Moves `value` to the end. @return The new element. @since 0.1
    T& Push(T&& value);

    /// Constructs an element at the end from `arguments`, which may refer to elements of this array.
    /// @return The new element.
    /// @since 0.1
    template <typename... TArgs>
    T& Emplace(TArgs&&... arguments);

    /// Adds copies of `values` at the end. `values` may view this array. @since 0.1
    void Append(Span<const T> values)
        requires CopyConstructible<T>;

    /// Removes the last element and returns it. @pre The array is not empty. @since 0.1
    T Pop();

    /// Inserts `value` before the element at `index`, shifting the later elements.
    /// @pre `index` <= Size().
    /// @since 0.1
    void Insert(usize index, T value);

    /// Removes the element at `index` and shifts the later elements, keeping their order. O(n).
    /// @pre `index` < Size().
    /// @since 0.1
    void RemoveAt(usize index);

    /// Removes the element at `index` by moving the last element into its place. O(1); changes the order.
    /// @pre `index` < Size().
    /// @since 0.1
    void RemoveAtSwap(usize index);

    /// Destroys every element and keeps the memory. @since 0.1
    void Clear();

    /// Returns unused memory to the allocator, if the allocator can shrink the block. @since 0.1
    void ShrinkToFit();

    /// Finds the first element equal to `value`. @return Its index, or kNone. @since 0.1
    template <typename U>
    [[nodiscard]] Option<usize> IndexOf(const U& value) const;

    /// Checks whether an element equals `value`. @since 0.1
    template <typename U>
    [[nodiscard]] bool Contains(const U& value) const {
        return IndexOf(value).HasValue();
    }

    /// Checks whether both arrays hold equal elements in the same order. @since 0.1
    [[nodiscard]] bool operator==(const Array& other) const;

    /// Pointer to the first element, for range-for. @since 0.1
    [[nodiscard]] T* begin() {
        return m_data;
    }

    /// Pointer past the last element, for range-for. @since 0.1
    [[nodiscard]] T* end() {
        return m_data + m_size;
    }

    /// Pointer to the first element, for range-for. @since 0.1
    [[nodiscard]] const T* begin() const {
        return m_data;
    }

    /// Pointer past the last element, for range-for. @since 0.1
    [[nodiscard]] const T* end() const {
        return m_data + m_size;
    }

private:
    [[nodiscard]] usize GrownCapacity(usize minimum) const;
    Result<void, Error> TryMoveTo(usize capacity);
    void GrowFor(usize minimum);
    void Release();

    T* m_data = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
    Memory::IAllocator* m_allocator = nullptr;
};

} // namespace Nexyl::Containers

#include <Nexyl/Containers/Array.inl>
