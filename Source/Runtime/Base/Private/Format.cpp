#include <Nexyl/Base/Format.h>

#include "FormatInternal.h"

#include <Nexyl/Base/Assert.h>

namespace Nexyl {

namespace {

constexpr usize kMaxIntegerDigits = 64; // u64 in binary

[[nodiscard]] usize TextLength(const Detail::FormatArg& arg) {
    if (arg.size != Detail::kNulTerminated) {
        return arg.size;
    }
    const char* const text = static_cast<const char*>(arg.pointer);
    usize length = 0;
    while (text[length] != '\0') {
        ++length;
    }
    return length;
}

void FormatInteger(FormatWriter& writer, const FormatSpec& spec, u64 magnitude, bool negative) {
    u32 base = 10;
    bool upper = false;
    const char* alternatePrefix = "";
    switch (spec.type) {
        case 'x':
            base = 16;
            alternatePrefix = "0x";
            break;
        case 'X':
            base = 16;
            upper = true;
            alternatePrefix = "0X";
            break;
        case 'b':
            base = 2;
            alternatePrefix = "0b";
            break;
        case 'o':
            base = 8;
            alternatePrefix = "0o";
            break;
        default:
            break;
    }

    char reversed[kMaxIntegerDigits] = {};
    usize count = 0;
    do {
        const auto digit = static_cast<u32>(magnitude % base);
        magnitude /= base;
        reversed[count++] = static_cast<char>(digit < 10 ? '0' + digit : (upper ? 'A' : 'a') + (digit - 10));
    } while (magnitude != 0);
    char digits[kMaxIntegerDigits] = {};
    for (usize index = 0; index < count; ++index) {
        digits[index] = reversed[count - 1 - index];
    }

    char prefix[3] = {};
    usize prefixSize = 0;
    if (negative) {
        prefix[prefixSize++] = '-';
    } else if (spec.sign == FormatSign::Plus) {
        prefix[prefixSize++] = '+';
    } else if (spec.sign == FormatSign::Space) {
        prefix[prefixSize++] = ' ';
    }
    if (spec.alternate && base != 10) {
        prefix[prefixSize++] = alternatePrefix[0];
        prefix[prefixSize++] = alternatePrefix[1];
    }
    Detail::WriteNumber(writer, spec, prefix, prefixSize, digits, count);
}

void FormatSigned(FormatWriter& writer, const FormatSpec& spec, u64 bits) {
    const auto value = static_cast<i64>(bits);
    // Negate in unsigned arithmetic so that the most negative value does not overflow.
    const u64 magnitude = value < 0 ? ~bits + 1U : bits;
    FormatInteger(writer, spec, magnitude, value < 0);
}

void FormatPointer(FormatWriter& writer, const FormatSpec& spec, const void* pointer) {
    FormatSpec hex = spec;
    hex.type = 'x';
    hex.alternate = true;
    FormatInteger(writer, hex, BitCast<u64>(pointer), false);
}

void FormatOne(FormatWriter& writer, const FormatSpec& spec, const Detail::FormatArg& arg) {
    switch (arg.category) {
        case Detail::FormatCategory::Bool:
            if (arg.bits != 0) {
                writer.WriteText(spec, "true", 4);
            } else {
                writer.WriteText(spec, "false", 5);
            }
            return;
        case Detail::FormatCategory::Character:
            if (spec.type == '\0' || spec.type == 'c') {
                const auto character = static_cast<char>(arg.bits);
                writer.WritePadded(spec, &character, 1, FormatAlign::Left);
            } else {
                FormatInteger(writer, spec, arg.bits, false);
            }
            return;
        case Detail::FormatCategory::SignedInteger:
            FormatSigned(writer, spec, arg.bits);
            return;
        case Detail::FormatCategory::UnsignedInteger:
            FormatInteger(writer, spec, arg.bits, false);
            return;
        case Detail::FormatCategory::FloatingPoint:
            Detail::FormatFloatingPoint(writer, spec, arg.bits, arg.size);
            return;
        case Detail::FormatCategory::Text:
            if (arg.pointer == nullptr) {
                writer.WriteText(spec, "(null)", 6);
            } else {
                writer.WriteText(spec, static_cast<const char*>(arg.pointer), TextLength(arg));
            }
            return;
        case Detail::FormatCategory::Pointer:
            FormatPointer(writer, spec, arg.pointer);
            return;
        case Detail::FormatCategory::Custom:
            arg.custom(writer, spec, arg.pointer);
            return;
    }
}

} // namespace

void FormatWriter::Write(const char* text, usize size) {
    const usize space = m_buffer.Size() - m_size;
    const usize count = size <= space ? size : space;
    for (usize index = 0; index < count; ++index) {
        m_buffer[m_size + index] = text[index];
    }
    m_size += count;
    m_truncated = m_truncated || count < size;
}

void FormatWriter::WriteRepeated(char character, usize count) {
    const usize space = m_buffer.Size() - m_size;
    const usize written = count <= space ? count : space;
    for (usize index = 0; index < written; ++index) {
        m_buffer[m_size + index] = character;
    }
    m_size += written;
    m_truncated = m_truncated || written < count;
}

void FormatWriter::WritePadded(const FormatSpec& spec, const char* text, usize size, FormatAlign defaultAlign) {
    if (spec.width <= size) {
        Write(text, size);
        return;
    }
    const usize padding = spec.width - size;
    const FormatAlign align = spec.align == FormatAlign::Default ? defaultAlign : spec.align;
    usize before = 0;
    if (align == FormatAlign::Right) {
        before = padding;
    } else if (align == FormatAlign::Center) {
        before = padding / 2;
    }
    WriteRepeated(spec.fill, before);
    Write(text, size);
    WriteRepeated(spec.fill, padding - before);
}

void FormatWriter::WriteText(const FormatSpec& spec, const char* text, usize size) {
    const usize length = spec.hasPrecision && spec.precision < size ? spec.precision : size;
    WritePadded(spec, text, length, FormatAlign::Left);
}

namespace Detail {

void WriteNumber(FormatWriter& writer, const FormatSpec& spec, const char* prefix, usize prefixSize, const char* body,
                 usize bodySize, bool allowZeroPad) {
    const usize length = prefixSize + bodySize;
    if (spec.zeroPad && allowZeroPad && spec.align == FormatAlign::Default) {
        writer.Write(prefix, prefixSize);
        if (spec.width > length) {
            writer.WriteRepeated('0', spec.width - length);
        }
        writer.Write(body, bodySize);
        return;
    }
    if (spec.width <= length) {
        writer.Write(prefix, prefixSize);
        writer.Write(body, bodySize);
        return;
    }
    const usize padding = spec.width - length;
    usize before = padding;
    if (spec.align == FormatAlign::Left) {
        before = 0;
    } else if (spec.align == FormatAlign::Center) {
        before = padding / 2;
    }
    writer.WriteRepeated(spec.fill, before);
    writer.Write(prefix, prefixSize);
    writer.Write(body, bodySize);
    writer.WriteRepeated(spec.fill, padding - before);
}

void FormatArgs(FormatWriter& writer, const char* text, usize size, Span<const FormatArg> args) {
    usize argument = 0;
    usize literalStart = 0;
    usize position = 0;
    while (position < size) {
        const char character = text[position];
        if (character != '{' && character != '}') {
            ++position;
            continue;
        }
        writer.Write(text + literalStart, position - literalStart);
        if (position + 1 < size && text[position + 1] == character) {
            // An escaped brace: write one, skip both.
            writer.Write(text + position, 1);
            position += 2;
            literalStart = position;
            continue;
        }
        NX_ASSERT(character == '{', "Unmatched '}' in a validated format string.");
        ++position;
        FormatSpec spec;
        const char* const reason = ParseFormatField(text, size, position, spec);
        NX_ASSERT(reason == nullptr && argument < args.Size(), "Invalid format string passed validation.");
        static_cast<void>(reason);
        FormatOne(writer, spec, args[argument]);
        ++argument;
        literalStart = position;
    }
    writer.Write(text + literalStart, size - literalStart);
}

} // namespace Detail

} // namespace Nexyl
