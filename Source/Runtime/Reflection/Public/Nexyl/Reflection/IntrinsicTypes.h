#pragma once

#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Reflection/TypeInfo.h>

// The built-in kinds of reflection: fundamental types, fixed arrays and the vocabulary containers. Their TypeInfo
// comes from the templates below, written once for every type, so that no type needs registration code
// (docs/NEXYL_ARCHITECTURE.md, section 8). The header tool lists the instantiations a module's fields use.

namespace Nexyl::Reflection {

namespace Detail {

/// The lifecycle functions a type supports. Internal. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeLifecycle LifecycleOf() {
    TypeLifecycle lifecycle;
    if constexpr (DefaultConstructible<T>) {
        lifecycle.construct = [](void* object) { ConstructAt<T>(object); };
    }
    if constexpr (Destructible<T>) {
        lifecycle.destruct = [](void* object) { DestroyAt(static_cast<T*>(object)); };
    }
    if constexpr (CopyConstructible<T>) {
        lifecycle.copy = [](void* destination, const void* source) {
            ConstructAt<T>(destination, *static_cast<const T*>(source));
        };
    }
    if constexpr (MoveConstructible<T>) {
        lifecycle.move = [](void* destination, void* source) {
            ConstructAt<T>(destination, ::Nexyl::Move(*static_cast<T*>(source)));
        };
    }
    return lifecycle;
}

/// A description with the name, identity, size, alignment and kind of `T` filled in. Internal. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeInfo BasicInfo(const char* name, TypeKind kind) {
    TypeInfo info;
    info.name = name;
    info.id = TypeIdOf<T>();
    info.size = sizeof(T);
    info.alignment = alignof(T);
    info.kind = kind;
    return info;
}

/// The canonical name of a fundamental type. Internal. @since 0.1
template <typename T>
[[nodiscard]] consteval const char* FundamentalName() {
    if constexpr (SameAs<T, bool>) {
        return "bool";
    } else if constexpr (SameAs<T, char>) {
        return "char";
    } else if constexpr (SameAs<T, f32>) {
        return "f32";
    } else if constexpr (SameAs<T, f64>) {
        return "f64";
    } else if constexpr (SignedIntegral<T>) {
        return sizeof(T) == 1 ? "i8" : sizeof(T) == 2 ? "i16" : sizeof(T) == 4 ? "i32" : "i64";
    } else {
        return sizeof(T) == 1 ? "u8" : sizeof(T) == 2 ? "u16" : sizeof(T) == 4 ? "u32" : "u64";
    }
}

/// The kind of a fundamental type. Internal. @since 0.1
template <typename T>
[[nodiscard]] consteval TypeKind FundamentalKind() {
    if constexpr (SameAs<T, bool>) {
        return TypeKind::Bool;
    } else if constexpr (SameAs<T, char>) {
        return TypeKind::Character;
    } else if constexpr (SameAs<T, f32> || SameAs<T, f64>) {
        return TypeKind::FloatingPoint;
    } else if constexpr (SignedIntegral<T>) {
        return TypeKind::SignedInteger;
    } else {
        return TypeKind::UnsignedInteger;
    }
}

/// Type-erased operations of Containers::Array<T>. Internal. @since 0.1
template <typename T>
struct ArrayAccess {
    /// See ArrayOperations::size. @since 0.1
    static usize Size(const void* array) {
        return static_cast<const Containers::Array<T>*>(array)->Size();
    }

    /// See ArrayOperations::at. @since 0.1
    static void* At(void* array, usize index) {
        return &(*static_cast<Containers::Array<T>*>(array))[index];
    }

    /// See ArrayOperations::atConst. @since 0.1
    static const void* AtConst(const void* array, usize index) {
        return &(*static_cast<const Containers::Array<T>*>(array))[index];
    }

