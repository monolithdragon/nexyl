#include <Nexyl/Serialization/BinaryArchive.h>

#include "SchemaHash.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Reflection/TypeId.h>

// The binary archive, format version 1. All numbers are little-endian.
//
//   header   "NXBA", u16 format version, u16 zero, u32 type count, u32 root type index
//   schema   one entry per type, referred to by index:
//            u16 name length, name, u64 TypeId, u8 kind, u32 size, u32 schema version, u64 schema hash, then
//            Record: u32 field count, per field u16 name length, name, u32 type index
//            FixedArray: u32 element index, u64 count   Array: u32 element index
//            HashMap: u32 key index, u32 value index    other kinds: nothing
//   value    the root value: fundamentals and enumerations as their bytes, Handle as index and generation,
//            String as u32 length and bytes, Array as u32 count and elements, HashMap as u32 count and key-value
//            pairs, FixedArray as its elements, Record as its serialized fields in schema order.

namespace Nexyl::Serialization {

namespace {

using Reflection::FieldInfo;
using Reflection::TypeInfo;
using Reflection::TypeKind;
using Reflection::TypeRegistry;

constexpr u8 kMagic[4] = {'N', 'X', 'B', 'A'};
constexpr u32 kMaxDepth = 64;
constexpr u32 kMaxTypes = u32{1} << 16U;
constexpr u64 kMaxFixedCount = u64{1} << 24U;
constexpr u64 kMaxEmptyElements = u64{1} << 16U;
constexpr u32 kNoField = ~u32{0};
constexpr u64 kSaturated = ~u64{0};

[[nodiscard]] bool IsPrimitive(TypeKind kind) {
    return kind == TypeKind::Bool || kind == TypeKind::Character || kind == TypeKind::SignedInteger ||
           kind == TypeKind::UnsignedInteger || kind == TypeKind::FloatingPoint;
}

[[nodiscard]] Containers::StringView Name(const char* text) {
    return Containers::StringView::FromCString(text);
}

[[nodiscard]] u8* Advance(void* base, usize offset) {
    return static_cast<u8*>(base) + offset;
}

[[nodiscard]] const u8* Advance(const void* base, usize offset) {
    return static_cast<const u8*>(base) + offset;
}

// ---- Writing --------------------------------------------------------------------------------------------

class Writer final {
public:
    Writer(const TypeRegistry& registry, Containers::Array<u8>& out)
        : m_registry(registry), m_out(out), m_memory(*out.GetAllocator(), kSerializationMemoryTag), m_types(m_memory),
          m_indices(m_memory) {}

    Result<void, Error> Run(const TypeInfo& root, const void* object) {
        NX_TRY_ASSIGN(const u32 rootIndex, Collect(root, 0));
        NX_TRY(Bytes(kMagic, sizeof(kMagic)));
        NX_TRY(Put(kBinaryFormatVersion));
        NX_TRY(Put(u16{0}));
        NX_TRY(Put(static_cast<u32>(m_types.Size())));
        NX_TRY(Put(rootIndex));
        for (const TypeInfo* const type : m_types) {
            NX_TRY(WriteSchema(*type));
        }
        return WriteValue(root, object, 0);
    }

private:
    Result<void, Error> Bytes(const void* data, usize size) {
        NX_TRY(m_out.TryReserve(m_out.Size() + size));
        m_out.Append(Span<const u8>(static_cast<const u8*>(data), size));
        return {};
    }

    template <typename T>
    Result<void, Error> Put(T value) {
        return Bytes(&value, sizeof(T));
    }

    Result<void, Error> PutName(const char* text) {
        const Containers::StringView name = Name(text);
        if (name.Size() > 0xFFFFU) {
            return Error(SerializationError::LimitExceeded);
        }
        NX_TRY(Put(static_cast<u16>(name.Size())));
        return Bytes(name.Data(), name.Size());
    }

    Result<const TypeInfo*, Error> Resolve(Reflection::TypeId id) const {
        const TypeInfo* const type = m_registry.Find(id);
        if (type == nullptr) {
            return Error(SerializationError::UnknownType);
        }
        return type;
    }

