#pragma once

#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/TypeName.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Hash.h>

namespace Nexyl::Reflection {

/// The identity of a type: a 64-bit hash of its canonical name, the same in every build, on every platform and
/// with every compiler (docs/NEXYL_ARCHITECTURE.md, section 8.8).
///
/// The canonical form, which the header tool (NHT) computes the same way:
///
/// | Type | Identity |
/// |---|---|
/// | `bool`, `char`, `f32`, `f64` | FNV-1a 64 of that name |
/// | Other integers | FNV-1a 64 of `i8` … `i64` or `u8` … `u64`, by size and signedness |
/// | Class or enumeration | FNV-1a 64 of the fully qualified name, for example `Nexyl::Math::Vec3` |
/// | Instantiation `T<A, B>` of a template with type parameters | the hash of `T`'s qualified name, continued over the
/// 8 little-endian bytes of each argument's identity | | Array `T[N]` | the hash of `[]`, continued over the identity
/// of `T`, then over `N` as 8 bytes |
///
/// Collisions are detected when tables are attached to a TypeRegistry.
/// @threadsafety safe
/// @since 0.1
struct TypeId {
    /// The hash; zero means no type.
    u64 value = 0;

    /// Tells whether this names no type.
    /// @return True for the default identity.
    /// @since 0.1
    [[nodiscard]] constexpr bool IsNull() const {
        return value == 0;
    }

    /// Compares two identities.
    /// @return True if they are equal.
    /// @since 0.1
    constexpr bool operator==(const TypeId&) const = default;
};

/// Hashes a type identity for HashMap and HashSet.
/// @param id The identity.
/// @return The hash.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] inline u64 HashValue(TypeId id) {
    return Containers::HashInteger(id.value);
}

namespace Detail {

/// FNV-1a 64 offset basis. Internal. @since 0.1
inline constexpr u64 kTypeHashBasis = 0xCBF2'9CE4'8422'2325ULL;
/// FNV-1a 64 prime. Internal. @since 0.1
inline constexpr u64 kTypeHashPrime = 0x100'0000'01B3ULL;

/// Continues an FNV-1a 64 hash over text. Internal. @since 0.1
[[nodiscard]] constexpr u64 ContinueTypeHash(u64 hash, const char* text, usize size) {
    for (usize index = 0; index < size; ++index) {
        hash = (hash ^ static_cast<u8>(text[index])) * kTypeHashPrime;
    }
    return hash;
}

/// Continues an FNV-1a 64 hash over the 8 little-endian bytes of a value. Internal. @since 0.1
[[nodiscard]] constexpr u64 ContinueTypeHash(u64 hash, u64 value) {
    for (u32 byte = 0; byte < 8; ++byte) {
        hash = (hash ^ ((value >> (byte * 8U)) & 0xFFU)) * kTypeHashPrime;
    }
    return hash;
}

/// Hashes a NUL-terminated name. Internal. @since 0.1
[[nodiscard]] constexpr u64 HashTypeName(const char* name) {
    usize size = 0;
    while (name[size] != '\0') {
        ++size;
    }
    return ContinueTypeHash(kTypeHashBasis, name, size);
}

/// Recognises instantiations of templates with type parameters only. Internal. @since 0.1
template <typename T>
struct TemplateInstance {
    /// False: not such an instantiation.
    static constexpr bool kIsInstance = false;
};

/// Recognises instantiations of templates with type parameters only. Internal. @since 0.1
template <template <typename...> class TTemplate, typename... TArguments>
struct TemplateInstance<TTemplate<TArguments...>> {
    /// True: an instantiation.
    static constexpr bool kIsInstance = true;

    /// Continues a hash over the identity of every argument.
    /// @since 0.1
    template <typename TIdOf>
    static consteval u64 ContinueOverArguments(u64 hash) {
        ((hash = ContinueTypeHash(hash, TIdOf::template Of<TArguments>().value)), ...);
        return hash;
    }
};

/// The identity of a type; see TypeId. Internal; use TypeIdOf. @since 0.1
struct TypeIdComputer {
    /// Computes the identity of `T`.
    /// @since 0.1
    template <typename T>
    static consteval TypeId Of() {
        using U = RemoveCv<T>;
        if constexpr (SameAs<U, bool>) {
            return TypeId{HashTypeName("bool")};
        } else if constexpr (SameAs<U, char>) {
            return TypeId{HashTypeName("char")};
        } else if constexpr (Integral<U>) {
            constexpr bool kSigned = SignedIntegral<U>;
            if constexpr (sizeof(U) == 1) {
                return TypeId{HashTypeName(kSigned ? "i8" : "u8")};
            } else if constexpr (sizeof(U) == 2) {
                return TypeId{HashTypeName(kSigned ? "i16" : "u16")};
            } else if constexpr (sizeof(U) == 4) {
                return TypeId{HashTypeName(kSigned ? "i32" : "u32")};
            } else {
                static_assert(sizeof(U) == 8, "Integers wider than 64 bits have no TypeId.");
                return TypeId{HashTypeName(kSigned ? "i64" : "u64")};
            }
        } else if constexpr (SameAs<U, f32>) {
            return TypeId{HashTypeName("f32")};
        } else if constexpr (SameAs<U, f64>) {
            return TypeId{HashTypeName("f64")};
        } else if constexpr (ArrayType<U>) {
            using TElement = RemoveExtent<U>;
            const u64 hash = ContinueTypeHash(HashTypeName("[]"), Of<TElement>().value);
            return TypeId{ContinueTypeHash(hash, u64{sizeof(U) / sizeof(TElement)})};
        } else if constexpr (TemplateInstance<U>::kIsInstance) {
            // The template's own name is the part of the type name before its argument list; the arguments are
            // spelled differently by different compilers, so their identities are hashed instead.
            constexpr TypeName kName = TypeNameOf<U>();
            usize length = 0;
            while (length < kName.size && kName.data[length] != '<') {
                ++length;
            }
            const u64 hash = ContinueTypeHash(kTypeHashBasis, kName.data, length);
            return TypeId{TemplateInstance<U>::template ContinueOverArguments<TypeIdComputer>(hash)};
        } else {
            static_assert(ClassType<U> || Enumeration<U>,
                          "Only fundamental, array, class and enum types have a TypeId.");
            constexpr TypeName kName = TypeNameOf<U>();
            return TypeId{ContinueTypeHash(kTypeHashBasis, kName.data, kName.size)};
        }
    }
};

} // namespace Detail

/// Returns the identity of a type, computed at compile time. See TypeId for the canonical form.
/// @return The identity.
/// @threadsafety safe
/// @since 0.1
template <typename T>
consteval TypeId TypeIdOf() {
    return Detail::TypeIdComputer::Of<T>();
}

} // namespace Nexyl::Reflection
