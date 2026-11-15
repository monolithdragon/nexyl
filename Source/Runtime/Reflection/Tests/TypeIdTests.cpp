#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Math/Vec3.h>
#include <Nexyl/Reflection/TypeId.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Reflection;

// Reference identities computed independently with FNV-1a 64 over the canonical names (see TypeId). The header
// tool computes the same values; if one of these changes, saved data and TypeId references break.
static_assert(TypeIdOf<bool>() == TypeId{0xCD2F'D49B'C6B0'14BDULL});
static_assert(TypeIdOf<u32>() == TypeId{0x4D2B'DF19'3E85'26D1ULL});
static_assert(TypeIdOf<i64>() == TypeId{0x2AE1'AF19'2B33'1746ULL});
static_assert(TypeIdOf<f32>() == TypeId{0xDD63'5018'FF70'3544ULL});
static_assert(TypeIdOf<Math::Vec3>() == TypeId{0xE681'F409'DE47'BB5CULL});
static_assert(TypeIdOf<Containers::Array<f32>>() == TypeId{0xFD79'BAE6'B03E'3650ULL});
static_assert(TypeIdOf<Containers::HashMap<u32, f64>>() == TypeId{0x9FDB'7BDA'D9C8'12C1ULL});
static_assert(TypeIdOf<f32[3]>() == TypeId{0xA94F'DC71'62F0'1A5CULL});

// Spelling does not matter: aliases, qualifiers and the platform's choice of integer type give one identity.
static_assert(TypeIdOf<const Math::Vec3>() == TypeIdOf<Math::Vec3>());
static_assert(TypeIdOf<usize>() == TypeIdOf<u64>());
static_assert(TypeIdOf<unsigned long long>() == TypeIdOf<u64>());
static_assert(TypeIdOf<long long>() == TypeIdOf<i64>());
static_assert(TypeIdOf<char>() != TypeIdOf<i8>() && TypeIdOf<char>() != TypeIdOf<u8>());
static_assert(TypeIdOf<Containers::Array<f32>>() != TypeIdOf<Containers::Array<f64>>());
static_assert(TypeIdOf<f32[3]>() != TypeIdOf<f32[4]>());
static_assert(TypeId{}.IsNull() && !TypeIdOf<bool>().IsNull());

NX_TEST(TypeId, HashesForMaps) {
    NX_CHECK(HashValue(TypeIdOf<u32>()) == HashValue(TypeIdOf<u32>()));
    NX_CHECK(HashValue(TypeIdOf<u32>()) != HashValue(TypeIdOf<u64>()));
}
