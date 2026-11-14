#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// Satisfied by a contiguous container whose elements can be viewed as a `Span<T>`:
/// it has `Data()` returning a pointer convertible to `T*` and `Size()` returning the element count.
/// @since 0.1
template <typename TContainer, typename T>
concept ContiguousContainerOf = requires(TContainer& container) {
    { container.Data() } -> ConvertibleTo<T*>;
    { container.Size() } -> ConvertibleTo<usize>;
};

/// A non-owning view of a contiguous sequence of `T` (docs/CODING_STANDARDS.md, section 6.1).
///
/// A span does not own its elements. It is valid only while they are, and it is never stored beyond that
/// (rule OWN-9). Use `Span<const T>` for read-only access.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
class Span final {
public:
    /// Creates an empty span. @since 0.1
    constexpr Span() noexcept = default;

    /// Creates a span over `size` elements starting at `data`.
    /// @param data First element. May be null only when `size` is zero.
    /// @param size Number of elements.
    /// @since 0.1
    constexpr Span(T* data, usize size) noexcept : m_data(data), m_size(size) {
        NX_ASSERT(data != nullptr || size == 0, "A span with elements needs a data pointer.");
    }

    // The converting constructors below are implicit by design, like a reference: passing an array or a
    // container where a span is expected is the normal use. This is the documented exception to rule CLS-6.

    /// Creates a span over a built-in array. @since 0.1
    template <usize N>
    constexpr explicit(false) Span(T (&array)[N]) noexcept : m_data(array), m_size(N) {}

    /// Creates a read-only span from a mutable one. @since 0.1
    template <typename U>
        requires(!SameAs<U, T> && ConvertibleTo<U (*)[], T (*)[]>)
    constexpr explicit(false) Span(Span<U> other) noexcept : m_data(other.Data()), m_size(other.Size()) {}

    /// Creates a span over the elements of a contiguous container such as Array. @since 0.1
    template <typename TContainer>
        requires(ContiguousContainerOf<TContainer, T> && !SameAs<RemoveCvRef<TContainer>, Span>)
    constexpr explicit(false) Span(TContainer& container) noexcept
        : m_data(container.Data()), m_size(static_cast<usize>(container.Size())) {}

    /// Returns a pointer to the first element, or null for an empty span. @since 0.1
    [[nodiscard]] constexpr T* Data() const noexcept {
        return m_data;
    }

    /// Returns the number of elements. @since 0.1
    [[nodiscard]] constexpr usize Size() const noexcept {
        return m_size;
    }

    /// Returns the size of the viewed memory in bytes. @since 0.1
    [[nodiscard]] constexpr usize SizeInBytes() const noexcept {
        return m_size * sizeof(T);
    }

    /// Checks whether the span has no elements. @since 0.1
    [[nodiscard]] constexpr bool IsEmpty() const noexcept {
        return m_size == 0;
    }

    /// Returns the element at `index`.
    /// @param index Element index.
    /// @pre `index` < Size().
    /// @return A reference to the element.
    /// @since 0.1
    [[nodiscard]] constexpr T& operator[](usize index) const noexcept {
        NX_ASSERT(index < m_size, "Span index out of range.");
        return m_data[index];
    }

    /// Returns the first `count` elements.
    /// @param count Number of elements.
    /// @pre `count` <= Size().
    /// @return The sub-span.
    /// @since 0.1
    [[nodiscard]] constexpr Span First(usize count) const noexcept {
        NX_ASSERT(count <= m_size, "Span::First count out of range.");
        return Span(m_data, count);
    }

    /// Returns the last `count` elements.
    /// @param count Number of elements.
    /// @pre `count` <= Size().
    /// @return The sub-span.
    /// @since 0.1
    [[nodiscard]] constexpr Span Last(usize count) const noexcept {
        NX_ASSERT(count <= m_size, "Span::Last count out of range.");
        return Span(m_data + (m_size - count), count);
    }

    /// Returns `count` elements starting at `offset`.
    /// @param offset Index of the first element.
    /// @param count  Number of elements.
    /// @pre `offset` + `count` <= Size().
    /// @return The sub-span.
    /// @since 0.1
    [[nodiscard]] constexpr Span Subspan(usize offset, usize count) const noexcept {
        NX_ASSERT(offset <= m_size && count <= m_size - offset, "Span::Subspan range out of range.");
        return Span(m_data + offset, count);
    }

    /// Returns the elements from `offset` to the end.
    /// @param offset Index of the first element.
    /// @pre `offset` <= Size().
    /// @return The sub-span.
    /// @since 0.1
    [[nodiscard]] constexpr Span Subspan(usize offset) const noexcept {
        NX_ASSERT(offset <= m_size, "Span::Subspan offset out of range.");
        return Span(m_data + offset, m_size - offset);
    }

    // Range-for support. These two names are lower-case because the language requires it.

    /// Pointer to the first element, for range-for. @since 0.1
    [[nodiscard]] constexpr T* begin() const noexcept {
        return m_data;
    }

    /// Pointer past the last element, for range-for. @since 0.1
    [[nodiscard]] constexpr T* end() const noexcept {
        return m_data + m_size;
    }

private:
    T* m_data = nullptr;
    usize m_size = 0;
};

} // namespace Nexyl
