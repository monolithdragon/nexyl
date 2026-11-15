#pragma once

#include <Nexyl/Base/Types.h>
#include <Nexyl/Reflection/TypeInfo.h>

namespace Nexyl::Serialization::Detail {

// The FNV-1a 64 hash of what an archive needs to know about a type: kind, size, version, and the names and types
// of its serialized fields or its element types. Equal hashes mean an archive's values map onto the type as they
// are; a different hash selects the tolerant path (docs/NEXYL_ARCHITECTURE.md, section 8.10).
[[nodiscard]] u64 SchemaHash(const Reflection::TypeInfo& type);

// Whether a field is written to archives: everything but `@transient` fields.
[[nodiscard]] inline bool IsSerialized(const Reflection::FieldInfo& field) {
    return !field.flags.Has(Reflection::FieldFlag::Transient);
}

} // namespace Nexyl::Serialization::Detail
