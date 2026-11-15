#include "MachineFixture.h"
#include "SerializationTestTypes.h"
#include "TestRegistry.h"

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Serialization/SerializationErrors.h>
#include <Nexyl/Serialization/TextArchive.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Serialization;
using namespace Nexyl::SerializationTest;

namespace {

template <typename T>
[[nodiscard]] Containers::String Write(const TestRegistry& types, const T& object, Memory::IAllocator& allocator) {
    Containers::String text(allocator);
    const Result<void, Error> written = WriteText(types.Get(), object, text);
    NX_ASSERT(written.IsOk(), "WriteText failed in a test.");
    static_cast<void>(written);
    return text;
}

[[nodiscard]] bool FailsWith(const Result<void, Error>& result, SerializationError code) {
    return result.IsError() && result.GetError().Is(code);
}

// A minimal document around a value of NewConveyor.
constexpr const char kConveyorPrefix[] = R"({"nexyl": 1, "type": "Nexyl::SerializationTest::NewConveyor", "value": )";

[[nodiscard]] Result<void, Error> ReadConveyor(const TestRegistry& types, Containers::StringView value,
                                               NewConveyor& conveyor, Memory::IAllocator& allocator) {
    Containers::String text(allocator, kConveyorPrefix);
    text.Append(value);
    text.Append("}");
    return ReadText(types.Get(), text.View(), conveyor, allocator);
}

} // namespace

NX_TEST(TextArchive, RoundTripsEveryKind) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    Machine source;
    FillMachine(source, allocator);
    const Containers::String text = Write(types, source, allocator);

    Machine copy;
    NX_REQUIRE(ReadText(types.Get(), text.View(), copy, allocator).IsOk());
    NX_CHECK(copy.kind == Kind::Robot && copy.running && copy.code == 'z');
    NX_CHECK(copy.balance == source.balance && copy.level == 200 && copy.precise == 0.1);
    NX_CHECK(copy.placement.position[2] == 1e300 && copy.placement.direction.z == 1.0F);
    NX_CHECK(copy.grid[1][2] == -7);
    NX_CHECK(copy.name == Containers::StringView("Unit 7"));
    NX_REQUIRE(copy.route.Size() == 2);
    NX_CHECK(copy.route[1].position.y == 5.0F && copy.route[1].dwell == 1.5F);
    NX_REQUIRE(copy.labels.Size() == 2);
    NX_CHECK(*copy.labels.Find(u32{10}) == Containers::StringView("ten"));
    NX_CHECK((*copy.labels.begin()).key == 10);
    NX_REQUIRE(copy.batches.Size() == 2);
    NX_CHECK(copy.batches[0].Size() == 3 && copy.batches[0][1] == 2 && copy.batches[1].IsEmpty());
    NX_CHECK(copy.next.index == 4 && copy.next.generation == 9);
    NX_CHECK(copy.GetTicks() == 0xFFFF'FFFF'FFFFULL);
    NX_CHECK(copy.cache == 0);
}

NX_TEST(TextArchive, WritesAStableReadableLayout) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    Machine source;
    FillMachine(source, allocator);
    const Containers::String text = Write(types, source, allocator);
    const Containers::StringView view = text.View();
    NX_CHECK(view.StartsWith("{\n  \"nexyl\": 1,\n  \"type\": \"Nexyl::SerializationTest::Machine\",\n"));
    NX_CHECK(view.Contains("\n    \"kind\": \"Robot\",\n"));
    NX_CHECK(view.Contains("\n    \"precise\": 0.1,\n"));
    NX_CHECK(view.Contains("\"position\": [0, 0, 1e+300]"));
    NX_CHECK(view.Contains("\"grid\": [\n      [0, 0, 0],\n      [0, 0, -7]\n    ]"));
    NX_CHECK(view.Contains("\"labels\": [\n      [10, \"ten\"],\n      [3, \"three\"]\n    ]"));
    NX_CHECK(view.Contains("\"next\": [4, 9]"));
    NX_CHECK(view.Contains("\"m_ticks\": 281474976710655"));
    NX_CHECK(!view.Contains("cache"));
    NX_CHECK(view.Contains("\"Nexyl::SerializationTest::Machine\": 1"));
    NX_CHECK(Write(types, source, allocator) == text);
}

