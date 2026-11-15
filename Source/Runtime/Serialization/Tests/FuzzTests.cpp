#include "MachineFixture.h"
#include "SerializationTestTypes.h"
#include "TestRegistry.h"

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/Heap.h>
#include <Nexyl/Memory/PageAllocator.h>
#include <Nexyl/Serialization/BinaryArchive.h>
#include <Nexyl/Serialization/TextArchive.h>
#include <Nexyl/Test/Fuzz.h>
#include <Nexyl/Test/Test.h>

// Fuzz tests of the archive readers (rule TEST-5): mutated archives must give an error or a value, never a crash
// or a failed assertion. CI also runs them under the address sanitizer. They allocate from a Heap: the guard
// allocator, which gives every block pages of its own, would make thousands of inputs slow.

using namespace Nexyl;
using namespace Nexyl::Serialization;
using namespace Nexyl::SerializationTest;

NX_TEST(SerializationFuzz, BinaryReaderSurvivesMutations) {
    Memory::PageAllocator pages;
    Memory::Heap allocator(pages);
    const TestRegistry types(allocator);
    Machine machine;
    FillMachine(machine, allocator);
    Containers::Array<u8> machineArchive(allocator);
    Containers::Array<u8> conveyorArchive(allocator);
    NX_REQUIRE(WriteBinary(types.Get(), machine, machineArchive).IsOk());
    NX_REQUIRE(WriteBinary(types.Get(), OldConveyor{1.0F, 2, 3, 4}, conveyorArchive).IsOk());
    const Span<const u8> corpus[] = {machineArchive, conveyorArchive};

    Containers::Array<u8> scratch(allocator);
    scratch.Resize(machineArchive.Size() + 256);
    usize accepted = 0;
    Test::FuzzOptions options;
    options.seed = 0xB1A7;
    options.iterations = 3000;
    const u32 runs = Test::Fuzz(corpus, scratch, options, [&types, &allocator, &accepted](Span<const u8> input) {
        Machine target;
        accepted += ReadBinary(types.Get(), input, target, allocator).IsOk() ? 1 : 0;
        NewConveyor conveyor;
        accepted += ReadBinary(types.Get(), input, conveyor, allocator).IsOk() ? 1 : 0;
    });
    NX_CHECK(runs == 3002);
    // The unmutated corpus reads; most mutations do not.
    NX_CHECK(accepted >= 2);
}

NX_TEST(SerializationFuzz, TextReaderSurvivesMutations) {
    Memory::PageAllocator pages;
    Memory::Heap allocator(pages);
    const TestRegistry types(allocator);
    Machine machine;
    FillMachine(machine, allocator);
    Containers::String machineText(allocator);
    Containers::String conveyorText(allocator);
    NX_REQUIRE(WriteText(types.Get(), machine, machineText).IsOk());
    NX_REQUIRE(WriteText(types.Get(), OldConveyor{1.0F, 2, 3, 4}, conveyorText).IsOk());
    const Span<const u8> corpus[] = {AsBytes(Span<const char>(machineText.Data(), machineText.Size())),
                                     AsBytes(Span<const char>(conveyorText.Data(), conveyorText.Size()))};
    const Span<const char> dictionary[] = {Span<const char>("{", 1),
                                           Span<const char>("}", 1),
                                           Span<const char>("[", 1),
                                           Span<const char>("]", 1),
                                           Span<const char>("\"", 1),
                                           Span<const char>(":", 1),
                                           Span<const char>(",", 1),
                                           Span<const char>("true", 4),
                                           Span<const char>("null", 4),
                                           Span<const char>("-0", 2),
                                           Span<const char>("1e308", 5),
                                           Span<const char>("1e999", 5),
                                           Span<const char>("\\u", 2),
                                           Span<const char>("\\ud800", 6),
                                           Span<const char>("\"value\"", 7),
                                           Span<const char>("\"nan\"", 5),
                                           Span<const char>("18446744073709551616", 20)};

    Containers::Array<u8> scratch(allocator);
    scratch.Resize(machineText.Size() + 256);
    usize accepted = 0;
    Test::FuzzOptions options;
    options.seed = 0x7E47;
    options.iterations = 3000;
    options.dictionary = dictionary;
    const u32 runs = Test::Fuzz(corpus, scratch, options, [&types, &allocator, &accepted](Span<const u8> input) {
        const Span<const char> characters = AsCharacters(input);
        const Containers::StringView text(characters.Data(), characters.Size());
        Machine target;
        accepted += ReadText(types.Get(), text, target, allocator).IsOk() ? 1 : 0;
        NewConveyor conveyor;
        accepted += ReadText(types.Get(), text, conveyor, allocator).IsOk() ? 1 : 0;
    });
    NX_CHECK(runs == 3002);
    NX_CHECK(accepted >= 2);
}
