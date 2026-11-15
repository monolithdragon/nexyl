#include <Nexyl/Serialization/TextArchive.h>

#include "SchemaHash.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Parse.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Serialization/BinaryArchive.h>

namespace Nexyl::Serialization {

namespace {

using Containers::StringView;
using Reflection::FieldInfo;
using Reflection::TypeInfo;
using Reflection::TypeKind;
using Reflection::TypeRegistry;

constexpr u32 kMaxDepth = 64;

[[nodiscard]] StringView Name(const char* text) {
    return StringView::FromCString(text);
}

[[nodiscard]] u8* Advance(void* base, usize offset) {
    return static_cast<u8*>(base) + offset;
}

[[nodiscard]] const u8* Advance(const void* base, usize offset) {
    return static_cast<const u8*>(base) + offset;
}

// ---- Integers of every size, through bytes ---------------------------------------------------------------

[[nodiscard]] i64 LoadSigned(const void* source, usize size) {
    switch (size) {
        case 1: {
            i8 value = 0;
            Memory::Copy(&value, source, 1);
            return value;
        }
        case 2: {
            i16 value = 0;
            Memory::Copy(&value, source, 2);
            return value;
        }
        case 4: {
            i32 value = 0;
            Memory::Copy(&value, source, 4);
            return value;
        }
        default: {
            i64 value = 0;
            Memory::Copy(&value, source, 8);
            return value;
        }
    }
}

[[nodiscard]] u64 LoadUnsigned(const void* source, usize size) {
    u64 value = 0;
    Memory::Copy(&value, source, size); // little-endian: the low bytes come first
    return value;
}

[[nodiscard]] bool FitsSigned(i64 value, usize size) {
    if (size >= 8) {
        return true;
    }
    const auto limit = static_cast<i64>(u64{1} << ((size * 8U) - 1U));
    return value >= -limit && value < limit;
}

[[nodiscard]] bool FitsUnsigned(u64 value, usize size) {
    return size >= 8 || value < (u64{1} << (size * 8U));
}

void StoreInteger(void* destination, u64 bits, usize size) {
    Memory::Copy(destination, &bits, size); // little-endian: the low bytes are the value
}

[[nodiscard]] bool IsInline(TypeKind kind) {
    return kind != TypeKind::Record && kind != TypeKind::Array && kind != TypeKind::FixedArray &&
           kind != TypeKind::HashMap;
}

// ---- Writing --------------------------------------------------------------------------------------------

class TextWriter final {
public:
    TextWriter(const TypeRegistry& registry, Containers::String& out)
        : m_registry(registry), m_out(out), m_memory(*out.GetAllocator(), kSerializationMemoryTag),
          m_versioned(m_memory) {}

    Result<void, Error> Run(const TypeInfo& root, const void* object) {
        NX_TRY(Collect(root, 0));
        NX_TRY(Raw("{\n  \"nexyl\": "));
        NX_TRY(Number(u64{kTextFormatVersion}));
        NX_TRY(Raw(",\n  \"type\": "));
        NX_TRY(Quoted(Name(root.name)));
        NX_TRY(Raw(",\n  \"versions\": {"));
        for (usize index = 0; index < m_versioned.Size(); ++index) {
            NX_TRY(Raw(index == 0 ? StringView("\n    ") : StringView(",\n    ")));
            NX_TRY(Quoted(Name(m_versioned[index]->name)));
            NX_TRY(Raw(": "));
            NX_TRY(Number(u64{m_versioned[index]->version}));
        }
        NX_TRY(Raw(m_versioned.IsEmpty() ? StringView("},\n  \"value\": ") : StringView("\n  },\n  \"value\": ")));
        NX_TRY(WriteValue(root, object, 1, 0));
        return Raw("\n}\n");
    }

private:
    Result<void, Error> Raw(StringView text) {
        NX_TRY(m_out.TryReserve(m_out.Size() + text.Size()));
        m_out.Append(text);
        return {};
    }

    Result<void, Error> Indent(u32 level) {
        for (u32 index = 0; index < level; ++index) {
            NX_TRY(Raw("  "));
        }
        return {};
    }

    template <typename T>
    Result<void, Error> Number(T value) {
        char buffer[48] = {};
        const FormatResult written = Format(buffer, "{}", value);
        return Raw(StringView(buffer, written.size));
    }