NX_TEST(TextArchive, ReadsAnEarlierVersionOfAType) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    const OldConveyor old{2.5F, 42, 17, 3};
    const Containers::String text = Write(types, old, allocator);

    NewConveyor current;
    NX_REQUIRE(ReadText(types.Get(), text.View(), current, allocator).IsOk());
    NX_CHECK(current.beltSpeed == 2.5F); // renamed field
    NX_CHECK(current.count == 17);
    NX_CHECK(current.level == 3.0F); // the number fits the new f32 field
    NX_CHECK(current.added == 7);    // new field keeps its value
}

NX_TEST(TextArchive, ReadsHandWrittenJson) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    NewConveyor conveyor;
    // Any key order and white space, unknown keys of any kind, and a value that does not fit keeps the old one.
    NX_REQUIRE(ReadConveyor(types,
                            "{ \"unknown\": {\"a\": [1, null, true, \"x\"]},\n\t\"count\" : 5 , "
                            "\"beltSpeed\": -1.5e2, \"added\": -3, \"level\": \"high\" }",
                            conveyor, allocator)
                   .IsOk());
    NX_CHECK(conveyor.count == 5 && conveyor.beltSpeed == -150.0F);
    NX_CHECK(conveyor.added == 7 && conveyor.level == 0.5F);

    Machine machine;
    const char text[] = R"({"nexyl": 1, "type": "Nexyl::SerializationTest::Machine", "value": {)"
                        R"("name": "caf\u00e9 \ud83d\ude00 \"q\" \\ \n", "code": "\u0041", )"
                        R"("precise": "-inf", "kind": 300, "labels": [[7, "seven"]], "level": 256}})";
    NX_REQUIRE(ReadText(types.Get(), Containers::StringView(text), machine, allocator).IsOk());
    NX_CHECK(machine.name == Containers::StringView("caf\xC3\xA9 \xF0\x9F\x98\x80 \"q\" \\ \n"));
    NX_CHECK(machine.code == 'A');
    NX_CHECK(machine.precise < -1e308);
    NX_CHECK(machine.kind == Kind::Robot);
    NX_CHECK(machine.labels.Size() == 1);
    NX_CHECK(machine.level == 0); // 256 does not fit in u8
}

NX_TEST(TextArchive, RejectsMalformedText) {
    Memory::GuardAllocator allocator;
    const TestRegistry types(allocator);
    NewConveyor conveyor;
    const Containers::StringView malformed[] = {
        "{\"count\": 1,}", "{\"count\": 01}", "{\"count\": 1",    "{\"count\" 1}",    "{count: 1}",
        "{\"count\": .5}", "{\"count\": 1e}", "{\"count\": tru}", "{\"a\": \"\\x\"}", "{\"a\": \"\\ud800\"}"};
    for (const Containers::StringView value : malformed) {
        NX_CHECK(FailsWith(ReadConveyor(types, value, conveyor, allocator), SerializationError::InvalidFormat));
    }

    const auto read = [&types, &conveyor, &allocator](const char* text) {
        return ReadText(types.Get(), Containers::StringView::FromCString(text), conveyor, allocator);
    };
    NX_CHECK(FailsWith(read(R"({"nexyl": 1, "type": "Other", "value": {}})"), SerializationError::TypeMismatch));
    NX_CHECK(FailsWith(read(R"({"nexyl": 2, "type": "x", "value": {}})"), SerializationError::UnsupportedVersion));
    NX_CHECK(FailsWith(read(R"({"nexyl": 1, "type": "Nexyl::SerializationTest::NewConveyor"})"),
                       SerializationError::InvalidFormat));
    NX_CHECK(FailsWith(read(R"({"nexyl": 1, "type": "Nexyl::SerializationTest::NewConveyor", "value": {}} x)"),
                       SerializationError::InvalidFormat));

    // Deep nesting is refused before the stack is at risk.
    Containers::String deep(allocator,
                            R"({"nexyl": 1, "type": "Nexyl::SerializationTest::NewConveyor", "value": {"x": )");
    for (u32 level = 0; level < 200; ++level) {
        deep.Append("[");
    }
    NX_CHECK(FailsWith(ReadText(types.Get(), deep.View(), conveyor, allocator), SerializationError::LimitExceeded));

    // Every proper prefix of a valid archive is an error.
    Machine source;
    FillMachine(source, allocator);
    const Containers::String text = Write(types, source, allocator);
    usize failures = 0;
    usize trimmed = text.Size();
    while (trimmed > 0 && (text[trimmed - 1] == '\n')) {
        --trimmed;
    }
    for (usize length = 0; length < trimmed; ++length) {
        Machine target;
        failures += ReadText(types.Get(), text.View().First(length), target, allocator).IsError() ? 1 : 0;
    }
    NX_CHECK(failures == trimmed);
}
