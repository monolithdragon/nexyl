#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Reflection/TypeInfo.h>
#include <Nexyl/Reflection/TypeRegistry.h>
#include <Nexyl/Serialization/SerializationErrors.h>

namespace Nexyl::Serialization {

/// The version of the text format that WriteText writes. ReadText reads it and every earlier one.
/// @since 0.1
inline constexpr u32 kTextFormatVersion = 1;

/// Writes an object as a text archive: JSON (RFC 8259) with a fixed layout, one field per line in declaration
/// order, so that diffs stay small (docs/NEXYL_ARCHITECTURE.md, section 8.10).
///
/// The document is an object with the keys `nexyl` (the format version), `type` (the root type's name),
/// `versions` (the schema version of every record and enumeration it uses) and `value`. Records are objects,
/// enumerations their value's name, integers exact, floating-point numbers the shortest text that reads back to
/// the same bits (`"nan"`, `"inf"` and `"-inf"` as strings), arrays arrays, a HashMap an array of
/// `[key, value]` pairs and a Handle `[index, generation]`. Transient fields are left out.
/// @param registry Describes the object's type and every type it contains.
/// @param type     The object's type.
/// @param object   The object. Borrowed for the duration of the call.
/// @param out      Receives the text after what it holds. It must have an allocator.
/// @return Success, or the error.
/// @error UnknownType     A type the object contains is not in the registry.
/// @error NotSerializable A type is opaque, or a record with a base class.
/// @error LimitExceeded   The object nests deeper than 64 levels.
/// @error OutOfMemory     `out` cannot grow.
/// @pre `out` has an allocator.
/// @threadsafety compatible
/// @since 0.1
[[nodiscard]] NX_SERIALIZATION_API Result<void, Error> WriteText(const Reflection::TypeRegistry& registry,
                                                                 const Reflection::TypeInfo& type, const void* object,
                                                                 Containers::String& out);

/// Reads a text archive into an existing object, which keeps its value in fields the text does not set.
///
/// Fields are matched by name or `@renamed-from`; unknown keys are skipped. A value that does not fit its field,
/// such as text for a number or a number out of range, leaves the field unchanged, so a changed type reads the
/// older text it can. Malformed JSON is an error (rule ERR-7).
/// @param registry  Describes the object's type and every type it contains.
/// @param text      The archive, UTF-8. Borrowed for the duration of the call.
/// @param type      The object's type: the archive's root type, or the type renamed from it.
/// @param object    Receives the values. Containers in it that have no allocator take `allocator`.
/// @param allocator Supplies new container storage and the reader's own bookkeeping.
/// @return Success, or the error.
/// @error InvalidFormat      The text is not well-formed JSON, or lacks the keys `nexyl`, `type` and `value`.
/// @error UnsupportedVersion The archive's format version is newer than kTextFormatVersion.
/// @error LimitExceeded      The text nests deeper than 64 levels.
/// @error TypeMismatch       The archive holds another root type.
/// @error UnknownType        A field's type is not in the registry.
/// @error OutOfMemory        A container cannot grow.
/// @threadsafety compatible
/// @since 0.1
[[nodiscard]] NX_SERIALIZATION_API Result<void, Error> ReadText(const Reflection::TypeRegistry& registry,
                                                                Containers::StringView text,
                                                                const Reflection::TypeInfo& type, void* object,
                                                                Memory::IAllocator& allocator);

/// Writes an object as a text archive. See the type-erased overload.
/// @param registry Describes the object's type and every type it contains.
/// @param object   The object. Borrowed for the duration of the call.
/// @param out      Receives the text. It must have an allocator.
/// @return Success, or the error.
/// @error UnknownType `T` is not in the registry; see the type-erased overload for the others.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
[[nodiscard]] Result<void, Error> WriteText(const Reflection::TypeRegistry& registry, const T& object,
                                            Containers::String& out) {
    const Reflection::TypeInfo* const type = registry.Find<T>();
    if (type == nullptr) {
        return Error(SerializationError::UnknownType);
    }
    return WriteText(registry, *type, &object, out);
}

/// Reads a text archive into an existing object. See the type-erased overload.
/// @param registry  Describes the object's type and every type it contains.
/// @param text      The archive. Borrowed for the duration of the call.
/// @param object    Receives the values.
/// @param allocator Supplies new container storage and the reader's own bookkeeping.
/// @return Success, or the error.
/// @error UnknownType `T` is not in the registry; see the type-erased overload for the others.
/// @threadsafety compatible
/// @since 0.1
template <typename T>
[[nodiscard]] Result<void, Error> ReadText(const Reflection::TypeRegistry& registry, Containers::StringView text,
                                           T& object, Memory::IAllocator& allocator) {
    const Reflection::TypeInfo* const type = registry.Find<T>();
    if (type == nullptr) {
        return Error(SerializationError::UnknownType);
    }
    return ReadText(registry, text, *type, &object, allocator);
}

} // namespace Nexyl::Serialization