    // Gives the type, and every type it contains, an index in the schema.
    Result<u32, Error> Collect(const TypeInfo& type, u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        if (const u32* const known = m_indices.Find(type.id)) {
            return *known;
        }
        if (type.kind == TypeKind::Opaque || (type.kind == TypeKind::Record && !type.base.IsNull())) {
            return Error(SerializationError::NotSerializable);
        }
        const auto index = static_cast<u32>(m_types.Size());
        NX_TRY(m_types.TryReserve(m_types.Size() + 1));
        NX_TRY(m_indices.TryReserve(m_indices.Size() + 1));
        m_types.Push(&type);
        m_indices.Set(type.id, index);
        switch (type.kind) {
            case TypeKind::Record:
                for (const FieldInfo& field : type.fields) {
                    if (Detail::IsSerialized(field)) {
                        NX_TRY_ASSIGN(const TypeInfo* const child, Resolve(field.type));
                        NX_TRY(Collect(*child, depth + 1));
                    }
                }
                break;
            case TypeKind::FixedArray:
            case TypeKind::Array: {
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                NX_TRY(Collect(*element, depth + 1));
                break;
            }
            case TypeKind::HashMap: {
                NX_TRY_ASSIGN(const TypeInfo* const key, Resolve(type.element));
                NX_TRY(Collect(*key, depth + 1));
                NX_TRY_ASSIGN(const TypeInfo* const value, Resolve(type.value));
                NX_TRY(Collect(*value, depth + 1));
                break;
            }
            default:
                break;
        }
        return index;
    }

    [[nodiscard]] u32 IndexOf(Reflection::TypeId id) const {
        const u32* const index = m_indices.Find(id);
        NX_ASSERT(index != nullptr, "Every contained type was collected.");
        return index != nullptr ? *index : 0;
    }

    Result<void, Error> WriteSchema(const TypeInfo& type) {
        NX_TRY(PutName(type.name));
        NX_TRY(Put(type.id.value));
        NX_TRY(Put(static_cast<u8>(type.kind)));
        NX_TRY(Put(static_cast<u32>(type.size)));
        NX_TRY(Put(type.version));
        NX_TRY(Put(Detail::SchemaHash(type)));
        switch (type.kind) {
            case TypeKind::Record: {
                u32 count = 0;
                for (const FieldInfo& field : type.fields) {
                    count += Detail::IsSerialized(field) ? 1U : 0U;
                }
                NX_TRY(Put(count));
                for (const FieldInfo& field : type.fields) {
                    if (Detail::IsSerialized(field)) {
                        NX_TRY(PutName(field.name));
                        NX_TRY(Put(IndexOf(field.type)));
                    }
                }
                break;
            }
            case TypeKind::FixedArray:
                NX_TRY(Put(IndexOf(type.element)));
                NX_TRY(Put(static_cast<u64>(type.count)));
                break;
            case TypeKind::Array:
                NX_TRY(Put(IndexOf(type.element)));
                break;
            case TypeKind::HashMap:
                NX_TRY(Put(IndexOf(type.element)));
                NX_TRY(Put(IndexOf(type.value)));
                break;
            default:
                break;
        }
        return {};
    }

    struct MapVisit {
        Writer* writer = nullptr;
        const TypeInfo* key = nullptr;
        const TypeInfo* value = nullptr;
        u32 depth = 0;
        Result<void, Error> status;
    };

    static void WriteEntry(void* context, const void* key, const void* value) {
        auto& visit = *static_cast<MapVisit*>(context);
        if (visit.status.IsOk()) {
            visit.status = visit.writer->WriteValue(*visit.key, key, visit.depth);
        }
        if (visit.status.IsOk()) {
            visit.status = visit.writer->WriteValue(*visit.value, value, visit.depth);
        }
    }

