#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Reflection/TypeInfo.h>
#include <Nexyl/Reflection/TypeRegistry.h>
#include <Nexyl/Serialization/SerializationErrors.h>

namespace Nexyl::Serialization {

/// The memory tag of everything the Serialization module allocates for its own use.
/// @since 0.1
inline constexpr Memory::MemoryTag kSerializationMemoryTag = Memory::MemoryTag::Make(ModuleId::Serialization);

/// The version of the binary format that WriteBinary writes. ReadBinary reads it and every earlier one.
/// @since 0.1
inline constexpr u16 kBinaryFormatVersion = 1;

/// Writes an object as a binary archive (docs/NEXYL_ARCHITECTURE.md, section 8.10).
///
/// The archive holds a schema of every type it uses, with their field names and a schema hash, then the value,
/// little-endian. Transient fields are left out. Because of the schema, a later version of the types can read
/// it: fields are matched by name or `@renamed-from`, fields that no longer exist are skipped, and new fields
/// keep the value they had.
/// @param registry Describes the object's type and every type it contains.
/// @param type     The object's type.
/// @param object   The object. Borrowed for the duration of the call.
/// @param out      Receives the archive after what it holds. It must have an allocator, which also serves the
///                 writer's own bookkeeping.
/// @return Success, or the error.
/// @error UnknownType     A type the object contains is not in the registry.
/// @error NotSerializable A type is opaque, or a record with a base class.
/// @error LimitExceeded   The object nests deeper than 64 levels, or a container holds more than 2^32 - 1 items.
/// @error OutOfMemory     `out` cannot grow.
/// @pre `out` has an allocator.
/// @threadsafety compatible
/// @since 0.1
[[nodiscard]] NX_SERIALIZATION_API Result<void, Error> WriteBinary(const Reflection::TypeRegistry& registry,
                                                                   const Reflection::TypeInfo& type, const void* object,
                                                                   Containers::Array<u8>& out);

/// Reads a binary archive into an existing object, which keeps its value in fields the archive does not have.
///
/// Every input is validated: a malformed or hostile archive gives an error, never a crash (rule ERR-7). After an
/// error the object holds a mix of read and earlier values; read into a temporary to keep it intact.
/// @param registry  Describes the object's type and every type it contains.
/// @param data      The archive. Borrowed for the duration of the call.
/// @param type      The object's type: the archive's root type, or the type renamed from it.
/// @param object    Receives the values. Containers in it that have no allocator take `allocator`.
/// @param allocator Supplies new container storage and the reader's own bookkeeping.
/// @return Success, or the error.
/// @error InvalidFormat      The data is not a binary archive, or its schema or value is inconsistent.
/// @error UnsupportedVersion The archive's format version is newer than kBinaryFormatVersion.
/// @error Truncated          The data ends early.
/// @error LimitExceeded      A count or the nesting depth exceeds what the data can hold.
/// @error InvalidValue       A bool is neither 0 nor 1.
/// @error TypeMismatch       The archive holds another root type.
/// @error UnknownType        A field's type is not in the registry.
/// @error OutOfMemory        A container cannot grow.
/// @threadsafety compatible
/// @since 0.1
[[nodiscard]] NX_SERIALIZATION_API Result<void, Error> ReadBinary(const Reflection::TypeRegistry& registry,
                                                                  Span<const u8> data, const Reflection::TypeInfo& type,
                                                                  void* object, Memory::IAllocator& allocator);

/// Writes an object as a binary archive. See the type-erased overload.
/// @param registry Describes the object's type and every type it contains.
/// @param object   The object. Borrowed for the duration of the call.
/// @param out      Receives the archive. It must have an allocator.
/// @return Success, or the error.
/// @error UnknownType `T` is not in the registry; see the type-erased overload for the others.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
[[nodiscard]] Result<void, Error> WriteBinary(const Reflection::TypeRegistry& registry, const T& object,
                                              Containers::Array<u8>& out) {
    const Reflection::TypeInfo* const type = registry.Find<T>();
    if (type == nullptr) {
        return Error(SerializationError::UnknownType);
    }
    return WriteBinary(registry, *type, &object, out);
}

/// Reads a binary archive into an existing object. See the type-erased overload.
/// @param registry  Describes the object's type and every type it contains.
/// @param data      The archive. Borrowed for the duration of the call.
/// @param object    Receives the values.
/// @param allocator Supplies new container storage and the reader's own bookkeeping.
/// @return Success, or the error.
/// @error UnknownType `T` is not in the registry; see the type-erased overload for the others.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
[[nodiscard]] Result<void, Error> ReadBinary(const Reflection::TypeRegistry& registry, Span<const u8> data, T& object,
                                             Memory::IAllocator& allocator) {
    const Reflection::TypeInfo* const type = registry.Find<T>();
    if (type == nullptr) {
        return Error(SerializationError::UnknownType);
    }
    return ReadBinary(registry, data, *type, &object, allocator);
}

} // namespace Nexyl::Serialization