    /// See ArrayOperations::resize. @since 0.1
    static Result<void, Error> Resize(void* array, usize size, Memory::IAllocator& fallback) {
        auto& typed = *static_cast<Containers::Array<T>*>(array);
        if (typed.GetAllocator() == nullptr) {
            typed = Containers::Array<T>(fallback);
        }
        NX_TRY(typed.TryReserve(size));
        typed.Resize(size);
        return {};
    }

    /// Resize, or null when elements cannot be default-constructed. @since 0.1
    static consteval auto ResizeFunction() -> Result<void, Error> (*)(void*, usize, Memory::IAllocator&) {
        if constexpr (DefaultConstructible<T>) {
            return &Resize;
        } else {
            return nullptr;
        }
    }
};

/// Type-erased operations of Containers::HashMap<K, V>. Internal. @since 0.1
template <typename TKey, typename TValue>
struct MapAccess {
    using Map = Containers::HashMap<TKey, TValue>;

    /// See MapOperations::size. @since 0.1
    static usize Size(const void* map) {
        return static_cast<const Map*>(map)->Size();
    }

    /// See MapOperations::visit. @since 0.1
    static void Visit(const void* map, void (*visitor)(void* context, const void* key, const void* value),
                      void* context) {
        for (const auto entry : *static_cast<const Map*>(map)) {
            visitor(context, &entry.key, &entry.value);
        }
    }

    /// See MapOperations::set. @since 0.1
    static Result<void, Error> Set(void* map, void* key, void* value, Memory::IAllocator& fallback) {
        auto& typed = *static_cast<Map*>(map);
        if (typed.GetAllocator() == nullptr) {
            typed = Map(fallback);
        }
        NX_TRY(typed.TryReserve(typed.Size() + 1));
        typed.Set(::Nexyl::Move(*static_cast<TKey*>(key)), ::Nexyl::Move(*static_cast<TValue*>(value)));
        return {};
    }
};

/// Type-erased operations of Containers::String. Internal. @since 0.1
struct StringAccess {
    /// See StringOperations::view. @since 0.1
    static Containers::StringView View(const void* string) {
        return static_cast<const Containers::String*>(string)->View();
    }

    /// See StringOperations::assign. @since 0.1
    static Result<void, Error> Assign(void* string, Containers::StringView text, Memory::IAllocator& fallback) {
        auto& typed = *static_cast<Containers::String*>(string);
        if (typed.GetAllocator() == nullptr) {
            typed = Containers::String(fallback);
        }
        NX_TRY(typed.TryReserve(text.Size()));
        typed.Assign(text);
        return {};
    }
};

/// Built-in reflection of a type; empty for types the header tool describes. Internal. @since 0.1
template <typename T>
struct Intrinsic {
    /// False: not a built-in kind.
    static constexpr bool kIsIntrinsic = false;
};

/// Fundamental types. Internal. @since 0.1
template <typename T>
    requires(SameAs<T, bool> || SameAs<T, char> || Integral<T> || SameAs<T, f32> || SameAs<T, f64>)
struct Intrinsic<T> {
    /// True: a built-in kind.
    static constexpr bool kIsIntrinsic = true;
    /// The type's description.
    static constexpr TypeInfo kInfo = [] {
        TypeInfo info = BasicInfo<T>(FundamentalName<T>(), FundamentalKind<T>());
        info.lifecycle = LifecycleOf<T>();
        return info;
    }();
};

/// Built-in arrays. Internal. @since 0.1
template <typename T, usize N>
struct Intrinsic<T[N]> {
    /// True: a built-in kind.
    static constexpr bool kIsIntrinsic = true;
    /// The type's description. Arrays are created and destroyed as part of their record.
    static constexpr TypeInfo kInfo = [] {
        TypeInfo info = BasicInfo<T[N]>("[]", TypeKind::FixedArray);
        info.element = TypeIdOf<T>();
        info.count = N;
        return info;
    }();
};