    Result<void, Error> WriteValue(const TypeInfo& type, const void* object, u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        switch (type.kind) {
            case TypeKind::Bool:
            case TypeKind::Character:
            case TypeKind::SignedInteger:
            case TypeKind::UnsignedInteger:
            case TypeKind::FloatingPoint:
            case TypeKind::Enum:
            case TypeKind::Handle:
                return Bytes(object, type.size);
            case TypeKind::String: {
                const Containers::StringView text = type.stringOperations->view(object);
                if (text.Size() > 0xFFFF'FFFFU) {
                    return Error(SerializationError::LimitExceeded);
                }
                NX_TRY(Put(static_cast<u32>(text.Size())));
                return Bytes(text.Data(), text.Size());
            }
            case TypeKind::Record:
                for (const FieldInfo& field : type.fields) {
                    if (Detail::IsSerialized(field)) {
                        NX_TRY_ASSIGN(const TypeInfo* const child, Resolve(field.type));
                        NX_TRY(WriteValue(*child, Advance(object, field.offset), depth + 1));
                    }
                }
                return {};
            case TypeKind::FixedArray: {
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                for (usize index = 0; index < type.count; ++index) {
                    NX_TRY(WriteValue(*element, Advance(object, index * element->size), depth + 1));
                }
                return {};
            }
            case TypeKind::Array: {
                NX_TRY_ASSIGN(const TypeInfo* const element, Resolve(type.element));
                const usize count = type.arrayOperations->size(object);
                if (count > 0xFFFF'FFFFU) {
                    return Error(SerializationError::LimitExceeded);
                }
                NX_TRY(Put(static_cast<u32>(count)));
                for (usize index = 0; index < count; ++index) {
                    NX_TRY(WriteValue(*element, type.arrayOperations->atConst(object, index), depth + 1));
                }
                return {};
            }
            case TypeKind::HashMap: {
                MapVisit visit;
                visit.writer = this;
                NX_TRY_ASSIGN(const TypeInfo* const key, Resolve(type.element));
                NX_TRY_ASSIGN(const TypeInfo* const value, Resolve(type.value));
                visit.key = key;
                visit.value = value;
                visit.depth = depth + 1;
                const usize count = type.mapOperations->size(object);
                if (count > 0xFFFF'FFFFU) {
                    return Error(SerializationError::LimitExceeded);
                }
                NX_TRY(Put(static_cast<u32>(count)));
                type.mapOperations->visit(object, &WriteEntry, &visit);
                return visit.status;
            }
            case TypeKind::Opaque:
                break;
        }
        return Error(SerializationError::NotSerializable);
    }

    const TypeRegistry& m_registry;
    Containers::Array<u8>& m_out;
    Memory::TrackingAllocator m_memory;
    Containers::Array<const TypeInfo*> m_types;
    Containers::HashMap<Reflection::TypeId, u32> m_indices;
};

// ---- Reading --------------------------------------------------------------------------------------------

class ByteReader final {
public:
    explicit ByteReader(Span<const u8> data) : m_data(data) {}

    [[nodiscard]] usize Remaining() const {
        return m_data.Size() - m_position;
    }

    Result<Span<const u8>, Error> Take(usize size) {
        if (size > Remaining()) {
            return Error(SerializationError::Truncated);
        }
        const Span<const u8> bytes = m_data.Subspan(m_position, size);
        m_position += size;
        return bytes;
    }

    // Copies `size` bytes to `destination`, or skips them when it is null.
    Result<void, Error> Copy(void* destination, usize size) {
        NX_TRY_ASSIGN(const Span<const u8> bytes, Take(size));
        if (destination != nullptr && size > 0) {
            Memory::Copy(destination, bytes.Data(), size);
        }
        return {};
    }

    template <typename T>
    Result<T, Error> Get() {
        T value{};
        NX_TRY(Copy(&value, sizeof(T)));
        return value;
    }

    template <typename T>
    Result<void, Error> Read(T& value) {
        return Copy(&value, sizeof(T));
    }

    Result<void, Error> ReadName(Containers::StringView& name) {
        NX_TRY_ASSIGN(const Containers::StringView read, GetName());
        name = read;
        return {};
    }