    Result<void, Error> Quoted(StringView text) {
        NX_TRY(Raw("\""));
        usize start = 0;
        for (usize index = 0; index < text.Size(); ++index) {
            const auto byte = static_cast<u8>(text[index]);
            if (byte >= 0x20U && text[index] != '"' && text[index] != '\\') {
                continue;
            }
            NX_TRY(Raw(text.Substring(start, index - start)));
            start = index + 1;
            char escape[8] = {};
            usize length = 2;
            escape[0] = '\\';
            switch (text[index]) {
                case '"':
                    escape[1] = '"';
                    break;
                case '\\':
                    escape[1] = '\\';
                    break;
                case '\n':
                    escape[1] = 'n';
                    break;
                case '\r':
                    escape[1] = 'r';
                    break;
                case '\t':
                    escape[1] = 't';
                    break;
                default:
                    length = Format(Span<char>(escape + 1, sizeof(escape) - 1), "u{:04x}", byte).size + 1;
                    break;
            }
            NX_TRY(Raw(StringView(escape, length)));
        }
        NX_TRY(Raw(text.Substring(start)));
        return Raw("\"");
    }

    Result<const TypeInfo*, Error> Resolve(Reflection::TypeId id) const {
        const TypeInfo* const type = m_registry.Find(id);
        if (type == nullptr) {
            return Error(SerializationError::UnknownType);
        }
        return type;
    }

    // Lists every record and enumeration the value uses, for the `versions` key, and checks serializability.
    Result<void, Error> Collect(const TypeInfo& type, u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        if (type.kind == TypeKind::Opaque || (type.kind == TypeKind::Record && !type.base.IsNull())) {
            return Error(SerializationError::NotSerializable);
        }
        if (type.kind == TypeKind::Record || type.kind == TypeKind::Enum) {
            for (const TypeInfo* const known : m_versioned) {
                if (known->id == type.id) {
                    return {};
                }
            }
            NX_TRY(m_versioned.TryReserve(m_versioned.Size() + 1));
            m_versioned.Push(&type);
        }
        if (type.kind == TypeKind::Record) {
            for (const FieldInfo& field : type.fields) {
                if (Detail::IsSerialized(field)) {
                    NX_TRY_ASSIGN(const TypeInfo* const child, Resolve(field.type));
                    NX_TRY(Collect(*child, depth + 1));
                }
            }
        } else if (type.kind == TypeKind::Array || type.kind == TypeKind::FixedArray) {
            NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
            NX_TRY(Collect(*element, depth + 1));
        } else if (type.kind == TypeKind::HashMap) {
            NX_TRY_ASSIGN(const TypeInfo* const key, Resolve(type.element));
            NX_TRY(Collect(*key, depth + 1));
            NX_TRY_ASSIGN(const TypeInfo* const value, Resolve(type.value));
            NX_TRY(Collect(*value, depth + 1));
        }
        return {};
    }

