#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Math/Mat3.h>
#include <Nexyl/Math/Pose.h>
#include <Nexyl/Math/Vec3.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Reflection/FoundationTypes.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Reflection/TypeInfo.h>
#include <Nexyl/Reflection/TypeRegistry.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Reflection;

namespace {

// A registry with the built-in and the generated foundation tables attached; detaches them when it goes.
class FoundationRegistry final {
public:
    FoundationRegistry() {
        Result<Memory::UniquePtr<TypeRegistry>, Error> created = TypeRegistry::Create(m_allocator);
        NX_ASSERT(created.IsOk(), "Registry creation failed in a test.");
        m_registry = Move(created).TakeValue();
        m_attached =
            m_registry->Attach(GetBuiltinTypeTable()).IsOk() && m_registry->Attach(GetFoundationTypeTables()).IsOk();
    }

    ~FoundationRegistry() {
        for (const TypeTable* const table : GetFoundationTypeTables()) {
            m_registry->Detach(*table);
        }
        m_registry->Detach(GetBuiltinTypeTable());
    }

    FoundationRegistry(const FoundationRegistry&) = delete;
    FoundationRegistry& operator=(const FoundationRegistry&) = delete;
    FoundationRegistry(FoundationRegistry&&) = delete;
    FoundationRegistry& operator=(FoundationRegistry&&) = delete;

    [[nodiscard]] bool IsAttached() const {
        return m_attached;
    }

    [[nodiscard]] const TypeRegistry& Get() const {
        return *m_registry;
    }

private:
    Memory::GuardAllocator m_allocator;
    Memory::UniquePtr<TypeRegistry> m_registry;
    bool m_attached = false;
};

[[nodiscard]] bool NameIs(const char* name, const char* expected) {
    return Containers::StringView::FromCString(name) == Containers::StringView::FromCString(expected);
}

} // namespace

NX_TEST(FoundationTypes, HasTheTablesOfBaseAndMath) {
    const Span<const TypeTable* const> tables = GetFoundationTypeTables();
    NX_REQUIRE(tables.Size() == 2);
    NX_CHECK(NameIs(tables[0]->module, "Base"));
    NX_CHECK(NameIs(tables[1]->module, "Math"));
    const FoundationRegistry registry;
    NX_CHECK(registry.IsAttached());
}

NX_TEST(FoundationTypes, DescribeRecordsFieldByField) {
    const FoundationRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    const TypeInfo* const vec3 = registry.Get().Find<Math::Vec3>();
    NX_REQUIRE(vec3 != nullptr);
    NX_CHECK(vec3->kind == TypeKind::Record);
    NX_CHECK(vec3->size == sizeof(Math::Vec3));
    NX_REQUIRE(vec3->fields.Size() == 3);
    NX_CHECK(NameIs(vec3->fields[2].name, "z"));
    NX_CHECK(vec3->fields[2].offset == 8);
    NX_CHECK(vec3->fields[2].type == TypeIdOf<f32>());
    NX_CHECK(vec3->fields[2].flags.Has(FieldFlag::Public));
    NX_CHECK(NameIs(vec3->fields[2].description, "The up component."));
    NX_CHECK(registry.Get().FindByName("Nexyl::Math::Vec3") == vec3);

    // Nested records and built-in arrays resolve through the registry.
    const TypeInfo* const pose = registry.Get().Find<Math::Pose>();
    NX_REQUIRE(pose != nullptr && pose->fields.Size() == 2);
    NX_CHECK(pose->fields[0].type == TypeIdOf<Math::Vec3d>());
    const TypeInfo* const mat3 = registry.Get().Find<Math::Mat3>();
    NX_REQUIRE(mat3 != nullptr && mat3->fields.Size() == 1);
    const TypeInfo* const columns = registry.Get().Find(mat3->fields[0].type);
    NX_REQUIRE(columns != nullptr);
    NX_CHECK(columns->kind == TypeKind::FixedArray);
    NX_CHECK(columns->count == 3 && columns->element == TypeIdOf<Math::Vec3>());
}

NX_TEST(FoundationTypes, DescribeEnumerationsAndOpaqueTypes) {
    const FoundationRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    const TypeInfo* const level = registry.Get().Find<LogLevel>();
    NX_REQUIRE(level != nullptr);
    NX_CHECK(level->kind == TypeKind::Enum);
    NX_CHECK(level->element == TypeIdOf<u8>());
    NX_REQUIRE(level->enumValues.Size() == 7);
    NX_CHECK(NameIs(level->enumValues[3].name, "Warning"));
    NX_CHECK(level->enumValues[3].value == static_cast<i64>(LogLevel::Warning));

    const TypeInfo* const error = registry.Get().Find<Error>();
    NX_REQUIRE(error != nullptr);
    NX_CHECK(error->kind == TypeKind::Opaque);
    NX_CHECK(error->fields.IsEmpty());
    NX_CHECK(error->size == sizeof(Error));
}

NX_TEST(FoundationTypes, EveryFieldTypeIsKnown) {
    // The tables are closed: every field, base, element and enumeration type they name is attached too.
    const FoundationRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    usize checked = 0;
    for (const TypeTable* const table : GetFoundationTypeTables()) {
        for (const TypeInfo* const type : table->types) {
            for (const FieldInfo& field : type->fields) {
                NX_CHECK(registry.Get().Find(field.type) != nullptr);
                ++checked;
            }
            if (!type->base.IsNull()) {
                NX_CHECK(registry.Get().Find(type->base) != nullptr);
            }
            if (!type->element.IsNull()) {
                NX_CHECK(registry.Get().Find(type->element) != nullptr);
            }
        }
    }
    NX_CHECK(checked > 20);
}

NX_TEST(FoundationTypes, ConstructRecordsThroughTheirLifecycle) {
    const FoundationRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    const TypeInfo* const vec3 = registry.Get().Find<Math::Vec3>();
    NX_REQUIRE(vec3 != nullptr && vec3->lifecycle.construct != nullptr && vec3->lifecycle.copy != nullptr);
    const Math::Vec3 source{1.0F, 2.0F, 3.0F};
    Math::Vec3 copy{};
    vec3->lifecycle.destruct(&copy);
    vec3->lifecycle.copy(&copy, &source);
    NX_CHECK(copy.y == 2.0F);
}
