#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Math/Vec3.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Reflection/IntrinsicTypes.h>
#include <Nexyl/Reflection/ReflectionErrors.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Reflection/TypeInfo.h>
#include <Nexyl/Reflection/TypeRegistry.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Reflection;

namespace {

[[nodiscard]] Memory::UniquePtr<TypeRegistry> CreateRegistry(Memory::IAllocator& allocator) {
    Result<Memory::UniquePtr<TypeRegistry>, Error> created = TypeRegistry::Create(allocator);
    NX_ASSERT(created.IsOk(), "Registry creation failed in a test.");
    return Move(created).TakeValue();
}

// Test fixtures: descriptions that clash with the built-in ones.
consteval TypeInfo MakeU32Like(const char* name, usize size) {
    TypeInfo info;
    info.name = name;
    info.id = TypeIdOf<u32>();
    info.size = size;
    info.alignment = size;
    info.kind = TypeKind::UnsignedInteger;
    return info;
}

constexpr TypeInfo kFakeU32 = MakeU32Like("NotU32", 4);
constexpr TypeInfo kWrongSizeU32 = MakeU32Like("u32", 8);
constexpr const TypeInfo* kFakeTypes[] = {&kFakeU32};
constexpr const TypeInfo* kWrongSizeTypes[] = {&kWrongSizeU32};
constexpr TypeTable kFakeTable{"Fake", Span<const TypeInfo* const>(kFakeTypes)};
constexpr TypeTable kWrongSizeTable{"WrongSize", Span<const TypeInfo* const>(kWrongSizeTypes)};

// Two tables that both provide Array<f32>, as two modules whose fields use it would.
constexpr const TypeInfo* kFirstUser[] = {&IntrinsicTypeInfo<Containers::Array<f32>>(), &IntrinsicTypeInfo<i32>()};
constexpr TypeInfo kSecondArrayCopy = IntrinsicTypeInfo<Containers::Array<f32>>();
constexpr TypeInfo kSecondI32Copy = IntrinsicTypeInfo<i32>();
constexpr const TypeInfo* kSecondUser[] = {&kSecondArrayCopy, &kSecondI32Copy};
constexpr TypeTable kFirstTable{"First", Span<const TypeInfo* const>(kFirstUser)};
constexpr TypeTable kSecondTable{"Second", Span<const TypeInfo* const>(kSecondUser)};

} // namespace

NX_TEST(IntrinsicTypes, DescribeFundamentalsAndContainers) {
    const TypeInfo& u16Info = IntrinsicTypeInfo<u16>();
    NX_CHECK(u16Info.kind == TypeKind::UnsignedInteger && u16Info.size == 2);
    NX_CHECK(Containers::StringView::FromCString(u16Info.name) == Containers::StringView("u16"));
    NX_CHECK(IntrinsicTypeInfo<usize>().id == IntrinsicTypeInfo<u64>().id);

    const TypeInfo& array = IntrinsicTypeInfo<Containers::Array<Math::Vec3>>();
    NX_CHECK(array.kind == TypeKind::Array);
    NX_CHECK(array.element == TypeIdOf<Math::Vec3>());
    NX_CHECK(array.arrayOperations != nullptr && array.arrayOperations->resize != nullptr);

    const TypeInfo& map = IntrinsicTypeInfo<Containers::HashMap<u32, Containers::String>>();
    NX_CHECK(map.kind == TypeKind::HashMap);
    NX_CHECK(map.element == TypeIdOf<u32>() && map.value == TypeIdOf<Containers::String>());

    const TypeInfo& fixed = IntrinsicTypeInfo<f32[3]>();
    NX_CHECK(fixed.kind == TypeKind::FixedArray && fixed.count == 3 && fixed.element == TypeIdOf<f32>());

    const TypeInfo& handle = IntrinsicTypeInfo<Containers::Handle<Math::Vec3>>();
    NX_CHECK(handle.kind == TypeKind::Handle && handle.size == 8);
}

