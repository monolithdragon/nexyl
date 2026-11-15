#include "MachineFixture.h"
#include "SerializationTestTypes.h"
#include "TestRegistry.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Serialization/BinaryArchive.h>
#include <Nexyl/Serialization/SerializationErrors.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Serialization;
using namespace Nexyl::SerializationTest;

namespace {

template <typename T>
[[nodiscard]] Containers::Array<u8> Write(const TestRegistry& types, const T& object, Memory::IAllocator& allocator) {
    Containers::Array<u8> archive(allocator);
    const Result<void, Error> written = WriteBinary(types.Get(), object, archive);
    NX_ASSERT(written.IsOk(), "WriteBinary failed in a test.");
    static_cast<void>(written);
    return archive;
}

[[nodiscard]] bool FailsWith(const Result<void, Error>& result, SerializationError code) {
    return result.IsError() && result.GetError().Is(code);
}

} // namespace

NX_TEST(BinaryArchive, RoundTripsEveryKind) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    Machine source;
    FillMachine(source, allocator);
    const Containers::Array<u8> archive = Write(types, source, allocator);

    Machine copy;
    NX_REQUIRE(ReadBinary(types.Get(), archive, copy, allocator).IsOk());
    NX_CHECK(copy.kind == Kind::Robot && copy.running && copy.code == 'z');
    NX_CHECK(copy.balance == source.balance && copy.level == 200 && copy.precise == 0.1);
    NX_CHECK(copy.placement.position[2] == 1e300 && copy.placement.direction.z == 1.0F);
    NX_CHECK(copy.grid[1][2] == -7 && copy.grid[0][0] == 0);
    NX_CHECK(copy.name == Containers::StringView("Unit 7"));
    NX_REQUIRE(copy.route.Size() == 2);
    NX_CHECK(copy.route[1].position.y == 5.0F && copy.route[1].dwell == 1.5F);
    NX_REQUIRE(copy.labels.Size() == 2);
    NX_CHECK(*copy.labels.Find(u32{3}) == Containers::StringView("three"));
    // The map keeps its insertion order.
    NX_CHECK((*copy.labels.begin()).key == 10);
    NX_REQUIRE(copy.batches.Size() == 2);
    NX_CHECK(copy.batches[0].Size() == 3 && copy.batches[0][2] == 3 && copy.batches[1].IsEmpty());
    NX_CHECK(copy.next.index == 4 && copy.next.generation == 9);
    NX_CHECK(copy.GetTicks() == 0xFFFF'FFFF'FFFFULL);
    // Transient fields are not written.
    NX_CHECK(copy.cache == 0);
}

NX_TEST(BinaryArchive, IsDeterministic) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    Machine source;
    FillMachine(source, allocator);
    const Containers::Array<u8> first = Write(types, source, allocator);
    const Containers::Array<u8> second = Write(types, source, allocator);
    NX_REQUIRE(first.Size() == second.Size());
    bool same = true;
    for (usize index = 0; index < first.Size(); ++index) {
        same = same && first[index] == second[index];
    }
    NX_CHECK(same);
}

NX_TEST(BinaryArchive, ReadsAnEarlierVersionOfAType) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    const OldConveyor old{2.5F, 42, 17, 3};
    const Containers::Array<u8> archive = Write(types, old, allocator);

    NewConveyor current;
    NX_REQUIRE(ReadBinary(types.Get(), archive, current, allocator).IsOk());
    NX_CHECK(current.beltSpeed == 2.5F); // renamed field
    NX_CHECK(current.count == 17);       // same field
    NX_CHECK(current.level == 0.5F);     // retyped field: skipped, keeps its value
    NX_CHECK(current.added == 7);        // new field: keeps its value
}

NX_TEST(BinaryArchive, RejectsMalformedArchives) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    Machine source;
    FillMachine(source, allocator);
    Containers::Array<u8> archive = Write(types, source, allocator);

    // Every proper prefix is an error, never a crash.
    usize failures = 0;
    for (usize length = 0; length < archive.Size(); ++length) {
        Machine target;
        failures +=
            ReadBinary(types.Get(), Span<const u8>(archive.Data(), length), target, allocator).IsError() ? 1 : 0;
    }
    NX_CHECK(failures == archive.Size());

    Machine target;
    archive[0] = 'X';
    NX_CHECK(FailsWith(ReadBinary(types.Get(), archive, target, allocator), SerializationError::InvalidFormat));
    archive[0] = 'N';
    archive[4] = 2; // a format version from the future
    NX_CHECK(FailsWith(ReadBinary(types.Get(), archive, target, allocator), SerializationError::UnsupportedVersion));
    archive[4] = 1;
    archive.Push(0); // trailing bytes
    NX_CHECK(FailsWith(ReadBinary(types.Get(), archive, target, allocator), SerializationError::InvalidFormat));
    static_cast<void>(archive.Pop());
    NX_CHECK(ReadBinary(types.Get(), archive, target, allocator).IsOk());

    // An archive of another type.
    NewConveyor conveyor;
    NX_CHECK(FailsWith(ReadBinary(types.Get(), archive, conveyor, allocator), SerializationError::TypeMismatch));

    // A bool must be 0 or 1; it is the last byte of the archive of a Flag.
    Containers::Array<u8> flag = Write(types, Flag{true}, allocator);
    flag[flag.Size() - 1] = 2;
    Flag readFlag;
    NX_CHECK(FailsWith(ReadBinary(types.Get(), flag, readFlag, allocator), SerializationError::InvalidValue));
}

NX_TEST(BinaryArchive, RefusesTypesThatAreNotData) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    const Error opaque(SerializationError::InvalidValue);
    Containers::Array<u8> archive(allocator);
    NX_CHECK(FailsWith(WriteBinary(types.Get(), opaque, archive), SerializationError::NotSerializable));
}