    Result<Containers::StringView, Error> GetName() {
        NX_TRY_ASSIGN(const u16 length, Get<u16>());
        NX_TRY_ASSIGN(const Span<const u8> bytes, Take(length));
        const Span<const char> text = AsCharacters(bytes);
        return Containers::StringView(text.Data(), text.Size());
    }

private:
    Span<const u8> m_data;
    usize m_position = 0;
};

struct StoredField {
    Containers::StringView name;
    u32 type = 0;
    // The index in the current type's fields that receives this field, or kNoField; valid for `resolved`.
    u32 current = kNoField;
};

struct StoredType {
    Containers::StringView name;
    u64 id = 0;
    TypeKind kind = TypeKind::Opaque;
    u32 size = 0;
    u32 version = 0;
    u64 hash = 0;
    u32 firstField = 0;
    u32 fieldCount = 0;
    u32 element = 0;
    u32 value = 0;
    u64 count = 0;
    // The smallest encoded size of a value, to bound counts by the remaining data.
    u64 minimumSize = 0;
    u8 sizeState = 0; // 0 not computed, 1 computing, 2 done
    // The current type the field mapping was made for.
    const TypeInfo* resolved = nullptr;
};

[[nodiscard]] bool ValidSize(TypeKind kind, u32 size) {
    switch (kind) {
        case TypeKind::Bool:
        case TypeKind::Character:
            return size == 1;
        case TypeKind::SignedInteger:
        case TypeKind::UnsignedInteger:
        case TypeKind::Enum:
            return size == 1 || size == 2 || size == 4 || size == 8;
        case TypeKind::FloatingPoint:
            return size == 4 || size == 8;
        case TypeKind::Handle:
            return size == 8;
        default:
            return true;
    }
}

[[nodiscard]] u64 SaturatingAdd(u64 first, u64 second) {
    return first > kSaturated - second ? kSaturated : first + second;
}

[[nodiscard]] u64 SaturatingMultiply(u64 first, u64 second) {
    return second != 0 && first > kSaturated / second ? kSaturated : first * second;
}

class Reader final {
public:
    Reader(const TypeRegistry& registry, Span<const u8> data, Memory::IAllocator& allocator)
        : m_registry(registry), m_input(data), m_allocator(allocator), m_memory(allocator, kSerializationMemoryTag),
          m_types(m_memory), m_fields(m_memory) {}

    Result<void, Error> Run(const TypeInfo& root, void* object) {
        NX_TRY_ASSIGN(const Span<const u8> magic, m_input.Take(sizeof(kMagic)));
        for (usize index = 0; index < sizeof(kMagic); ++index) {
            if (magic[index] != kMagic[index]) {
                return Error(SerializationError::InvalidFormat);
            }
        }
        NX_TRY_ASSIGN(const u16 version, m_input.Get<u16>());
        NX_TRY_ASSIGN(const u16 reserved, m_input.Get<u16>());
        if (version == 0 || reserved != 0) {
            return Error(SerializationError::InvalidFormat);
        }
        if (version > kBinaryFormatVersion) {
            return Error(SerializationError::UnsupportedVersion);
        }
        NX_TRY_ASSIGN(const u32 count, m_input.Get<u32>());
        NX_TRY_ASSIGN(const u32 rootIndex, m_input.Get<u32>());
        if (count == 0 || count > kMaxTypes || rootIndex >= count) {
            return Error(SerializationError::InvalidFormat);
        }
        NX_TRY(m_types.TryReserve(count));
        for (u32 index = 0; index < count; ++index) {
            NX_TRY(ParseType());
        }
        NX_TRY(ValidateSchema());

        const StoredType& stored = m_types[rootIndex];
        const bool sameName = stored.name == Name(root.name) || stored.name == Name(root.renamedFrom);
        if ((!sameName && stored.id != root.id.value) || !Compatible(stored, root)) {
            return Error(SerializationError::TypeMismatch);
        }
        NX_TRY(ReadValue(rootIndex, &root, object, 0));
        if (m_input.Remaining() != 0) {
            return Error(SerializationError::InvalidFormat);
        }
        return {};
    }

private:
    Result<void, Error> ParseType() {
        StoredType type;
        NX_TRY(m_input.ReadName(type.name));
        NX_TRY(m_input.Read(type.id));
        NX_TRY_ASSIGN(const u8 kind, m_input.Get<u8>());
        if (kind > static_cast<u8>(TypeKind::Handle) || kind == static_cast<u8>(TypeKind::Opaque)) {
            return Error(SerializationError::InvalidFormat);
        }
        type.kind = static_cast<TypeKind>(kind);
        NX_TRY(m_input.Read(type.size));
        if (!ValidSize(type.kind, type.size)) {
            return Error(SerializationError::InvalidFormat);
        }
        NX_TRY(m_input.Read(type.version));
        NX_TRY(m_input.Read(type.hash));
        switch (type.kind) {
            case TypeKind::Record: {
                NX_TRY(m_input.Read(type.fieldCount));
                // A field takes at least six bytes: an empty name and a type index.
                if (type.fieldCount > m_input.Remaining() / 6) {
                    return Error(SerializationError::Truncated);
                }
                type.firstField = static_cast<u32>(m_fields.Size());
                NX_TRY(m_fields.TryReserve(m_fields.Size() + type.fieldCount));
                for (u32 field = 0; field < type.fieldCount; ++field) {
                    StoredField stored;
                    NX_TRY(m_input.ReadName(stored.name));
                    NX_TRY(m_input.Read(stored.type));
                    m_fields.Push(stored);
                }
                break;
            }
            case TypeKind::FixedArray:
                NX_TRY(m_input.Read(type.element));
                NX_TRY(m_input.Read(type.count));
                if (type.count > kMaxFixedCount) {
                    return Error(SerializationError::LimitExceeded);
                }
                break;
            case TypeKind::Array:
                NX_TRY(m_input.Read(type.element));
                break;
            case TypeKind::HashMap:
                NX_TRY(m_input.Read(type.element));
                NX_TRY(m_input.Read(type.value));
                break;
            default:
                break;
        }
        m_types.Push(type);
        return {};
    }

