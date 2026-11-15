#pragma once

// A TypeRegistry with the built-in, foundation and test tables, shared by the Serialization tests.

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Reflection/FoundationTypes.h>
#include <Nexyl/Reflection/TypeInfo.h>
#include <Nexyl/Reflection/TypeRegistry.h>

namespace Nexyl::Reflection::Generated {

// Generated from Tests/SerializationTestTypes.h by the build.
extern const TypeTable kSerializationTestsTypeTable;

} // namespace Nexyl::Reflection::Generated

namespace Nexyl::SerializationTest {

/// Not data: holds the registry the tests use.
/// @opaque
class TestRegistry final {
public:
    explicit TestRegistry(Memory::IAllocator& allocator) {
        Result<Memory::UniquePtr<Reflection::TypeRegistry>, Error> created =
            Reflection::TypeRegistry::Create(allocator);
        NX_ASSERT(created.IsOk(), "Registry creation failed in a test.");
        m_registry = Move(created).TakeValue();
        const bool attached = m_registry->Attach(Reflection::GetBuiltinTypeTable()).IsOk() &&
                              m_registry->Attach(Reflection::GetFoundationTypeTables()).IsOk() &&
                              m_registry->Attach(Reflection::Generated::kSerializationTestsTypeTable).IsOk();
        NX_ASSERT(attached, "The test tables do not attach.");
        static_cast<void>(attached);
    }

    ~TestRegistry() {
        m_registry->Detach(Reflection::Generated::kSerializationTestsTypeTable);
        for (const Reflection::TypeTable* const table : Reflection::GetFoundationTypeTables()) {
            m_registry->Detach(*table);
        }
        m_registry->Detach(Reflection::GetBuiltinTypeTable());
    }

    TestRegistry(const TestRegistry&) = delete;
    TestRegistry& operator=(const TestRegistry&) = delete;
    TestRegistry(TestRegistry&&) = delete;
    TestRegistry& operator=(TestRegistry&&) = delete;

    [[nodiscard]] const Reflection::TypeRegistry& Get() const {
        return *m_registry;
    }

private:
    Memory::UniquePtr<Reflection::TypeRegistry> m_registry;
};

} // namespace Nexyl::SerializationTest