    Result<void, Error> WriteFloat(const void* object, usize size) {
        char buffer[48] = {};
        FormatResult written;
        bool finite = true;
        if (size == sizeof(f32)) {
            f32 value = 0.0F;
            Memory::Copy(&value, object, sizeof(value));
            finite = (BitCast<u32>(value) & 0x7F80'0000U) != 0x7F80'0000U;
            written = Format(buffer, "{}", value);
        } else {
            f64 value = 0.0;
            Memory::Copy(&value, object, sizeof(value));
            finite = (BitCast<u64>(value) & 0x7FF0'0000'0000'0000ULL) != 0x7FF0'0000'0000'0000ULL;
            written = Format(buffer, "{}", value);
        }
        // JSON has no infinity or NaN; they are written as strings that ReadText and ParseF64 accept.
        const StringView text(buffer, written.size);
        return finite ? Raw(text) : Quoted(text);
    }

    Result<void, Error> WriteEnum(const TypeInfo& type, const void* object) {
        NX_TRY_ASSIGN(const TypeInfo* const underlying, Resolve(type.element));
        const bool isSigned = underlying->kind == TypeKind::SignedInteger;
        const i64 value = isSigned ? LoadSigned(object, type.size) : static_cast<i64>(LoadUnsigned(object, type.size));
        for (const Reflection::EnumValueInfo& enumerator : type.enumValues) {
            if (enumerator.value == value) {
                return Quoted(Name(enumerator.name));
            }
        }
        return isSigned ? Number(value) : Number(static_cast<u64>(value));
    }

    struct MapVisit {
        TextWriter* writer = nullptr;
        const TypeInfo* key = nullptr;
        const TypeInfo* value = nullptr;
        u32 indent = 0;
        u32 depth = 0;
        bool first = true;
        Result<void, Error> status;
    };

    static void WriteEntry(void* context, const void* key, const void* value) {
        auto& visit = *static_cast<MapVisit*>(context);
        TextWriter& writer = *visit.writer;
        if (visit.status.IsOk()) {
            visit.status = writer.Raw(visit.first ? StringView("\n") : StringView(",\n"));
        }
        visit.first = false;
        if (visit.status.IsOk()) {
            visit.status = writer.Indent(visit.indent + 1);
        }
        if (visit.status.IsOk()) {
            visit.status = writer.Raw("[");
        }
        if (visit.status.IsOk()) {
            visit.status = writer.WriteValue(*visit.key, key, visit.indent + 1, visit.depth);
        }
        if (visit.status.IsOk()) {
            visit.status = writer.Raw(", ");
        }
        if (visit.status.IsOk()) {
            visit.status = writer.WriteValue(*visit.value, value, visit.indent + 1, visit.depth);
        }
        if (visit.status.IsOk()) {
            visit.status = writer.Raw("]");
        }
    }

    // Writes the elements of an array, inline when they are short values, one per line otherwise.
    template <typename TElementAt>
    Result<void, Error> WriteElements(const TypeInfo& element, usize count, u32 indent, u32 depth,
                                      const TElementAt& elementAt) {
        if (count == 0) {
            return Raw("[]");
        }
        const bool inlined = IsInline(element.kind);
        NX_TRY(Raw("["));
        for (usize index = 0; index < count; ++index) {
            if (inlined) {
                NX_TRY(Raw(index == 0 ? StringView("") : StringView(", ")));
            } else {
                NX_TRY(Raw(index == 0 ? StringView("\n") : StringView(",\n")));
                NX_TRY(Indent(indent + 1));
            }
            NX_TRY(WriteValue(element, elementAt(index), indent + 1, depth + 1));
        }
        if (!inlined) {
            NX_TRY(Raw("\n"));
            NX_TRY(Indent(indent));
        }
        return Raw("]");
    }

    Result<void, Error> WriteValue(const TypeInfo& type, const void* object, u32 indent, u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        switch (type.kind) {
            case TypeKind::Bool: {
                u8 value = 0;
                Memory::Copy(&value, object, 1);
                return Raw(value != 0 ? StringView("true") : StringView("false"));
            }
            case TypeKind::Character: {
                char value = 0;
                Memory::Copy(&value, object, 1);
                return Quoted(StringView(&value, 1));
            }
            case TypeKind::SignedInteger:
                return Number(LoadSigned(object, type.size));
            case TypeKind::UnsignedInteger:
                return Number(LoadUnsigned(object, type.size));
            case TypeKind::FloatingPoint:
                return WriteFloat(object, type.size);
            case TypeKind::Enum:
                return WriteEnum(type, object);
            case TypeKind::String:
                return Quoted(type.stringOperations->view(object));
            case TypeKind::Handle: {
                u32 parts[2] = {};
                Memory::Copy(parts, object, sizeof(parts));
                NX_TRY(Raw("["));
                NX_TRY(Number(parts[0]));
                NX_TRY(Raw(", "));
                NX_TRY(Number(parts[1]));
                return Raw("]");
            }
            case TypeKind::FixedArray: {
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                return WriteElements(*element, type.count, indent, depth, [object, element](usize index) {
                    return static_cast<const void*>(Advance(object, index * element->size));
                });
            }
            case TypeKind::Array: {
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                const Reflection::ArrayOperations& operations = *type.arrayOperations;
                return WriteElements(*element, operations.size(object), indent, depth,
                                     [object, &operations](usize index) { return operations.atConst(object, index); });
            }
            case TypeKind::HashMap: {
                MapVisit visit;
                visit.writer = this;
                NX_TRY_ASSIGN(const TypeInfo* const key, Resolve(type.element));
                NX_TRY_ASSIGN(const TypeInfo* const value, Resolve(type.value));
                visit.key = key;
                visit.value = value;
                visit.indent = indent;
                visit.depth = depth + 1;
                if (type.mapOperations->size(object) == 0) {
                    return Raw("[]");
                }
                NX_TRY(Raw("["));
                type.mapOperations->visit(object, &WriteEntry, &visit);
                NX_TRY(visit.status);
                NX_TRY(Raw("\n"));
                NX_TRY(Indent(indent));
                return Raw("]");
            }
            case TypeKind::Record: {
                bool first = true;
                for (const FieldInfo& field : type.fields) {
                    if (!Detail::IsSerialized(field)) {
                        continue;
                    }
                    NX_TRY_ASSIGN(const TypeInfo* const child, Resolve(field.type));
                    NX_TRY(Raw(first ? StringView("{\n") : StringView(",\n")));
                    first = false;
                    NX_TRY(Indent(indent + 1));
                    NX_TRY(Quoted(Name(field.name)));
                    NX_TRY(Raw(": "));
                    NX_TRY(WriteValue(*child, Advance(object, field.offset), indent + 1, depth + 1));
                }
                if (first) {
                    return Raw("{}");
                }
                NX_TRY(Raw("\n"));
                NX_TRY(Indent(indent));
                return Raw("}");
            }
            case TypeKind::Opaque:
                break;
        }
        return Error(SerializationError::NotSerializable);
    }

    const TypeRegistry& m_registry;
    Containers::String& m_out;
    Memory::TrackingAllocator m_memory;
    Containers::Array<const TypeInfo*> m_versioned;
};

// ---- Reading --------------------------------------------------------------------------------------------

[[nodiscard]] Error Malformed() {
    return Error(SerializationError::InvalidFormat);
}

class TextReader final {
public:
    TextReader(const TypeRegistry& registry, StringView text, Memory::IAllocator& allocator)
        : m_registry(registry), m_text(text), m_allocator(allocator), m_memory(allocator, kSerializationMemoryTag),
          m_scratch(m_memory) {}

    Result<void, Error> Run(const TypeInfo& root, void* object) {
        NX_TRY(Expect('{'));
        bool sawFormat = false;
        bool sawType = false;
        bool sawValue = false;
        if (!Consume('}')) {
            do {
                NX_TRY(ReadString());
                const StringView key = m_scratch.View();
                NX_TRY(Expect(':'));
                if (key == StringView("nexyl")) {
                    NX_TRY_ASSIGN(const Span<const char> token, ReadNumberToken());
                    const Result<u64, Error> version = ParseU64(token);
                    if (version.IsError() || version.GetValue() == 0) {
                        return Malformed();
                    }
                    if (version.GetValue() > kTextFormatVersion) {
                        return Error(SerializationError::UnsupportedVersion);
                    }
                    sawFormat = true;
                } else if (key == StringView("type")) {
                    NX_TRY(ReadString());
                    const StringView name = m_scratch.View();
                    if (name != Name(root.name) && name != Name(root.renamedFrom)) {
                        return Error(SerializationError::TypeMismatch);
                    }
                    sawType = true;
                } else if (key == StringView("value")) {
                    if (!sawFormat || !sawType || sawValue) {
                        return Malformed();
                    }
                    NX_TRY(ReadValue(root, object, 0));
                    sawValue = true;
                } else {
                    NX_TRY(SkipValue(0));
                }
            } while (Consume(','));
            NX_TRY(Expect('}'));
        }
        SkipWhitespace();
        if (!sawValue || m_position != m_text.Size()) {
            return Malformed();
        }
        return {};
    }

private:
    void SkipWhitespace() {
        while (m_position < m_text.Size()) {
            const char character = m_text[m_position];
            if (character != ' ' && character != '\n' && character != '\r' && character != '\t') {
                return;
            }
            ++m_position;
        }
    }

    [[nodiscard]] char Peek() {
        SkipWhitespace();
        return m_position < m_text.Size() ? m_text[m_position] : '\0';
    }

    [[nodiscard]] bool Consume(char character) {
        if (Peek() == character && m_position < m_text.Size()) {
            ++m_position;
            return true;
        }
        return false;
    }

    Result<void, Error> Expect(char character) {
        return Consume(character) ? Result<void, Error>() : Result<void, Error>(Malformed());
    }

    Result<void, Error> Literal(StringView word) {
        if (m_text.Size() - m_position < word.Size() || m_text.Substring(m_position, word.Size()) != word) {
            return Malformed();
        }
        m_position += word.Size();
        return {};
    }

    [[nodiscard]] bool IsDigit(usize position) const {
        return position < m_text.Size() && m_text[position] >= '0' && m_text[position] <= '9';
    }

    // A JSON number: -? (0 | [1-9][0-9]*) (.[0-9]+)? ([eE][+-]?[0-9]+)?
    Result<Span<const char>, Error> ReadNumberToken() {
        SkipWhitespace();
        const usize start = m_position;
        if (m_position < m_text.Size() && m_text[m_position] == '-') {
            ++m_position;
        }
        if (!IsDigit(m_position)) {
            return Malformed();
        }
        if (m_text[m_position] == '0') {
            ++m_position;
        } else {
            while (IsDigit(m_position)) {
                ++m_position;
            }
        }
        if (m_position < m_text.Size() && m_text[m_position] == '.') {
            ++m_position;
            if (!IsDigit(m_position)) {
                return Malformed();
            }
            while (IsDigit(m_position)) {
                ++m_position;
            }
        }
        if (m_position < m_text.Size() && (m_text[m_position] == 'e' || m_text[m_position] == 'E')) {
            ++m_position;
            if (m_position < m_text.Size() && (m_text[m_position] == '+' || m_text[m_position] == '-')) {
                ++m_position;
            }
            if (!IsDigit(m_position)) {
                return Malformed();
            }
            while (IsDigit(m_position)) {
                ++m_position;
            }
        }
        return Span<const char>(m_text.Data() + start, m_position - start);
    }

    Result<void, Error> Append(char character) {
        NX_TRY(m_scratch.TryReserve(m_scratch.Size() + 1));
        m_scratch.Append(character);
        return {};
    }

    Result<void, Error> AppendCodePoint(u32 codePoint) {
        if (codePoint < 0x80U) {
            return Append(static_cast<char>(codePoint));
        }
        if (codePoint < 0x800U) {
            NX_TRY(Append(static_cast<char>(0xC0U | (codePoint >> 6U))));
            return Append(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        if (codePoint < 0x10000U) {
            NX_TRY(Append(static_cast<char>(0xE0U | (codePoint >> 12U))));
            NX_TRY(Append(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU))));
            return Append(static_cast<char>(0x80U | (codePoint & 0x3FU)));
        }
        NX_TRY(Append(static_cast<char>(0xF0U | (codePoint >> 18U))));
        NX_TRY(Append(static_cast<char>(0x80U | ((codePoint >> 12U) & 0x3FU))));
        NX_TRY(Append(static_cast<char>(0x80U | ((codePoint >> 6U) & 0x3FU))));
        return Append(static_cast<char>(0x80U | (codePoint & 0x3FU)));
    }

    Result<u32, Error> ReadHex4() {
        if (m_text.Size() - m_position < 4) {
            return Malformed();
        }
        u32 value = 0;
        for (u32 digit = 0; digit < 4; ++digit) {
            const char character = m_text[m_position++];
            u32 nibble = 0;
            if (character >= '0' && character <= '9') {
                nibble = static_cast<u32>(character - '0');
            } else if (character >= 'a' && character <= 'f') {
                nibble = static_cast<u32>(character - 'a') + 10U;
            } else if (character >= 'A' && character <= 'F') {
                nibble = static_cast<u32>(character - 'A') + 10U;
            } else {
                return Malformed();
            }
            value = (value << 4U) | nibble;
        }
        return value;
    }

    // Reads a JSON string into m_scratch, decoding escapes to UTF-8.
    Result<void, Error> ReadString() {
        NX_TRY(Expect('"'));
        m_scratch.Clear();
        for (;;) {
            if (m_position >= m_text.Size()) {
                return Malformed();
            }
            const char character = m_text[m_position++];
            if (character == '"') {
                return {};
            }
            if (static_cast<u8>(character) < 0x20U) {
                return Malformed();
            }
            if (character != '\\') {
                NX_TRY(Append(character));
                continue;
            }
            if (m_position >= m_text.Size()) {
                return Malformed();
            }
            const char escape = m_text[m_position++];
            switch (escape) {
                case '"':
                case '\\':
                case '/':
                    NX_TRY(Append(escape));
                    break;
                case 'b':
                    NX_TRY(Append('\b'));
                    break;
                case 'f':
                    NX_TRY(Append('\f'));
                    break;
                case 'n':
                    NX_TRY(Append('\n'));
                    break;
                case 'r':
                    NX_TRY(Append('\r'));
                    break;
                case 't':
                    NX_TRY(Append('\t'));
                    break;
                case 'u': {
                    NX_TRY_ASSIGN(u32 codePoint, ReadHex4());
                    if (codePoint >= 0xDC00U && codePoint <= 0xDFFFU) {
                        return Malformed(); // a low surrogate without its high one
                    }
                    if (codePoint >= 0xD800U && codePoint <= 0xDBFFU) {
                        NX_TRY(Literal("\\u"));
                        NX_TRY_ASSIGN(const u32 low, ReadHex4());
                        if (low < 0xDC00U || low > 0xDFFFU) {
                            return Malformed();
                        }
                        codePoint = 0x10000U + ((codePoint - 0xD800U) << 10U) + (low - 0xDC00U);
                    }
                    NX_TRY(AppendCodePoint(codePoint));
                    break;
                }
                default:
                    return Malformed();
            }
        }
    }

    // Validates and passes over any JSON value.
    Result<void, Error> SkipValue(u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        const char next = Peek();
        if (next == '"') {
            return ReadString();
        }
        if (next == '{') {
            ++m_position;
            if (Consume('}')) {
                return {};
            }
            do {
                NX_TRY(ReadString());
                NX_TRY(Expect(':'));
                NX_TRY(SkipValue(depth + 1));
            } while (Consume(','));
            return Expect('}');
        }
        if (next == '[') {
            ++m_position;
            if (Consume(']')) {
                return {};
            }
            do {
                NX_TRY(SkipValue(depth + 1));
            } while (Consume(','));
            return Expect(']');
        }
        if (next == 't') {
            return Literal("true");
        }
        if (next == 'f') {
            return Literal("false");
        }
        if (next == 'n') {
            return Literal("null");
        }
        NX_TRY_ASSIGN(const Span<const char> token, ReadNumberToken());
        static_cast<void>(token);
        return {};
    }

    Result<const TypeInfo*, Error> Resolve(Reflection::TypeId id) const {
        const TypeInfo* const type = m_registry.Find(id);
        if (type == nullptr) {
            return Error(SerializationError::UnknownType);
        }
        return type;
    }

    Result<void, Error> ReadInteger(const TypeInfo& type, void* target) {
        if (Peek() != '-' && !IsDigit(m_position)) {
            return SkipValue(0);
        }
        NX_TRY_ASSIGN(const Span<const char> token, ReadNumberToken());
        if (type.kind == TypeKind::SignedInteger) {
            const Result<i64, Error> value = ParseI64(token);
            if (value.IsOk() && FitsSigned(value.GetValue(), type.size)) {
                StoreInteger(target, static_cast<u64>(value.GetValue()), type.size);
            }
        } else {
            const Result<u64, Error> value = ParseU64(token);
            if (value.IsOk() && FitsUnsigned(value.GetValue(), type.size)) {
                StoreInteger(target, value.GetValue(), type.size);
            }
        }
        return {};
    }

    Result<void, Error> ReadFloat(const TypeInfo& type, void* target) {
        Span<const char> text;
        if (Peek() == '"') {
            NX_TRY(ReadString()); // "nan", "inf" or "-inf"
            text = Span<const char>(m_scratch.Data(), m_scratch.Size());
        } else if (Peek() == '-' || IsDigit(m_position)) {
            NX_TRY_ASSIGN(const Span<const char> token, ReadNumberToken());
            text = token;
        } else {
            return SkipValue(0);
        }
        if (type.size == sizeof(f32)) {
            const Result<f32, Error> value = ParseF32(text);
            if (value.IsOk()) {
                const f32 parsed = value.GetValue();
                Memory::Copy(target, &parsed, sizeof(parsed));
            }
        } else {
            const Result<f64, Error> value = ParseF64(text);
            if (value.IsOk()) {
                const f64 parsed = value.GetValue();
                Memory::Copy(target, &parsed, sizeof(parsed));
            }
        }
        return {};
    }

    Result<void, Error> ReadEnum(const TypeInfo& type, void* target) {
        NX_TRY_ASSIGN(const TypeInfo* const underlying, Resolve(type.element));
        if (Peek() == '"') {
            NX_TRY(ReadString());
            const StringView name = m_scratch.View();
            for (const Reflection::EnumValueInfo& enumerator : type.enumValues) {
                if (name == Name(enumerator.name) || name == Name(enumerator.renamedFrom)) {
                    StoreInteger(target, static_cast<u64>(enumerator.value), type.size);
                    return {};
                }
            }
            return {}; // a value that no longer exists: the field keeps its value
        }
        TypeInfo integer = *underlying;
        integer.size = type.size;
        return ReadInteger(integer, target);
    }

    // Reads the elements of a JSON array; `slotAt` gives the target of an element or nullptr to skip it.
    template <typename TSlotAt>
    Result<void, Error> ReadElements(const TypeInfo* element, u32 depth, const TSlotAt& slotAt) {
        NX_TRY(Expect('['));
        if (Consume(']')) {
            return {};
        }
        usize index = 0;
        do {
            NX_TRY_ASSIGN(void* const slot, slotAt(index));
            NX_TRY(slot != nullptr ? ReadValue(*element, slot, depth + 1) : SkipValue(depth + 1));
            ++index;
        } while (Consume(','));
        return Expect(']');
    }

    struct Temporary {
        Memory::IAllocator* memory = nullptr;
        const TypeInfo* type = nullptr;
        void* storage = nullptr;

        Temporary() = default;
        Temporary(const Temporary&) = delete;
        Temporary& operator=(const Temporary&) = delete;
        Temporary(Temporary&&) = delete;
        Temporary& operator=(Temporary&&) = delete;
        ~Temporary() {
            if (storage != nullptr) {
                type->lifecycle.destruct(storage);
                memory->Deallocate(storage, type->size);
            }
        }

        Result<void, Error> Create(Memory::IAllocator& allocator, const TypeInfo& info) {
            memory = &allocator;
            type = &info;
            NX_TRY_ASSIGN(void* const block, allocator.TryAllocate(info.size, info.alignment));
            info.lifecycle.construct(block);
            storage = block;
            return {};
        }
    };

    Result<void, Error> ReadMap(const TypeInfo& type, void* target, u32 depth) {
        NX_TRY_ASSIGN(const TypeInfo* const key, Resolve(type.element));
        NX_TRY_ASSIGN(const TypeInfo* const value, Resolve(type.value));
        if (key->lifecycle.construct == nullptr || value->lifecycle.construct == nullptr) {
            return SkipValue(depth);
        }
        NX_TRY(Expect('['));
        if (Consume(']')) {
            return {};
        }
        do {
            if (depth + 1 > kMaxDepth) {
                return Error(SerializationError::LimitExceeded);
            }
            Temporary storedKey;
            Temporary storedValue;
            NX_TRY(storedKey.Create(m_memory, *key));
            NX_TRY(storedValue.Create(m_memory, *value));
            NX_TRY(Expect('['));
            NX_TRY(ReadValue(*key, storedKey.storage, depth + 2));
            NX_TRY(Expect(','));
            NX_TRY(ReadValue(*value, storedValue.storage, depth + 2));
            NX_TRY(Expect(']'));
            NX_TRY(type.mapOperations->set(target, storedKey.storage, storedValue.storage, m_allocator));
        } while (Consume(','));
        return Expect(']');
    }

    Result<void, Error> ReadRecord(const TypeInfo& type, void* target, u32 depth) {
        NX_TRY(Expect('{'));
        if (Consume('}')) {
            return {};
        }
        do {
            NX_TRY(ReadString());
            const FieldInfo* match = nullptr;
            const StringView key = m_scratch.View();
            for (const FieldInfo& field : type.fields) {
                if (Detail::IsSerialized(field) && (key == Name(field.name) || key == Name(field.renamedFrom))) {
                    match = &field;
                    break;
                }
            }
            NX_TRY(Expect(':'));
            if (match == nullptr) {
                NX_TRY(SkipValue(depth + 1));
                continue;
            }
            NX_TRY_ASSIGN(const TypeInfo* const child, Resolve(match->type));
            NX_TRY(ReadValue(*child, Advance(target, match->offset), depth + 1));
        } while (Consume(','));
        return Expect('}');
    }

    // Reads one JSON value into a value of `type`. A value of the wrong JSON kind is skipped.
    Result<void, Error> ReadValue(const TypeInfo& type, void* target, u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        const char next = Peek();
        switch (type.kind) {
            case TypeKind::Bool:
                if (next == 't' || next == 'f') {
                    const bool value = next == 't';
                    NX_TRY(Literal(value ? StringView("true") : StringView("false")));
                    Memory::Copy(target, &value, 1);
                    return {};
                }
                return SkipValue(depth);
            case TypeKind::Character:
                if (next == '"') {
                    NX_TRY(ReadString());
                    if (m_scratch.Size() == 1) {
                        Memory::Copy(target, m_scratch.Data(), 1);
                    }
                    return {};
                }
                return SkipValue(depth);
            case TypeKind::SignedInteger:
            case TypeKind::UnsignedInteger:
                return ReadInteger(type, target);
            case TypeKind::FloatingPoint:
                return ReadFloat(type, target);
            case TypeKind::Enum:
                return ReadEnum(type, target);
            case TypeKind::String:
                if (next == '"') {
                    NX_TRY(ReadString());
                    return type.stringOperations->assign(target, m_scratch.View(), m_allocator);
                }
                return SkipValue(depth);
            case TypeKind::Handle: {
                if (next != '[') {
                    return SkipValue(depth);
                }
                u32 parts[2] = {};
                NX_TRY(ReadHandle(parts));
                Memory::Copy(target, parts, sizeof(parts));
                return {};
            }
            case TypeKind::FixedArray: {
                if (next != '[') {
                    return SkipValue(depth);
                }
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                return ReadElements(element, depth, [target, element, &type](usize index) -> Result<void*, Error> {
                    return index < type.count ? static_cast<void*>(Advance(target, index * element->size)) : nullptr;
                });
            }
            case TypeKind::Array: {
                if (next != '[' || type.arrayOperations->resize == nullptr) {
                    return SkipValue(depth);
                }
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                const Reflection::ArrayOperations& operations = *type.arrayOperations;
                NX_TRY(operations.resize(target, 0, m_allocator));
                usize capacity = 0;
                usize used = 0;
                // Grow geometrically, so that long arrays are not resized per element.
                const auto slotAt = [this, target, &operations, &capacity, &used](usize index) -> Result<void*, Error> {
                    if (index >= capacity) {
                        capacity = capacity == 0 ? 4 : capacity * 2;
                        NX_TRY(operations.resize(target, capacity, m_allocator));
                    }
                    used = index + 1;
                    return operations.at(target, index);
                };
                NX_TRY(ReadElements(element, depth, slotAt));
                return operations.resize(target, used, m_allocator);
            }
            case TypeKind::HashMap:
                if (next != '[') {
                    return SkipValue(depth);
                }
                return ReadMap(type, target, depth);
            case TypeKind::Record:
                if (next != '{') {
                    return SkipValue(depth);
                }
                return ReadRecord(type, target, depth);
            case TypeKind::Opaque:
                break;
        }
        return Error(SerializationError::NotSerializable);
    }

    Result<void, Error> ReadHandle(u32 (&parts)[2]) {
        NX_TRY(Expect('['));
        for (u32 part = 0; part < 2; ++part) {
            if (part == 1) {
                NX_TRY(Expect(','));
            }
            NX_TRY_ASSIGN(const Span<const char> token, ReadNumberToken());
            const Result<u64, Error> value = ParseU64(token);
            if (value.IsError() || value.GetValue() > 0xFFFF'FFFFU) {
                return Error(SerializationError::InvalidValue);
            }
            parts[part] = static_cast<u32>(value.GetValue());
        }
        return Expect(']');
    }

    const TypeRegistry& m_registry;
    StringView m_text;
    usize m_position = 0;
    Memory::IAllocator& m_allocator;
    Memory::TrackingAllocator m_memory;
    Containers::String m_scratch;
};

} // namespace

Result<void, Error> WriteText(const TypeRegistry& registry, const TypeInfo& type, const void* object,
                              Containers::String& out) {
    NX_ASSERT(out.GetAllocator() != nullptr, "WriteText needs an output string with an allocator.");
    TextWriter writer(registry, out);
    return writer.Run(type, object);
}

Result<void, Error> ReadText(const TypeRegistry& registry, Containers::StringView text, const TypeInfo& type,
                             void* object, Memory::IAllocator& allocator) {
    TextReader reader(registry, text, allocator);
    return reader.Run(type, object);
}

} // namespace Nexyl::Serialization