    Result<void, Error> ValidateSchema() {
        const auto count = static_cast<u32>(m_types.Size());
        for (const StoredType& type : m_types) {
            if (type.element >= count || type.value >= count) {
                return Error(SerializationError::InvalidFormat);
            }
        }
        for (const StoredField& field : m_fields) {
            if (field.type >= count) {
                return Error(SerializationError::InvalidFormat);
            }
        }
        for (u32 index = 0; index < count; ++index) {
            NX_TRY(ComputeMinimumSize(index, 0));
        }
        return {};
    }

    // The smallest encoded size of each type; a type that contains itself by value is malformed.
    Result<void, Error> ComputeMinimumSize(u32 index, u32 depth) {
        StoredType& type = m_types[index];
        if (type.sizeState == 2) {
            return {};
        }
        if (type.sizeState == 1 || depth > kMaxDepth) {
            return Error(SerializationError::InvalidFormat);
        }
        type.sizeState = 1;
        u64 size = 0;
        switch (type.kind) {
            case TypeKind::String:
            case TypeKind::Array:
            case TypeKind::HashMap:
                size = 4;
                break;
            case TypeKind::FixedArray:
                NX_TRY(ComputeMinimumSize(type.element, depth + 1));
                size = SaturatingMultiply(m_types[type.element].minimumSize, type.count);
                break;
            case TypeKind::Record:
                for (u32 field = 0; field < type.fieldCount; ++field) {
                    const u32 fieldType = m_fields[type.firstField + field].type;
                    NX_TRY(ComputeMinimumSize(fieldType, depth + 1));
                    size = SaturatingAdd(size, m_types[fieldType].minimumSize);
                }
                break;
            default:
                size = type.size;
                break;
        }
        // The reference may have moved while computing children: index again.
        m_types[index].minimumSize = size;
        m_types[index].sizeState = 2;
        return {};
    }

    [[nodiscard]] static bool Compatible(const StoredType& stored, const TypeInfo& current) {
        if (stored.kind != current.kind) {
            return false;
        }
        if (IsPrimitive(stored.kind) || stored.kind == TypeKind::Enum || stored.kind == TypeKind::Handle) {
            return stored.size == current.size;
        }
        if (stored.kind == TypeKind::Record) {
            return stored.name == Name(current.name) || stored.name == Name(current.renamedFrom);
        }
        return true;
    }

    Result<const TypeInfo*, Error> Resolve(Reflection::TypeId id) const {
        const TypeInfo* const type = m_registry.Find(id);
        if (type == nullptr) {
            return Error(SerializationError::UnknownType);
        }
        return type;
    }

