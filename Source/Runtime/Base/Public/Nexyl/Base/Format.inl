#pragma once

// Inline and template definitions of <Nexyl/Base/Format.h> (rule HDR-8).

namespace Nexyl {

namespace Detail {

template <typename T>
consteval FormatCategory FormatCategoryOf() {
    using U = RemoveCvRef<T>;
    if constexpr (SameAs<U, bool>) {
        return FormatCategory::Bool;
    } else if constexpr (SameAs<U, char>) {
        return FormatCategory::Character;
    } else if constexpr (Enumeration<U>) {
        return SignedIntegral<UnderlyingType<U>> ? FormatCategory::SignedInteger : FormatCategory::UnsignedInteger;
    } else if constexpr (SignedIntegral<U>) {
        return FormatCategory::SignedInteger;
    } else if constexpr (Integral<U>) {
        return FormatCategory::UnsignedInteger;
    } else if constexpr (ArrayType<U>) {
        return SameAs<RemoveCv<RemoveExtent<U>>, char> ? FormatCategory::Text : FormatCategory::Custom;
    } else if constexpr (PointerType<U>) {
        return SameAs<RemoveCv<RemovePointer<U>>, char> ? FormatCategory::Text : FormatCategory::Pointer;
    } else if constexpr (NullPointerType<U>) {
        return FormatCategory::Pointer;
    } else {
        return FormatCategory::Custom;
    }
}

template <typename T>
void FormatCustom(FormatWriter& writer, const FormatSpec& spec, const void* value) {
    FormatValue(writer, spec, *static_cast<const T*>(value));
}

template <typename T>
[[nodiscard]] constexpr FormatArg MakeFormatArg(const T& value) {
    using U = RemoveCvRef<T>;
    constexpr FormatCategory kCategory = FormatCategoryOf<T>();
    FormatArg arg;
    arg.category = kCategory;
    if constexpr (kCategory == FormatCategory::Bool) {
        arg.bits = value ? 1U : 0U;
    } else if constexpr (kCategory == FormatCategory::Character) {
        arg.bits = static_cast<u8>(value);
    } else if constexpr (kCategory == FormatCategory::SignedInteger) {
        if constexpr (Enumeration<U>) {
            arg.bits = static_cast<u64>(static_cast<i64>(static_cast<UnderlyingType<U>>(value)));
        } else {
            arg.bits = static_cast<u64>(static_cast<i64>(value));
        }
    } else if constexpr (kCategory == FormatCategory::UnsignedInteger) {
        if constexpr (Enumeration<U>) {
            arg.bits = static_cast<u64>(static_cast<UnderlyingType<U>>(value));
        } else {
            arg.bits = static_cast<u64>(value);
        }
    } else if constexpr (kCategory == FormatCategory::FloatingPoint) {
        if constexpr (SameAs<U, f32>) {
            arg.bits = BitCast<u32>(value);
            arg.size = sizeof(f32);
        } else {
            arg.bits = BitCast<u64>(static_cast<f64>(value));
            arg.size = sizeof(f64);
        }
    } else if constexpr (kCategory == FormatCategory::Text) {
        if constexpr (ArrayType<U>) {
            // A character array holds text up to its first NUL, or the whole array if it has none.
            constexpr usize kCapacity = sizeof(U);
            usize length = 0;
            while (length < kCapacity && value[length] != '\0') {
                ++length;
            }
            arg.pointer = value;
            arg.size = length;
        } else {
            arg.pointer = value;
            arg.size = kNulTerminated;
        }
    } else if constexpr (kCategory == FormatCategory::Pointer) {
        arg.pointer = value;
    } else {
        arg.pointer = &value;
        arg.custom = &FormatCustom<U>;
    }
    return arg;
}

[[nodiscard]] constexpr FormatAlign ToFormatAlign(char character) {
    if (character == '<') {
        return FormatAlign::Left;
    }
    if (character == '>') {
        return FormatAlign::Right;
    }
    if (character == '^') {
        return FormatAlign::Center;
    }
    return FormatAlign::Default;
}

[[nodiscard]] constexpr bool IsDigit(char character) {
    return character >= '0' && character <= '9';
}

// Parses decimal digits at `position` into `value`. Returns false if the number exceeds `limit`.
[[nodiscard]] constexpr bool ParseFormatNumber(const char* text, usize size, usize& position, u16 limit, u16& value) {
    u32 number = 0;
    while (position < size && IsDigit(text[position])) {
        number = number * 10U + static_cast<u32>(text[position] - '0');
        if (number > limit) {
            return false;
        }
        ++position;
    }
    value = static_cast<u16>(number);
    return true;
}

constexpr const char* ParseFormatField(const char* text, usize size, usize& position, FormatSpec& spec) {
    spec = FormatSpec{};
    if (position < size && text[position] == '}') {
        ++position;
        return nullptr;
    }
    if (position >= size || text[position] != ':') {
        return "expected '}' or ':' after '{'; argument indices are not supported, write '{{' for a brace";
    }
    ++position;

    if (position + 1 < size && ToFormatAlign(text[position + 1]) != FormatAlign::Default) {
        const char fill = text[position];
        if (fill == '{' || fill == '}' || static_cast<u8>(fill) >= 0x80U) {
            return "the fill character must be ASCII and not a brace";
        }
        spec.fill = fill;
        spec.align = ToFormatAlign(text[position + 1]);
        position += 2;
    } else if (position < size && ToFormatAlign(text[position]) != FormatAlign::Default) {
        spec.align = ToFormatAlign(text[position]);
        ++position;
    }
    if (position < size && (text[position] == '+' || text[position] == ' ')) {
        spec.sign = text[position] == '+' ? FormatSign::Plus : FormatSign::Space;
        ++position;
    }
    if (position < size && text[position] == '#') {
        spec.alternate = true;
        ++position;
    }
    if (position < size && text[position] == '0') {
        spec.zeroPad = true;
        ++position;
    }
    if (!ParseFormatNumber(text, size, position, kMaxFormatWidth, spec.width)) {
        return "the field width exceeds kMaxFormatWidth";
    }
    if (position < size && text[position] == '.') {
        ++position;
        if (position >= size || !IsDigit(text[position])) {
            return "'.' must be followed by a precision";
        }
        spec.hasPrecision = true;
        if (!ParseFormatNumber(text, size, position, kMaxFormatPrecision, spec.precision)) {
            return "the precision exceeds kMaxFormatPrecision";
        }
    }
    if (position < size && text[position] != '}') {
        spec.type = text[position];
        ++position;
    }
    if (position >= size || text[position] != '}') {
        return "unknown format specifier or missing '}'";
    }
    ++position;
    return nullptr;
}

[[nodiscard]] constexpr bool IsIntegerType(char type) {
    return type == 'd' || type == 'x' || type == 'X' || type == 'b' || type == 'o';
}

[[nodiscard]] constexpr bool IsFloatType(char type) {
    return type == 'e' || type == 'E' || type == 'f' || type == 'F' || type == 'g' || type == 'G';
}

// Checks that a field's spec suits its argument. Custom formatters receive the spec unchecked.
[[nodiscard]] constexpr const char* CheckFormatSpec(FormatCategory category, const FormatSpec& spec) {
    const bool numeric = category == FormatCategory::SignedInteger || category == FormatCategory::UnsignedInteger ||
                         category == FormatCategory::FloatingPoint ||
                         (category == FormatCategory::Character && IsIntegerType(spec.type));
    if (category == FormatCategory::Custom) {
        return nullptr;
    }
    if ((spec.sign != FormatSign::Default || spec.zeroPad) && !numeric) {
        return "sign and zero padding apply only to numbers";
    }
    if (spec.alternate && !IsIntegerType(spec.type)) {
        return "'#' applies only to the integer types d, x, X, b and o";
    }
    if (spec.hasPrecision && category != FormatCategory::FloatingPoint && category != FormatCategory::Text) {
        return "a precision applies only to floating point and text";
    }
    switch (category) {
        case FormatCategory::Bool:
        case FormatCategory::Text:
            return spec.type == '\0' || spec.type == 's' ? nullptr : "text and bool accept only the type 's'";
        case FormatCategory::Character:
            return spec.type == '\0' || spec.type == 'c' || IsIntegerType(spec.type)
                       ? nullptr
                       : "a character accepts only the type 'c' or an integer type";
        case FormatCategory::SignedInteger:
        case FormatCategory::UnsignedInteger:
            return spec.type == '\0' || IsIntegerType(spec.type) ? nullptr
                                                                 : "an integer accepts only the types d, x, X, b and o";
        case FormatCategory::FloatingPoint:
            return spec.type == '\0' || IsFloatType(spec.type)
                       ? nullptr
                       : "floating point accepts only the types e, E, f, F, g and G";
        case FormatCategory::Pointer:
            return spec.type == '\0' || spec.type == 'p' ? nullptr : "a pointer accepts only the type 'p'";
        case FormatCategory::Custom:
            break;
    }
    return nullptr;
}

constexpr const char* ValidateFormat(const char* text, usize size, const FormatCategory* categories, usize count) {
    usize argument = 0;
    usize position = 0;
    while (position < size) {
        const char character = text[position];
        if (character == '{') {
            if (position + 1 < size && text[position + 1] == '{') {
                position += 2;
                continue;
            }
            ++position;
            FormatSpec spec;
            if (const char* const reason = ParseFormatField(text, size, position, spec); reason != nullptr) {
                return reason;
            }
            if (argument >= count) {
                return "the format string has more replacement fields than arguments";
            }
            if (const char* const reason = CheckFormatSpec(categories[argument], spec); reason != nullptr) {
                return reason;
            }
            ++argument;
            continue;
        }
        if (character == '}') {
            if (position + 1 < size && text[position + 1] == '}') {
                position += 2;
                continue;
            }
            return "unmatched '}'; write '}}' for a brace";
        }
        ++position;
    }
    return argument == count ? nullptr : "the format string has fewer replacement fields than arguments";
}

// Not constexpr: calling it during constant evaluation makes the compiler reject the format string, and the
// diagnostic shows `reason`.
inline void InvalidFormatString(const char* reason) {
    static_cast<void>(reason);
}

} // namespace Detail

template <typename... T>
template <usize N>
consteval FormatString<T...>::FormatString(const char (&text)[N]) : m_text(text), m_size(N - 1) {
    // One extra element so that the array is never empty.
    constexpr Detail::FormatCategory kCategories[] = {Detail::FormatCategoryOf<T>()..., Detail::FormatCategory::Custom};
    if (const char* const reason = Detail::ValidateFormat(text, N - 1, kCategories, sizeof...(T)); reason != nullptr) {
        Detail::InvalidFormatString(reason);
    }
}

template <Formattable... T>
void FormatTo(FormatWriter& writer, FormatString<TypeIdentity<T>...> format, const T&... args) {
    const Detail::FormatArg packed[] = {Detail::MakeFormatArg(args)..., Detail::FormatArg{}};
    Detail::FormatArgs(writer, format.Data(), format.Size(), Span<const Detail::FormatArg>(packed, sizeof...(T)));
}

template <Formattable... T>
FormatResult Format(Span<char> buffer, FormatString<TypeIdentity<T>...> format, const T&... args) {
    FormatWriter writer(buffer);
    FormatTo<T...>(writer, format, args...);
    return FormatResult{writer.Size(), writer.IsTruncated()};
}

} // namespace Nexyl
