#include "SchemaHash.h"

namespace Nexyl::Serialization::Detail {

namespace {

constexpr u64 kBasis = 0xCBF2'9CE4'8422'2325ULL;
constexpr u64 kPrime = 0x100'0000'01B3ULL;

u64 Mix(u64 hash, u64 value) {
    for (u32 byte = 0; byte < 8; ++byte) {
        hash = (hash ^ ((value >> (byte * 8U)) & 0xFFU)) * kPrime;
    }
    return hash;
}

u64 MixText(u64 hash, const char* text) {
    for (const char* character = text; *character != '\0'; ++character) {
        hash = (hash ^ static_cast<u8>(*character)) * kPrime;
    }
    return (hash ^ 0xFFU) * kPrime;
}

} // namespace

u64 SchemaHash(const Reflection::TypeInfo& type) {
    u64 hash = Mix(kBasis, static_cast<u64>(type.kind));
    hash = Mix(hash, type.size);
    hash = Mix(hash, type.version);
    switch (type.kind) {
        case Reflection::TypeKind::Record:
            for (const Reflection::FieldInfo& field : type.fields) {
                if (IsSerialized(field)) {
                    hash = Mix(MixText(hash, field.name), field.type.value);
                }
            }
            break;
        case Reflection::TypeKind::FixedArray:
            hash = Mix(Mix(hash, type.element.value), type.count);
            break;
        case Reflection::TypeKind::Array:
        case Reflection::TypeKind::Handle:
            hash = Mix(hash, type.element.value);
            break;
        case Reflection::TypeKind::HashMap:
            hash = Mix(Mix(hash, type.element.value), type.value.value);
            break;
        default:
            break;
    }
    return hash;
}

} // namespace Nexyl::Serialization::Detail