    // Maps the stored fields of a record onto the current type's fields, by name or `@renamed-from`.
    void MapFields(u32 index, const TypeInfo& current) {
        StoredType& type = m_types[index];
        if (type.resolved == &current) {
            return;
        }
        type.resolved = &current;
        const bool identical = type.hash == Detail::SchemaHash(current);
        u32 serialized = 0;
        for (u32 field = 0; field < type.fieldCount; ++field) {
            StoredField& stored = m_fields[type.firstField + field];
            stored.current = kNoField;
            for (u32 candidate = 0; candidate < current.fields.Size(); ++candidate) {
                const FieldInfo& info = current.fields[candidate];
                if (!Detail::IsSerialized(info)) {
                    continue;
                }
                const bool matches = identical
                                         ? serialized == field
                                         : stored.name == Name(info.name) || stored.name == Name(info.renamedFrom);
                ++serialized;
                if (matches) {
                    stored.current = candidate;
                    break;
                }
            }
            serialized = 0;
        }
    }

    Result<void, Error> CheckCount(u64 count, u32 element) const {
        const u64 minimum = m_types[element].minimumSize;
        const bool fits = minimum == 0 ? count <= kMaxEmptyElements : count <= m_input.Remaining() / minimum;
        return fits ? Result<void, Error>() : Result<void, Error>(Error(SerializationError::LimitExceeded));
    }

    struct Temporary {
        Memory::IAllocator* memory = nullptr;
        const TypeInfo* type = nullptr;
        void* storage = nullptr;
        bool constructed = false;

        Temporary() = default;
        Temporary(const Temporary&) = delete;
        Temporary& operator=(const Temporary&) = delete;
        Temporary(Temporary&&) = delete;
        Temporary& operator=(Temporary&&) = delete;
        ~Temporary() {
            if (constructed) {
                type->lifecycle.destruct(storage);
            }
            if (storage != nullptr) {
                memory->Deallocate(storage, type->size);
            }
        }

        Result<void, Error> Create(Memory::IAllocator& allocator, const TypeInfo& info) {
            memory = &allocator;
            type = &info;
            NX_TRY_ASSIGN(void* const block, allocator.TryAllocate(info.size, info.alignment));
            storage = block;
            info.lifecycle.construct(storage);
            constructed = true;
            return {};
        }
    };

    // Reads one value of the stored type. With a target and a compatible current type, the value lands in the
    // target; otherwise it is validated and skipped.
    Result<void, Error> ReadValue(u32 index, const TypeInfo* current, void* target, u32 depth) {
        if (depth > kMaxDepth) {
            return Error(SerializationError::LimitExceeded);
        }
        const StoredType& stored = m_types[index];
        const bool into = target != nullptr && current != nullptr && Compatible(stored, *current);
        void* const destination = into ? target : nullptr;
        switch (stored.kind) {
            case TypeKind::Bool: {
                NX_TRY_ASSIGN(const u8 value, m_input.Get<u8>());
                if (value > 1) {
                    return Error(SerializationError::InvalidValue);
                }
                if (destination != nullptr) {
                    Memory::Copy(destination, &value, 1);
                }
                return {};
            }
            case TypeKind::Character:
            case TypeKind::SignedInteger:
            case TypeKind::UnsignedInteger:
            case TypeKind::FloatingPoint:
            case TypeKind::Enum:
            case TypeKind::Handle:
                return m_input.Copy(destination, stored.size);
            case TypeKind::String: {
                NX_TRY_ASSIGN(const u32 length, m_input.Get<u32>());
                NX_TRY_ASSIGN(const Span<const u8> bytes, m_input.Take(length));
                if (destination != nullptr) {
                    const Span<const char> characters = AsCharacters(bytes);
                    const Containers::StringView text(characters.Data(), characters.Size());
                    NX_TRY(current->stringOperations->assign(destination, text, m_allocator));
                }
                return {};
            }
            case TypeKind::FixedArray: {
                const TypeInfo* element = nullptr;
                if (into) {
                    NX_TRY_ASSIGN(const TypeInfo* const resolved, Resolve(current->element));
                    element = resolved;
                }
                for (u64 item = 0; item < stored.count; ++item) {
                    void* const slot = element != nullptr && item < current->count
                                           ? static_cast<void*>(Advance(destination, item * element->size))
                                           : nullptr;
                    NX_TRY(ReadValue(stored.element, element, slot, depth + 1));
                }
                return {};
            }
            case TypeKind::Array: {
                NX_TRY_ASSIGN(const u32 count, m_input.Get<u32>());
                NX_TRY(CheckCount(count, stored.element));
                const TypeInfo* element = nullptr;
                const bool resizable = into && current->arrayOperations->resize != nullptr;
                if (resizable) {
                    NX_TRY_ASSIGN(const TypeInfo* const resolved, Resolve(current->element));
                    element = resolved;
                    NX_TRY(current->arrayOperations->resize(destination, count, m_allocator));
                }
                for (u32 item = 0; item < count; ++item) {
                    void* const slot = resizable ? current->arrayOperations->at(destination, item) : nullptr;
                    NX_TRY(ReadValue(stored.element, element, slot, depth + 1));
                }
                return {};
            }
            case TypeKind::HashMap:
                return ReadMap(stored, current, destination, depth);
            case TypeKind::Record: {
                if (destination != nullptr) {
                    MapFields(index, *current);
                }
                const StoredType& record = m_types[index];
                for (u32 field = 0; field < record.fieldCount; ++field) {
                    const StoredField& storedField = m_fields[record.firstField + field];
                    const TypeInfo* fieldType = nullptr;
                    void* slot = nullptr;
                    if (destination != nullptr && storedField.current != kNoField) {
                        const FieldInfo& info = current->fields[storedField.current];
                        NX_TRY_ASSIGN(const TypeInfo* const resolved, Resolve(info.type));
                        fieldType = resolved;
                        slot = Advance(destination, info.offset);
                    }
                    NX_TRY(ReadValue(storedField.type, fieldType, slot, depth + 1));
                }
                return {};
            }
            case TypeKind::Opaque:
                break;
        }
        return Error(SerializationError::InvalidFormat);
    }