/// Containers::String. Internal. @since 0.1
template <>
struct Intrinsic<Containers::String> {
    /// True: a built-in kind.
    static constexpr bool kIsIntrinsic = true;
    /// The text operations.
    static constexpr StringOperations kOperations{&StringAccess::View, &StringAccess::Assign};
    /// The type's description.
    static constexpr TypeInfo kInfo = [] {
        TypeInfo info = BasicInfo<Containers::String>("Nexyl::Containers::String", TypeKind::String);
        info.lifecycle = LifecycleOf<Containers::String>();
        info.stringOperations = &kOperations;
        return info;
    }();
};

/// Containers::Array<T>. Internal. @since 0.1
template <typename T>
struct Intrinsic<Containers::Array<T>> {
    /// True: a built-in kind.
    static constexpr bool kIsIntrinsic = true;
    /// The element operations; resizing needs a default-constructible element.
    static constexpr ArrayOperations kOperations{&ArrayAccess<T>::Size, &ArrayAccess<T>::At, &ArrayAccess<T>::AtConst,
                                                 ArrayAccess<T>::ResizeFunction()};
    /// The type's description.
    static constexpr TypeInfo kInfo = [] {
        TypeInfo info = BasicInfo<Containers::Array<T>>("Nexyl::Containers::Array", TypeKind::Array);
        info.element = TypeIdOf<T>();
        info.lifecycle = LifecycleOf<Containers::Array<T>>();
        info.arrayOperations = &kOperations;
        return info;
    }();
};

/// Containers::HashMap<K, V>. Internal. @since 0.1
template <typename TKey, typename TValue>
struct Intrinsic<Containers::HashMap<TKey, TValue>> {
    /// True: a built-in kind.
    static constexpr bool kIsIntrinsic = true;
    /// The entry operations.
    static constexpr MapOperations kOperations{&MapAccess<TKey, TValue>::Size, &MapAccess<TKey, TValue>::Visit,
                                               &MapAccess<TKey, TValue>::Set};
    /// The type's description.
    static constexpr TypeInfo kInfo = [] {
        using Map = Containers::HashMap<TKey, TValue>;
        TypeInfo info = BasicInfo<Map>("Nexyl::Containers::HashMap", TypeKind::HashMap);
        info.element = TypeIdOf<TKey>();
        info.value = TypeIdOf<TValue>();
        info.lifecycle = LifecycleOf<Map>();
        info.mapOperations = &kOperations;
        return info;
    }();
};

/// Containers::Handle<T>. Internal. @since 0.1
template <typename T>
struct Intrinsic<Containers::Handle<T>> {
    /// True: a built-in kind.
    static constexpr bool kIsIntrinsic = true;
    /// The type's description.
    static constexpr TypeInfo kInfo = [] {
        TypeInfo info = BasicInfo<Containers::Handle<T>>("Nexyl::Containers::Handle", TypeKind::Handle);
        info.element = TypeIdOf<T>();
        info.lifecycle = LifecycleOf<Containers::Handle<T>>();
        return info;
    }();
};

} // namespace Detail

/// Satisfied when reflection describes `T` itself, without the header tool: fundamental types, built-in arrays,
/// Containers::String, Array, HashMap and Handle.
/// @since 0.1
template <typename T>
concept IntrinsicType = Detail::Intrinsic<RemoveCv<T>>::kIsIntrinsic;

/// Returns the built-in description of a type.
///
/// In a dynamic build every binary has its own copy of the description; compare descriptions by identity, not
/// by address.
///
/// Instantiations of the container templates are known by the template's name, for example
/// `Nexyl::Containers::Array`; their identity tells them apart.
/// @return The constant description.
/// @threadsafety safe
/// @since 0.1
template <IntrinsicType T>
[[nodiscard]] constexpr const TypeInfo& IntrinsicTypeInfo() {
    return Detail::Intrinsic<RemoveCv<T>>::kInfo;
}

} // namespace Nexyl::Reflection
