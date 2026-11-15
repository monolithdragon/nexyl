#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Hash.h>

namespace Nexyl::Containers {

/// A non-owning view of UTF-8 text: a pointer and a byte count, without a terminator
/// (docs/CODING_STANDARDS.md, section 6.1).
///
/// The layout matches `NxStringView` of the C ABI (docs/PLUGIN_ABI.md, section 3). A view is valid only while
/// the text it refers to is, and is never stored beyond that (rule OWN-9). It converts implicitly from a string
/// literal, like Span from an array (the documented exception to rule CLS-6); other text is viewed explicitly.
///
/// ```cpp
/// void SetName(StringView name);
/// SetName("Player");
/// SetName(StringView::FromCString(path));
/// ```
/// @threadsafety compatible
/// @since 0.1
class StringView final {
public:
    /// Creates an empty view. @since 0.1
    constexpr StringView() = default;

    /// Creates a view of `size` bytes at `data`.
    /// @param data First byte. May be null only when `size` is zero.
    /// @param size Number of bytes.
    /// @since 0.1
    constexpr StringView(const char* data, usize size) : m_data(data), m_size(size) {
        NX_ASSERT(data != nullptr || size == 0, "A view with text needs a data pointer.");
    }

    /// Views a string literal, up to its first NUL character. Implicit by design, see above. @since 0.1
    template <usize N>
    constexpr explicit(false) StringView(const char (&literal)[N]) : m_data(literal), m_size(LengthOf(literal, N)) {}

    /// Views a NUL-terminated string.
    /// @param text The string, or null for an empty view.
    /// @return A view up to the terminator.
    /// @since 0.1
    [[nodiscard]] static constexpr StringView FromCString(const char* text) {
        if (text == nullptr) {
            return {};
        }
        usize size = 0;
        while (text[size] != '\0') {
            ++size;
        }
        return StringView(text, size);
    }

    /// Returns the first byte, or null for an empty default view. @since 0.1
    [[nodiscard]] constexpr const char* Data() const {
        return m_data;
    }

    /// Returns the number of bytes. @since 0.1
    [[nodiscard]] constexpr usize Size() const {
        return m_size;
    }

    /// Checks whether the view has no bytes. @since 0.1
    [[nodiscard]] constexpr bool IsEmpty() const {
        return m_size == 0;
    }

    /// Returns the byte at `index`. @pre `index` < Size(). @since 0.1
    [[nodiscard]] constexpr char operator[](usize index) const {
        NX_ASSERT(index < m_size, "StringView index out of range.");
        return m_data[index];
    }

    /// Returns the first `count` bytes. @pre `count` <= Size(). @since 0.1
    [[nodiscard]] constexpr StringView First(usize count) const {
        NX_ASSERT(count <= m_size, "StringView::First count out of range.");
        return StringView(m_data, count);
    }

    /// Returns the last `count` bytes. @pre `count` <= Size(). @since 0.1
    [[nodiscard]] constexpr StringView Last(usize count) const {
        NX_ASSERT(count <= m_size, "StringView::Last count out of range.");
        return StringView(m_data + (m_size - count), count);
    }

    /// Returns `count` bytes starting at `offset`. @pre `offset` + `count` <= Size(). @since 0.1
    [[nodiscard]] constexpr StringView Substring(usize offset, usize count) const {
        NX_ASSERT(offset <= m_size && count <= m_size - offset, "StringView::Substring range out of range.");
        return StringView(m_data + offset, count);
    }

    /// Returns the bytes from `offset` to the end. @pre `offset` <= Size(). @since 0.1
    [[nodiscard]] constexpr StringView Substring(usize offset) const {
        NX_ASSERT(offset <= m_size, "StringView::Substring offset out of range.");
        return StringView(m_data + offset, m_size - offset);
    }

    /// Checks whether the view begins with `prefix`. @since 0.1
    [[nodiscard]] constexpr bool StartsWith(StringView prefix) const {
        return prefix.m_size <= m_size && First(prefix.m_size) == prefix;
    }

    /// Checks whether the view ends with `suffix`. @since 0.1
    [[nodiscard]] constexpr bool EndsWith(StringView suffix) const {
        return suffix.m_size <= m_size && Last(suffix.m_size) == suffix;
    }

    /// Finds the first occurrence of `text` at or after `from`.
    /// @param text The text to find. An empty text is found at `from`.
    /// @param from Where the search starts.
    /// @return Its offset, or kNone.
    /// @since 0.1
    [[nodiscard]] constexpr Option<usize> Find(StringView text, usize from = 0) const {
        if (from > m_size || text.m_size > m_size - from) {
            return kNone;
        }
        for (usize start = from; start + text.m_size <= m_size; ++start) {
            if (Substring(start, text.m_size) == text) {
                return start;
            }
        }
        return kNone;
    }

    /// Finds the first occurrence of a byte at or after `from`. @return Its offset, or kNone. @since 0.1
    [[nodiscard]] constexpr Option<usize> Find(char character, usize from = 0) const {
        for (usize index = from; index < m_size; ++index) {
            if (m_data[index] == character) {
                return index;
            }
        }
        return kNone;
    }

    /// Checks whether `text` occurs in the view. @since 0.1
    [[nodiscard]] constexpr bool Contains(StringView text) const {
        return Find(text).HasValue();
    }

    /// Compares byte by byte, as unsigned bytes, then by length.
    /// @return -1 if this view sorts first, 1 if `other` does, 0 if they are equal.
    /// @since 0.1
    [[nodiscard]] constexpr i32 Compare(StringView other) const {
        const usize common = m_size < other.m_size ? m_size : other.m_size;
        for (usize index = 0; index < common; ++index) {
            const auto mine = static_cast<u8>(m_data[index]);
            const auto theirs = static_cast<u8>(other.m_data[index]);
            if (mine != theirs) {
                return mine < theirs ? -1 : 1;
            }
        }
        if (m_size == other.m_size) {
            return 0;
        }
        return m_size < other.m_size ? -1 : 1;
    }

    /// Checks whether two views hold the same bytes. @since 0.1
    [[nodiscard]] constexpr bool operator==(StringView other) const {
        if (m_size != other.m_size) {
            return false;
        }
        for (usize index = 0; index < m_size; ++index) {
            if (m_data[index] != other.m_data[index]) {
                return false;
            }
        }
        return true;
    }

    /// Pointer to the first byte, for range-for. @since 0.1
    [[nodiscard]] constexpr const char* begin() const {
        return m_data;
    }

    /// Pointer past the last byte, for range-for. @since 0.1
    [[nodiscard]] constexpr const char* end() const {
        return m_data + m_size;
    }

private:
    template <usize N>
    static constexpr usize LengthOf(const char (&literal)[N], usize capacity) {
        usize size = 0;
        while (size < capacity && literal[size] != '\0') {
            ++size;
        }
        return size;
    }

    const char* m_data = nullptr;
    usize m_size = 0;
};

/// Hashes the bytes of a view. Equal to the hash of a String with the same text.
/// @param text The text.
/// @return The hash.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline u64 HashValue(StringView text) {
    return HashBytes(text.Data(), text.Size());
}

} // namespace Nexyl::Containers
