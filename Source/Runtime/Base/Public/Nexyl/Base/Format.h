#pragma once

#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

// The formatter's types share this header because each one exists only to serve the others (rule HDR-9 allows
// tightly coupled types in one header).

namespace Nexyl {

/// Where padded text sits inside its field.
/// @since 0.1
enum class FormatAlign : u8 {
    /// The type's own default: numbers right, everything else left.
    Default,
    /// `<`: padding after the text.
    Left,
    /// `>`: padding before the text.
    Right,
    /// `^`: padding split, the extra character after the text.
    Center,
};

/// Which non-negative numbers get a sign character.
/// @since 0.1
enum class FormatSign : u8 {
    /// Only negative numbers get a sign.
    Default,
    /// `+`: non-negative numbers get `+`.
    Plus,
    /// A space: non-negative numbers get a space.
    Space,
};

/// The parsed specification of one replacement field: `{:[[fill]align][sign][#][0][width][.precision][type]}`.
///
/// | Part | Meaning |
/// |---|---|
/// | fill, align | Any ASCII character other than a brace, then `<`, `>` or `^` |
/// | sign | `+` or a space; numbers only |
/// | `#` | Integers: prefix `0x`, `0X`, `0b` or `0o` |
/// | `0` | Numbers: pad with zeros after the sign and prefix (ignored when an alignment is given) |
/// | width | Minimum field width in bytes, at most kMaxFormatWidth |
/// | precision | Floating point: digits (see `type`); text: maximum bytes. At most kMaxFormatPrecision |
/// | type | Integers `d x X b o`; characters `c` or an integer type; floating point `e E f F g G`; text and `bool` `s`;
/// pointers `p` |
///
/// @threadsafety safe
/// @since 0.1
struct FormatSpec {
    /// Padding character. ASCII.
    char fill = ' ';
    /// Alignment of the text in its field.
    FormatAlign align = FormatAlign::Default;
    /// Sign of non-negative numbers.
    FormatSign sign = FormatSign::Default;
    /// `#` was given.
    bool alternate = false;
    /// `0` was given.
    bool zeroPad = false;
    /// A precision was given.
    bool hasPrecision = false;
    /// Minimum field width in bytes.
    u16 width = 0;
    /// The precision, if `hasPrecision`.
    u16 precision = 0;
    /// The type character, or `'\0'` for the default presentation.
    char type = '\0';
};

/// The largest field width a format string may request.
/// @since 0.1
inline constexpr u16 kMaxFormatWidth = 1024;

/// The largest precision a format string may request.
/// @since 0.1
inline constexpr u16 kMaxFormatPrecision = 512;

/// Writes formatted text into a caller-supplied buffer, never past its end.
///
/// Output that does not fit is dropped and the writer remembers that it truncated. The buffer is not
/// NUL-terminated. Custom formatters (see Formattable) write through it.
///
/// ```cpp
/// char buffer[64];
/// FormatWriter writer(buffer);
/// FormatTo(writer, "{} of {}", done, total);
/// ```
/// @threadsafety compatible
/// @since 0.1
/// @opaque
class FormatWriter final {
public:
    /// Creates a writer that fills `buffer` from its start.
    /// @param buffer Receives the text. Borrowed; it must outlive the writer.
    /// @since 0.1
    explicit FormatWriter(Span<char> buffer) : m_buffer(buffer) {}

    /// Appends text, as much as fits.
    /// @param text The bytes to append. Borrowed for the duration of the call.
    /// @param size Number of bytes in `text`.
    /// @since 0.1
    NX_BASE_API void Write(const char* text, usize size);

    /// Appends `count` copies of a character, as many as fit.
    /// @param character The byte to append.
    /// @param count     How many times.
    /// @since 0.1
    NX_BASE_API void WriteRepeated(char character, usize count);

    /// Appends text inside a field padded to `spec.width` with `spec.fill`, aligned by `spec.align`.
    /// @param spec         Supplies width, fill and alignment; other parts are ignored.
    /// @param text         The bytes to append. Borrowed for the duration of the call.
    /// @param size         Number of bytes in `text`.
    /// @param defaultAlign Used when `spec.align` is FormatAlign::Default.
    /// @since 0.1
    NX_BASE_API void WritePadded(const FormatSpec& spec, const char* text, usize size, FormatAlign defaultAlign);

    /// Appends text the way a `{:s}` field formats it: cut to the precision, then padded, left-aligned by
    /// default. Custom formatters of string types use it.
    /// @param spec Supplies precision, width, fill and alignment.
    /// @param text The bytes to append. Borrowed for the duration of the call.
    /// @param size Number of bytes in `text`.
    /// @since 0.1
    NX_BASE_API void WriteText(const FormatSpec& spec, const char* text, usize size);

    /// Returns the number of bytes written so far.
    /// @return At most the buffer size.
    /// @since 0.1
    [[nodiscard]] usize Size() const {
        return m_size;
    }