    Result<void, Error> ReadMap(const StoredType& stored, const TypeInfo* current, void* destination, u32 depth) {
        NX_TRY_ASSIGN(const u32 count, m_input.Get<u32>());
        NX_TRY(CheckCount(count, stored.element));
        const TypeInfo* key = nullptr;
        const TypeInfo* value = nullptr;
        if (destination != nullptr) {
            NX_TRY_ASSIGN(const TypeInfo* const resolvedKey, Resolve(current->element));
            NX_TRY_ASSIGN(const TypeInfo* const resolvedValue, Resolve(current->value));
            key = resolvedKey;
            value = resolvedValue;
        }
        const bool usable = key != nullptr && value != nullptr && key->lifecycle.construct != nullptr &&
                            value->lifecycle.construct != nullptr && Compatible(m_types[stored.element], *key) &&
                            Compatible(m_types[stored.value], *value);
        for (u32 entry = 0; entry < count; ++entry) {
            if (!usable) {
                NX_TRY(ReadValue(stored.element, nullptr, nullptr, depth + 1));
                NX_TRY(ReadValue(stored.value, nullptr, nullptr, depth + 1));
                continue;
            }
            Temporary storedKey;
            Temporary storedValue;
            NX_TRY(storedKey.Create(m_memory, *key));
            NX_TRY(storedValue.Create(m_memory, *value));
            NX_TRY(ReadValue(stored.element, key, storedKey.storage, depth + 1));
            NX_TRY(ReadValue(stored.value, value, storedValue.storage, depth + 1));
            NX_TRY(current->mapOperations->set(destination, storedKey.storage, storedValue.storage, m_allocator));
        }
        return {};
    }

    const TypeRegistry& m_registry;
    ByteReader m_input;
    Memory::IAllocator& m_allocator;
    Memory::TrackingAllocator m_memory;
    Containers::Array<StoredType> m_types;
    Containers::Array<StoredField> m_fields;
};

} // namespace

Result<void, Error> WriteBinary(const TypeRegistry& registry, const TypeInfo& type, const void* object,
                                Containers::Array<u8>& out) {
    NX_ASSERT(out.GetAllocator() != nullptr, "WriteBinary needs an output array with an allocator.");
    Writer writer(registry, out);
    return writer.Run(type, object);
}

Result<void, Error> ReadBinary(const TypeRegistry& registry, Span<const u8> data, const TypeInfo& type, void* object,
                               Memory::IAllocator& allocator) {
    Reader reader(registry, data, allocator);
    return reader.Run(type, object);
}

} // namespace Nexyl::Serialization
