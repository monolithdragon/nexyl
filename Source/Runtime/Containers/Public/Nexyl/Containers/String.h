#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Hash.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::Containers {

/// Growable UTF-8 text that owns its bytes (docs/CODING_STANDARDS.md, section 6.1).
///
/// The text is always followed by a NUL byte, so CStr hands it to functions that need a terminated string, such
/// as the Platform file functions, without copying. The bytes are not checked for valid UTF-8. Like Array, a
/// string takes its memory from the allocator it was created with; a default-constructed string is empty and has
/// none. Copies are explicit, with Clone (rule OWN-7). View returns a StringView of the text.
///
/// ```cpp
/// String path(allocator, "Assets/");
/// path.Append(name);
/// const Result<File, Error> file = File::Open(path.CStr(), FileAccess::Read, FileOpenMode::OpenExisting);
/// ```
/// @threadsafety compatible
/// @since 0.1
class String final {
public:
    /// Creates an empty string without an allocator. @since 0.1
    constexpr String() = default;

    /// Creates an empty string. It takes no memory until it grows.
    /// @param allocator Supplies the memory. It must outlive the string.
    /// @since 0.1
    explicit String(Memory::IAllocator& allocator) : m_allocator(&allocator) {}

    /// Creates a string holding a copy of `text`.
    /// @param allocator Supplies the memory. It must outlive the string.
    /// @param text      The initial text.
    /// @since 0.1
    NX_CONTAINERS_API String(Memory::IAllocator& allocator, StringView text);

    /// Takes over the text and the allocator of `other`, which becomes empty and keeps no allocator. @since 0.1
    NX_CONTAINERS_API String(String&& other) noexcept;
    /// Releases the current text, then takes over that of `other`, like the move constructor. @since 0.1
    NX_CONTAINERS_API String& operator=(String&& other) noexcept;
    /// Releases the memory. @since 0.1
    NX_CONTAINERS_API ~String();

    String(const String&) = delete;
    String& operator=(const String&) = delete;

    /// Copies the string into memory from the same allocator. @return The copy. @since 0.1
    [[nodiscard]] NX_CONTAINERS_API String Clone() const;
    /// Copies the string into memory from `allocator`. @param allocator For the copy. @return The copy. @since 0.1
    [[nodiscard]] NX_CONTAINERS_API String Clone(Memory::IAllocator& allocator) const;

    /// Returns the text followed by a NUL byte. Never null; an empty string returns "". @since 0.1
    [[nodiscard]] const char* CStr() const {
        return m_data != nullptr ? m_data : "";
    }

    /// Returns the first byte, as CStr does. @since 0.1
    [[nodiscard]] const char* Data() const {
        return CStr();
    }

    /// Returns the number of bytes, without the terminator. @since 0.1
    [[nodiscard]] usize Size() const {
        return m_size;
    }

    /// Returns how many bytes fit before the string has to grow, without the terminator. @since 0.1
    [[nodiscard]] usize Capacity() const {
        return m_capacity;
    }

    /// Checks whether the string has no bytes. @since 0.1
    [[nodiscard]] bool IsEmpty() const {
        return m_size == 0;
    }

    /// Returns the allocator, or null for a string that has none. @since 0.1
    [[nodiscard]] Memory::IAllocator* GetAllocator() const {
        return m_allocator;
    }

    /// Returns a view of the text. It is valid until the string changes. @since 0.1
    [[nodiscard]] StringView View() const {
        return StringView(CStr(), m_size);
    }

    /// Returns the byte at `index`. @pre `index` < Size(). @since 0.1
    [[nodiscard]] char operator[](usize index) const {
        NX_ASSERT(index < m_size, "String index out of range.");
        return m_data[index];
    }

    /// Returns the byte at `index` for writing. @pre `index` < Size(). @since 0.1
    [[nodiscard]] char& operator[](usize index) {
        NX_ASSERT(index < m_size, "String index out of range.");
        return m_data[index];
    }

    /// Makes room for at least `capacity` bytes, terminating if the memory is not available. @since 0.1
    NX_CONTAINERS_API void Reserve(usize capacity);

    /// Makes room for at least `capacity` bytes.
    /// @param capacity The number of bytes, without the terminator.
    /// @error OutOfMemory, CapacityExceeded The allocator cannot provide the memory; the string is unchanged.
    /// @since 0.1
    NX_CONTAINERS_API Result<void, Error> TryReserve(usize capacity);

    /// Replaces the text with `text`, which may view this string. @since 0.1
    NX_CONTAINERS_API void Assign(StringView text);

    /// Adds `text` at the end. `text` may view this string. @since 0.1
    NX_CONTAINERS_API void Append(StringView text);

    /// Adds one byte at the end. @since 0.1
    NX_CONTAINERS_API void Append(char character);

    /// Inserts `text` before the byte at `index`. `text` may view this string. @pre `index` <= Size().
    /// @since 0.1
    NX_CONTAINERS_API void Insert(usize index, StringView text);

    /// Removes `count` bytes starting at `index`. @pre `index` + `count` <= Size(). @since 0.1
    NX_CONTAINERS_API void Remove(usize index, usize count);

    /// Changes the length; new bytes are `fill`. @param size The new length. @param fill The byte to add. @since 0.1
    NX_CONTAINERS_API void Resize(usize size, char fill = '\0');

    /// Empties the string and keeps the memory. @since 0.1
    NX_CONTAINERS_API void Clear();

    /// Checks whether the text equals `other`. @since 0.1
    [[nodiscard]] bool operator==(const String& other) const {
        return View() == other.View();
    }

    /// Checks whether the text equals `text`. @since 0.1
    [[nodiscard]] bool operator==(StringView text) const {
        return View() == text;
    }

    /// Pointer to the first byte, for range-for. @since 0.1
    [[nodiscard]] const char* begin() const {
        return CStr();
    }

    /// Pointer past the last byte, for range-for. @since 0.1
    [[nodiscard]] const char* end() const {
        return CStr() + m_size;
    }

private:
    [[nodiscard]] usize GrownCapacity(usize minimum) const;
    void GrowFor(usize minimum);
    void Release();

    // m_capacity bytes of text plus one for the terminator; null until the string first grows.
    char* m_data = nullptr;
    usize m_size = 0;
    usize m_capacity = 0;
    Memory::IAllocator* m_allocator = nullptr;
};

/// Formats a string as text, like a `{:s}` field: the precision cuts it, the width pads it.
/// @param writer Receives the text.
/// @param spec   The field's spec.
/// @param text   The string. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
inline void FormatValue(FormatWriter& writer, const FormatSpec& spec, const String& text) {
    writer.WriteText(spec, text.Data(), text.Size());
}

/// Hashes the text of a string. Equal to the hash of a StringView with the same text, so a HashMap keyed by
/// String can be searched with a StringView.
/// @param text The string.
/// @return The hash.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline u64 HashValue(const String& text) {
    return HashValue(text.View());
}

} // namespace Nexyl::Containers
