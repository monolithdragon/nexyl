#pragma once

#include <Nexyl/Base/Flags.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Reflection/IntrinsicTypes.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Reflection/TypeInfo.h>

// Builders for the constant tables that the header tool generates (docs/NEXYL_ARCHITECTURE.md, section 8.7).
// Only generated code calls them; types are never registered by hand.

namespace Nexyl::Reflection::Detail {

/// Describes one field. Internal; called by generated tables. @since 0.1
[[nodiscard]] consteval FieldInfo MakeField(const char* name, u64 type, usize offset, Flags<FieldFlag> flags,
                                            const char* description, const char* units, const char* category,
                                            const char* renamedFrom, bool hasRange, f64 rangeMinimum, f64 rangeMaximum,
                                            f64 step) {
    FieldInfo field;
    field.name = name;
    field.type = TypeId{type};
    field.offset = offset;
    field.flags = flags;
    field.description = description;
    field.units = units;
    field.category = category;
    field.renamedFrom = renamedFrom;
    field.hasRange = hasRange;
    field.rangeMinimum = rangeMinimum;
    field.rangeMaximum = rangeMaximum;
    field.step = step;
    return field;
}

/// Describes one enumeration value. Internal; called by generated tables. @since 0.1
[[nodiscard]] consteval EnumValueInfo MakeEnumValue(const char* name, i64 value, const char* description,
                                                    const char* renamedFrom, bool hidden) {
    EnumValueInfo enumValue;
    enumValue.name = name;
    enumValue.value = value;
    enumValue.description = description;
    enumValue.renamedFrom = renamedFrom;
    enumValue.hidden = hidden;
    return enumValue;
}

/// The parts every generated description shares. Internal. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeInfo MakeType(const char* name, u64 id, TypeKind kind, Flags<TypeFlag> flags,
                                          const char* description, const char* category, const char* renamedFrom) {
    TypeInfo type;
    type.name = name;
    type.id = TypeId{id};
    type.size = sizeof(T);
    type.alignment = alignof(T);
    type.kind = kind;
    type.flags = flags;
    type.description = description;
    type.category = category;
    type.renamedFrom = renamedFrom;
    return type;
}

/// Describes a record. Internal; called by generated tables. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeInfo MakeRecordType(const char* name, u64 id, Flags<TypeFlag> flags,
                                                const char* description, const char* category, const char* renamedFrom,
                                                u64 base, Span<const FieldInfo> fields) {
    TypeInfo type = MakeType<T>(name, id, TypeKind::Record, flags, description, category, renamedFrom);
    type.base = TypeId{base};
    type.fields = fields;
    type.lifecycle = LifecycleOf<T>();
    return type;
}

/// Describes a class known by identity only. Internal; called by generated tables. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeInfo MakeOpaqueType(const char* name, u64 id, Flags<TypeFlag> flags,
                                                const char* description, const char* category,
                                                const char* renamedFrom) {
    return MakeType<T>(name, id, TypeKind::Opaque, flags, description, category, renamedFrom);
}

/// Describes an enumeration. Internal; called by generated tables. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeInfo MakeEnumType(const char* name, u64 id, Flags<TypeFlag> flags, const char* description,
                                              const char* category, const char* renamedFrom, u64 underlying,
                                              Span<const EnumValueInfo> values) {
    TypeInfo type = MakeType<T>(name, id, TypeKind::Enum, flags, description, category, renamedFrom);
    type.element = TypeId{underlying};
    type.enumValues = values;
    type.lifecycle = LifecycleOf<T>();
    return type;
}

} // namespace Nexyl::Reflection::Detail
