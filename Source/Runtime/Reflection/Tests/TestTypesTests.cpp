#include "ReflectionTestTypes.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Reflection/FoundationTypes.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Reflection/TypeInfo.h>
#include <Nexyl/Reflection/TypeRegistry.h>
#include <Nexyl/Test/Test.h>

namespace Nexyl::Reflection::Generated {

// Generated from Tests/ReflectionTestTypes.h by the build.
extern const TypeTable kReflectionTestsTypeTable;

} // namespace Nexyl::Reflection::Generated

using namespace Nexyl;
using namespace Nexyl::Reflection;

namespace {

class TestRegistry final {
public:
    TestRegistry() {
        Result<Memory::UniquePtr<TypeRegistry>, Error> created = TypeRegistry::Create(m_allocator);
        NX_ASSERT(created.IsOk(), "Registry creation failed in a test.");
        m_registry = Move(created).TakeValue();
        m_attached = m_registry->Attach(GetBuiltinTypeTable()).IsOk() &&
                     m_registry->Attach(GetFoundationTypeTables()).IsOk() &&
                     m_registry->Attach(Generated::kReflectionTestsTypeTable).IsOk();
    }

    ~TestRegistry() {
        m_registry->Detach(Generated::kReflectionTestsTypeTable);
        for (const TypeTable* const table : GetFoundationTypeTables()) {
            m_registry->Detach(*table);
        }
        m_registry->Detach(GetBuiltinTypeTable());
    }

    TestRegistry(const TestRegistry&) = delete;
    TestRegistry& operator=(const TestRegistry&) = delete;
    TestRegistry(TestRegistry&&) = delete;
    TestRegistry& operator=(TestRegistry&&) = delete;

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

[[nodiscard]] bool Is(const char* text, const char* expected) {
    return Containers::StringView::FromCString(text) == Containers::StringView::FromCString(expected);
}

[[nodiscard]] const FieldInfo* FindField(const TypeInfo& type, const char* name) {
    for (const FieldInfo& field : type.fields) {
        if (Is(field.name, name)) {
            return &field;
        }
    }
    return nullptr;
}

} // namespace

NX_TEST(TestTypes, CarryTheirTags) {
    const TestRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    const TypeInfo* const everything = registry.Get().Find<ReflectionTest::Everything>();
    NX_REQUIRE(everything != nullptr);
    NX_CHECK(everything->version == 2);
    NX_CHECK(Is(everything->category, "Tests"));
    // Thirteen fields: the transient pointer is left out.
    NX_CHECK(everything->fields.Size() == 13);
    NX_CHECK(FindField(*everything, "cache") == nullptr);

    const FieldInfo* const speed = FindField(*everything, "speed");
    NX_REQUIRE(speed != nullptr);
    NX_CHECK(Is(speed->units, "m/s"));
    NX_CHECK(speed->hasRange && speed->rangeMinimum == 0.0 && speed->rangeMaximum == 5.0);
    NX_CHECK(speed->step == 0.5);
    NX_CHECK(Is(speed->description, "Speed of the thing."));

    const FieldInfo* const glow = FindField(*everything, "glow");
    NX_REQUIRE(glow != nullptr);
    NX_CHECK(glow->flags.Has(FieldFlag::Presentation) && glow->flags.Has(FieldFlag::ReadOnly));

    const TypeInfo* const mode = registry.Get().Find<ReflectionTest::Mode>();
    NX_REQUIRE(mode != nullptr && mode->enumValues.Size() == 3);
    NX_CHECK(mode->version == 3);
    NX_CHECK(Is(mode->enumValues[1].renamedFrom, "Quick"));
    NX_CHECK(mode->enumValues[2].hidden);
}

NX_TEST(TestTypes, ResolveEveryFieldType) {
    const TestRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    const TypeInfo* const everything = registry.Get().Find<ReflectionTest::Everything>();
    NX_REQUIRE(everything != nullptr);
    for (const FieldInfo& field : everything->fields) {
        NX_CHECK(registry.Get().Find(field.type) != nullptr);
    }
    const TypeInfo* const matrix = registry.Get().Find(FindField(*everything, "matrix")->type);
    NX_REQUIRE(matrix != nullptr && matrix->kind == TypeKind::FixedArray && matrix->count == 2);
    const TypeInfo* const row = registry.Get().Find(matrix->element);
    NX_REQUIRE(row != nullptr && row->kind == TypeKind::FixedArray && row->count == 3);
    const TypeInfo* const labels = registry.Get().Find(FindField(*everything, "labels")->type);
    NX_REQUIRE(labels != nullptr && labels->kind == TypeKind::HashMap);
}

NX_TEST(TestTypes, ConfirmPrivateOffsets) {
    const TestRegistry registry;
    NX_REQUIRE(registry.IsAttached());
    const TypeInfo* const counter = registry.Get().Find<ReflectionTest::Counter>();
    NX_REQUIRE(counter != nullptr && counter->fields.Size() == 4);
    NX_CHECK(!counter->fields[0].flags.Has(FieldFlag::Public));
    NX_REQUIRE(counter->checkLayout != nullptr);
    NX_CHECK(counter->checkLayout());

    // Every generated record that has a layout check passes it.
    usize checked = 0;
    for (const TypeInfo* const type : Generated::kReflectionTestsTypeTable.types) {
        if (type->checkLayout != nullptr) {
            NX_CHECK(type->checkLayout());
            ++checked;
        }
    }
    NX_CHECK(checked >= 1);
}