NX_TEST(IntrinsicTypes, OperateOnContainersThroughTheirDescription) {
    Memory::GuardAllocator allocator;

    Containers::Array<i32> values;
    const ArrayOperations& arrays = *IntrinsicTypeInfo<Containers::Array<i32>>().arrayOperations;
    NX_REQUIRE(arrays.resize(&values, 3, allocator).IsOk());
    NX_CHECK(arrays.size(&values) == 3);
    *static_cast<i32*>(arrays.at(&values, 2)) = 7;
    NX_CHECK(values[2] == 7);
    NX_CHECK(*static_cast<const i32*>(arrays.atConst(&values, 2)) == 7);

    Containers::String text;
    const StringOperations& strings = *IntrinsicTypeInfo<Containers::String>().stringOperations;
    NX_REQUIRE(strings.assign(&text, Containers::StringView("reflected"), allocator).IsOk());
    NX_CHECK(strings.view(&text) == Containers::StringView("reflected"));

    Containers::HashMap<u32, f64> map;
    const MapOperations& maps = *IntrinsicTypeInfo<Containers::HashMap<u32, f64>>().mapOperations;
    u32 key = 5;
    f64 value = 2.5;
    NX_REQUIRE(maps.set(&map, &key, &value, allocator).IsOk());
    NX_CHECK(maps.size(&map) == 1);
    f64 sum = 0.0;
    maps.visit(
        &map,
        [](void* context, const void* /*key*/, const void* entryValue) {
            *static_cast<f64*>(context) += *static_cast<const f64*>(entryValue);
        },
        &sum);
    NX_CHECK(sum == 2.5);

    // Lifecycle functions construct and destroy in raw memory.
    alignas(Containers::String) u8 storage[sizeof(Containers::String)] = {};
    const TypeLifecycle& lifecycle = IntrinsicTypeInfo<Containers::String>().lifecycle;
    NX_REQUIRE(lifecycle.construct != nullptr && lifecycle.copy == nullptr && lifecycle.move != nullptr);
    lifecycle.construct(storage);
    lifecycle.destruct(storage);
}

NX_TEST(TypeRegistry, FindsAttachedTypesByIdentityAndName) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<TypeRegistry> registry = CreateRegistry(allocator);
    NX_REQUIRE(registry->Attach(GetBuiltinTypeTable()).IsOk());
    NX_CHECK(registry->GetTypeCount() == GetBuiltinTypeTable().types.Size());
    // A dynamic build gives every binary its own copy of a built-in description, so compare identities.
    NX_REQUIRE(registry->Find<u32>() != nullptr);
    NX_CHECK(registry->Find<u32>()->id == TypeIdOf<u32>());
    NX_REQUIRE(registry->FindByName("f64") != nullptr);
    NX_CHECK(registry->FindByName("f64")->id == TypeIdOf<f64>());
    NX_REQUIRE(registry->FindByName("Nexyl::Containers::String") != nullptr);
    NX_CHECK(registry->FindByName("Nexyl::Containers::String")->kind == TypeKind::String);
    NX_CHECK(registry->Find<Math::Vec3>() == nullptr);

    registry->Detach(GetBuiltinTypeTable());
    NX_CHECK(registry->GetTypeCount() == 0);
    NX_CHECK(registry->Find<u32>() == nullptr);
    NX_CHECK(registry->FindByName("f64") == nullptr);
}

NX_TEST(TypeRegistry, RejectsCollisionsAndMismatchesWithoutChange) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<TypeRegistry> registry = CreateRegistry(allocator);
    NX_REQUIRE(registry->Attach(GetBuiltinTypeTable()).IsOk());
    const usize count = registry->GetTypeCount();

    const Result<void, Error> collision = registry->Attach(kFakeTable);
    NX_REQUIRE(collision.IsError());
    NX_CHECK(collision.GetError().Is(ReflectionError::TypeIdCollision));

    const Result<void, Error> mismatch = registry->Attach(kWrongSizeTable);
    NX_REQUIRE(mismatch.IsError());
    NX_CHECK(mismatch.GetError().Is(ReflectionError::TypeMismatch));

    NX_CHECK(registry->GetTypeCount() == count);
    NX_REQUIRE(registry->Find<u32>() != nullptr);
    NX_CHECK(Containers::StringView::FromCString(registry->Find<u32>()->name) == Containers::StringView("u32"));
    registry->Detach(GetBuiltinTypeTable());
}

NX_TEST(TypeRegistry, KeepsSharedTypesWhileAnyProviderIsAttached) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<TypeRegistry> registry = CreateRegistry(allocator);
    NX_REQUIRE(registry->Attach(kFirstTable).IsOk());
    NX_REQUIRE(registry->Attach(kSecondTable).IsOk());
    NX_CHECK(registry->GetTypeCount() == 2);

    registry->Detach(kFirstTable);
    // The second table's copies answer now, also by name.
    NX_CHECK(registry->Find<Containers::Array<f32>>() == &kSecondArrayCopy);
    NX_CHECK(registry->FindByName("i32") == &kSecondI32Copy);

    registry->Detach(kSecondTable);
    NX_CHECK(registry->GetTypeCount() == 0);
}
