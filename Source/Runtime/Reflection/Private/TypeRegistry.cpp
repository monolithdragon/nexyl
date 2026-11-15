#include <Nexyl/Reflection/TypeRegistry.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Reflection/IntrinsicTypes.h>
#include <Nexyl/Reflection/ReflectionErrors.h>

namespace Nexyl::Reflection {

namespace {

constexpr const TypeInfo* kBuiltinTypes[] = {
    &IntrinsicTypeInfo<bool>(),
    &IntrinsicTypeInfo<char>(),
    &IntrinsicTypeInfo<i8>(),
    &IntrinsicTypeInfo<i16>(),
    &IntrinsicTypeInfo<i32>(),
    &IntrinsicTypeInfo<i64>(),
    &IntrinsicTypeInfo<u8>(),
    &IntrinsicTypeInfo<u16>(),
    &IntrinsicTypeInfo<u32>(),
    &IntrinsicTypeInfo<u64>(),
    &IntrinsicTypeInfo<f32>(),
    &IntrinsicTypeInfo<f64>(),
    &IntrinsicTypeInfo<Containers::String>(),
};

constexpr TypeTable kBuiltinTable{"Builtin", Span<const TypeInfo* const>(kBuiltinTypes)};

} // namespace

const TypeTable& GetBuiltinTypeTable() {
    return kBuiltinTable;
}

Result<Memory::UniquePtr<TypeRegistry>, Error> TypeRegistry::Create(Memory::IAllocator& allocator) {
    return Memory::MakeUnique<TypeRegistry>(allocator, CreateKey{}, allocator);
}

TypeRegistry::TypeRegistry(CreateKey /*key*/, Memory::IAllocator& allocator)
    : m_memory(allocator, kReflectionMemoryTag), m_types(m_memory), m_names(m_memory) {}

TypeRegistry::~TypeRegistry() = default;

bool TypeRegistry::IsNamedByIdentityOnly(const TypeInfo& type) {
    return type.kind == TypeKind::FixedArray || type.kind == TypeKind::Array || type.kind == TypeKind::HashMap ||
           type.kind == TypeKind::Handle;
}

Result<void, Error> TypeRegistry::Check(const TypeInfo& type) const {
    const Entry* const existing = m_types.Find(type.id);
    if (existing == nullptr) {
        return {};
    }
    const TypeInfo& known = *existing->providers[0];
    if (Containers::StringView::FromCString(known.name) != Containers::StringView::FromCString(type.name)) {
        return Error(ReflectionError::TypeIdCollision);
    }
    if (known.size != type.size || known.alignment != type.alignment || known.kind != type.kind) {
        return Error(ReflectionError::TypeMismatch);
    }
    return {};
}

Result<void, Error> TypeRegistry::Attach(const TypeTable& table) {
    // Validate everything first, so that a failed attach leaves the registry unchanged.
    for (const TypeInfo* const type : table.types) {
        NX_ASSERT(type != nullptr && !type->id.IsNull(), "A type table holds an invalid description.");
        NX_TRY(Check(*type));
    }
    NX_TRY(m_types.TryReserve(m_types.Size() + table.types.Size()));
    NX_TRY(m_names.TryReserve(m_names.Size() + table.types.Size()));
    for (const TypeInfo* const type : table.types) {
        Entry* existing = m_types.Find(type->id);
        if (existing == nullptr) {
            existing = &m_types.Set(type->id, Entry{Containers::Array<const TypeInfo*>(m_memory)});
            if (!IsNamedByIdentityOnly(*type)) {
                m_names.Set(Containers::StringView::FromCString(type->name), type->id);
            }
        }
        existing->providers.Push(type);
    }
    return {};
}

Result<void, Error> TypeRegistry::Attach(Span<const TypeTable* const> tables) {
    for (const TypeTable* const table : tables) {
        NX_TRY(Attach(*table));
    }
    return {};
}

void TypeRegistry::Detach(const TypeTable& table) {
    for (const TypeInfo* const type : table.types) {
        Entry* const entry = m_types.Find(type->id);
        NX_ASSERT(entry != nullptr, "Detaching a type table that was not attached.");
        if (entry == nullptr) {
            continue;
        }
        const Option<usize> position = entry->providers.IndexOf(type);
        NX_ASSERT(position.HasValue(), "Detaching a type table that was not attached.");
        if (position.HasValue()) {
            entry->providers.RemoveAt(position.GetValue());
        }
        const bool named = !IsNamedByIdentityOnly(*type);
        if (named && position.HasValue() && position.GetValue() == 0 && !entry->providers.IsEmpty()) {
            // The name key points into the detached description; key it by the next provider's copy of the name.
            static_cast<void>(m_names.Remove(Containers::StringView::FromCString(type->name)));
            m_names.Set(Containers::StringView::FromCString(entry->providers[0]->name), type->id);
        }
        if (entry->providers.IsEmpty()) {
            if (named) {
                static_cast<void>(m_names.Remove(Containers::StringView::FromCString(type->name)));
            }
            static_cast<void>(m_types.Remove(type->id));
        }
    }
}

const TypeInfo* TypeRegistry::Find(TypeId id) const {
    const Entry* const entry = m_types.Find(id);
    return entry != nullptr ? entry->providers[0] : nullptr;
}

const TypeInfo* TypeRegistry::FindByName(Containers::StringView name) const {
    const TypeId* const id = m_names.Find(name);
    return id != nullptr ? Find(*id) : nullptr;
}

const TypeInfo* TypeRegistry::FindRenamed(Containers::StringView previousName) const {
    for (const auto entry : m_types) {
        const TypeInfo* const type = entry.value.providers[0];
        if (Containers::StringView::FromCString(type->renamedFrom) == previousName && !previousName.IsEmpty()) {
            return type;
        }
    }
    return nullptr;
}

usize TypeRegistry::GetTypeCount() const {
    return m_types.Size();
}

} // namespace Nexyl::Reflection
