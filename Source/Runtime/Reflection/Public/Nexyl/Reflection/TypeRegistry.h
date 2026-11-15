#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Reflection/TypeInfo.h>

namespace Nexyl::Reflection {

/// The memory tag of everything the Reflection module allocates.
/// @since 0.1
inline constexpr Memory::MemoryTag kReflectionMemoryTag = Memory::MemoryTag::Make(ModuleId::Reflection);

/// Maps type identities and names to their descriptions (docs/NEXYL_ARCHITECTURE.md, section 8.8).
///
/// Modules attach their generated TypeTable when they load and detach it before they unload. Attaching costs one
/// map entry per type; no description is copied. The same built-in instantiation, for example
/// `Array<Vec3>`, may come from several tables; the registry keeps every provider and answers with one that is
/// still attached. There is no global registry: the application creates one and passes it to what needs it.
///
/// ```cpp
/// NX_TRY_ASSIGN(UniquePtr<TypeRegistry> types, TypeRegistry::Create(reflectionMemory));
/// NX_TRY(types->Attach(GetFoundationTypeTables()));
/// const TypeInfo* vec3 = types->Find<Math::Vec3>();
/// ```
/// @threadsafety compatible
/// @since 0.1
class TypeRegistry final {
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    /// Creates an empty registry.
    /// @param allocator Supplies the maps. It must outlive the registry.
    /// @return The registry.
    /// @error OutOfMemory The registry cannot be allocated.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_REFLECTION_API static Result<Memory::UniquePtr<TypeRegistry>, Error>
    Create(Memory::IAllocator& allocator);

    /// Constructs an empty registry. Only Create can call it.
    /// @param key       Restricts construction to Create.
    /// @param allocator Supplies the maps.
    /// @since 0.1
    NX_REFLECTION_API TypeRegistry(CreateKey key, Memory::IAllocator& allocator);

    /// Destroys the registry. Tables still attached are dropped. @since 0.1
    NX_REFLECTION_API ~TypeRegistry();

    TypeRegistry(const TypeRegistry&) = delete;
    TypeRegistry& operator=(const TypeRegistry&) = delete;
    TypeRegistry(TypeRegistry&&) = delete;
    TypeRegistry& operator=(TypeRegistry&&) = delete;

    /// Adds every type of a table. Nothing is added if any type conflicts.
    /// @param table The table. It and its descriptions must stay valid until it is detached.
    /// @return Success, or the first conflict.
    /// @error TypeIdCollision A type's identity equals that of a type with another name.
    /// @error TypeMismatch    A type already known by that name differs in size, alignment or kind.
    /// @error OutOfMemory     The maps cannot grow.
    /// @pre The table is not attached.
    /// @threadsafety main
    /// @since 0.1
    [[nodiscard]] NX_REFLECTION_API Result<void, Error> Attach(const TypeTable& table);

    /// Adds every type of several tables, in order; stops at the first table that fails.
    /// @param tables The tables. They must stay valid until they are detached.
    /// @return Success, or the first error.
    /// @error Any See Attach.
    /// @threadsafety main
    /// @since 0.1
    [[nodiscard]] NX_REFLECTION_API Result<void, Error> Attach(Span<const TypeTable* const> tables);

    /// Removes every type of a table.
    /// @param table A table that was attached.
    /// @threadsafety main
    /// @since 0.1
    NX_REFLECTION_API void Detach(const TypeTable& table);

    /// Finds a type by identity.
    /// @param id The identity.
    /// @return The description, or nullptr if no attached table has it.
    /// @threadsafety compatible
    /// @since 0.1
    [[nodiscard]] NX_REFLECTION_API const TypeInfo* Find(TypeId id) const;

    /// Finds a type by its canonical name. Container instantiations are found by identity only.
    /// @param name The name, for example `Nexyl::Math::Vec3`.
    /// @return The description, or nullptr.
    /// @threadsafety compatible
    /// @since 0.1
    [[nodiscard]] NX_REFLECTION_API const TypeInfo* FindByName(Containers::StringView name) const;

    /// Finds the description of `T`.
    /// @return The description, or nullptr if no attached table has it.
    /// @threadsafety compatible
    /// @since 0.1
    template <typename T>
    [[nodiscard]] const TypeInfo* Find() const {
        return Find(TypeIdOf<T>());
    }

    /// Returns the number of distinct types.
    /// @return The number of identities with at least one attached description.
    /// @threadsafety compatible
    /// @since 0.1
    [[nodiscard]] NX_REFLECTION_API usize GetTypeCount() const;

private:
    struct Entry {
        // Every attached description of this identity; the first answers lookups.
        Containers::Array<const TypeInfo*> providers;
    };

    [[nodiscard]] static bool IsNamedByIdentityOnly(const TypeInfo& type);
    [[nodiscard]] Result<void, Error> Check(const TypeInfo& type) const;

    Memory::TrackingAllocator m_memory;
    Containers::HashMap<TypeId, Entry> m_types;
    Containers::HashMap<Containers::StringView, TypeId> m_names;
};

/// Returns the table of the built-in types: `bool`, `char`, the integers, `f32`, `f64` and Containers::String.
/// @return The table; it lives for the life of the program.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_REFLECTION_API const TypeTable& GetBuiltinTypeTable();

} // namespace Nexyl::Reflection
