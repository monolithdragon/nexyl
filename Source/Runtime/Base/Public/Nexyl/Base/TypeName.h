#pragma once

#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Std/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// The fully qualified name of a type as the compiler spells it, without `struct`, `class` or `enum`.
///
/// The text is not NUL-terminated as far as `size` is concerned; it lives in the binary's constant data.
/// @threadsafety safe
/// @since 0.1
struct TypeName {
    /// First character of the name.
    const char* data = "";
    /// Length of the name in bytes.
    usize size = 0;
};

namespace Detail {

/// Returns the compiler's description of this function, which names `T`. Internal. @since 0.1
template <typename T>
consteval const char* TypeNameProbe() {
    return std::source_location::current().function_name();
}

/// Position of `pattern` in `text` at or after `from`, or `size` if absent. Internal. @since 0.1
consteval usize FindInName(const char* text, usize size, const char* pattern, usize from) {
    usize patternSize = 0;
    while (pattern[patternSize] != '\0') {
        ++patternSize;
    }
    for (usize start = from; start + patternSize <= size; ++start) {
        bool match = true;
        for (usize index = 0; index < patternSize && match; ++index) {
            match = text[start + index] == pattern[index];
        }
        if (match) {
            return start;
        }
    }
    return size;
}

/// Whether `text` starts with `prefix` at `position`. Internal. @since 0.1
consteval bool NameStartsWith(const char* text, usize size, usize position, const char* prefix) {
    usize index = 0;
    while (prefix[index] != '\0') {
        if (position + index >= size || text[position + index] != prefix[index]) {
            return false;
        }
        ++index;
    }
    return true;
}

} // namespace Detail

/// Returns the fully qualified name of a class or enumeration type, computed at compile time.
///
/// Reflection derives a type's `TypeId` from this name (docs/NEXYL_ARCHITECTURE.md, section 8.8). Every
/// supported compiler gives the same text for a named class or enumeration outside an anonymous namespace,
/// for example `Nexyl::Math::Vec3`. Fundamental types and template arguments are spelled differently by
/// different compilers; callers that need a stable identity for them do not use this text.
/// @return The name.
/// @threadsafety safe
/// @since 0.1
template <typename T>
consteval TypeName TypeNameOf() {
    const char* const text = Detail::TypeNameProbe<T>();
    usize size = 0;
    while (text[size] != '\0') {
        ++size;
    }
#if NX_COMPILER_MSVC
    // "const char *__cdecl Nexyl::Detail::TypeNameProbe<struct Nexyl::Math::Vec3>(void)"
    usize begin = Detail::FindInName(text, size, "TypeNameProbe<", 0) + 14;
    usize end = size;
    while (end > begin && text[end - 1] != '>') {
        --end;
    }
    --end;
    constexpr const char* kKeywords[] = {"struct ", "class ", "enum ", "union "};
    for (const char* keyword : kKeywords) {
        if (Detail::NameStartsWith(text, size, begin, keyword)) {
            usize length = 0;
            while (keyword[length] != '\0') {
                ++length;
            }
            begin += length;
            break;
        }
    }
#else
    // Clang: "... TypeNameProbe() [T = Nexyl::Math::Vec3]"; GCC: "... [with T = Nexyl::Math::Vec3]".
    const usize begin = Detail::FindInName(text, size, "T = ", 0) + 4;
    usize end = begin;
    while (end < size && text[end] != ']' && text[end] != ';') {
        ++end;
    }
#endif
    return TypeName{text + begin, end - begin};
}

} // namespace Nexyl