    /// Tells whether output was dropped because the buffer was full.
    /// @return True after the first write that did not fit.
    /// @since 0.1
    [[nodiscard]] bool IsTruncated() const {
        return m_truncated;
    }

private:
    Span<char> m_buffer;
    usize m_size = 0;
    bool m_truncated = false;
};

/// The outcome of Format.
/// @threadsafety safe
/// @since 0.1
struct FormatResult {
    /// Number of bytes written to the buffer.
    usize size = 0;
    /// True if the text did not fit and was cut.
    bool truncated = false;
};

namespace Detail {

/// How the formatter treats an argument type. Internal. @since 0.1
enum class FormatCategory : u8 {
    /// `bool`.
    Bool,
    /// `char`.
    Character,
    /// A signed integer or an enumeration with a signed underlying type.
    SignedInteger,
    /// An unsigned integer or an enumeration with an unsigned underlying type.
    UnsignedInteger,
    /// `f32` or `f64`.
    FloatingPoint,
    /// A character array or a pointer to `char`.
    Text,
    /// Any other pointer, or `nullptr`.
    Pointer,
    /// A type with a FormatValue overload.
    Custom,
};

/// Formats a custom argument. Internal. @since 0.1
using CustomFormatFunction = void (*)(FormatWriter& writer, const FormatSpec& spec, const void* value);

/// One type-erased argument. Internal; built by Format. @threadsafety safe @since 0.1
struct FormatArg {
    /// What `bits`, `pointer` and `size` hold.
    FormatCategory category = FormatCategory::Custom;
    /// The integer value, the `f64` bits, or for `f32` the `f32` bits; `size` is 4 for `f32`.
    u64 bits = 0;
    /// Text, pointer value, or the custom object.
    const void* pointer = nullptr;
    /// Text length in bytes, kNulTerminated for a NUL-terminated pointer, or 4 or 8 for floating point.
    usize size = 0;
    /// The formatter of a custom argument.
    CustomFormatFunction custom = nullptr;
};

/// FormatArg::size of text that ends at its first NUL byte. Internal. @since 0.1
inline constexpr usize kNulTerminated = ~usize{0};

template <typename T>
[[nodiscard]] consteval FormatCategory FormatCategoryOf();

template <typename T>
void FormatCustom(FormatWriter& writer, const FormatSpec& spec, const void* value);

/// Parses the replacement field that starts after a `{`. Internal.
/// @return nullptr on success, with `position` after the closing `}`; otherwise the reason.
/// @since 0.1
[[nodiscard]] constexpr const char* ParseFormatField(const char* text, usize size, usize& position, FormatSpec& spec);

/// Checks a whole format string against its argument categories. Internal.
/// @return nullptr if the string is valid; otherwise the reason.
/// @since 0.1
[[nodiscard]] constexpr const char* ValidateFormat(const char* text, usize size, const FormatCategory* categories,
                                                   usize count);

/// Formats with type-erased arguments. The format string has been validated. Internal.
/// @since 0.1
NX_BASE_API void FormatArgs(FormatWriter& writer, const char* text, usize size, Span<const FormatArg> args);

} // namespace Detail

/// Satisfied when Format accepts an argument of type `T`.
///
/// Built in: `bool`, `char`, integers, enumerations (as their underlying value), `f32`, `f64`, character arrays,
/// pointers to `char` (NUL-terminated; null prints `(null)`), other pointers and `nullptr`. Any other type is
/// formattable when an overload `void FormatValue(FormatWriter&, const FormatSpec&, const T&)` is found by
/// argument-dependent lookup. Such a formatter receives the field's spec unchecked.
/// @since 0.1
template <typename T>
concept Formattable = Detail::FormatCategoryOf<T>() != Detail::FormatCategory::Custom ||
                      requires(FormatWriter& writer, const FormatSpec& spec, const RemoveCvRef<T>& value) {
                          FormatValue(writer, spec, value);
                      };

/// A format string checked at compile time against the argument types `T...`.
///
/// It converts implicitly from a string literal. A wrong number of replacement fields, an unknown specifier, or
/// a type character that does not fit its argument is a compile error. Literal braces are written `{{` and `}}`.
/// @threadsafety safe
/// @since 0.1
template <typename... T>
class FormatString final {
public:
    /// Checks `text` against the argument types.
    ///
    /// Implicit by design, like the StringView conversion from a literal: a format string is written as a literal
    /// at the call site (the documented exception to rule CLS-6).
    /// @param text The format string literal.
    /// @since 0.1
    template <usize N>
    consteval explicit(false) FormatString(const char (&text)[N]);

    /// Returns the text.
    /// @return The first byte of the format string; not NUL-terminated as far as Size is concerned.
    /// @since 0.1
    [[nodiscard]] constexpr const char* Data() const {
        return m_text;
    }

    /// Returns the length of the text in bytes.
    /// @return The number of bytes, without the literal's terminator.
    /// @since 0.1
    [[nodiscard]] constexpr usize Size() const {
        return m_size;
    }

private:
    const char* m_text = "";
    usize m_size = 0;
};

/// Formats arguments into a writer, after whatever it already holds.
///
/// @param writer Receives the text.
/// @param format The format string, checked at compile time.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void FormatTo(FormatWriter& writer, FormatString<TypeIdentity<T>...> format, const T&... args);

/// Formats arguments into a buffer, never past its end. Allocates nothing.
///
/// ```cpp
/// char buffer[64];
/// const FormatResult result = Format(buffer, "{:>8} {:.3f} {:#x}", "mass", 12.5, 255u);
/// // "    mass 12.500 0xff"
/// ```
/// @param buffer Receives the text. It is not NUL-terminated.
/// @param format The format string, checked at compile time.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @return The number of bytes written and whether the text was cut.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
FormatResult Format(Span<char> buffer, FormatString<TypeIdentity<T>...> format, const T&... args);

} // namespace Nexyl

#include <Nexyl/Base/Format.inl>
