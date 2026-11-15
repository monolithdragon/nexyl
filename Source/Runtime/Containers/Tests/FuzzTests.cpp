#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Test/Fuzz.h>
#include <Nexyl/Test/Test.h>

// Differential fuzz tests (D-36): the bytes of each input are a sequence of operations, applied to a container
// and to a plain reference model; after every input both must hold the same elements in the same order.

using namespace Nexyl;
using namespace Nexyl::Containers;

namespace {

constexpr usize kModelCapacity = 1024;
constexpr u8 kSeedOperations[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20};
constexpr Span<const u8> kCorpus[] = {Span<const u8>(kSeedOperations)};

// Reads operation arguments from the input; past the end it reads zeros.
class OperationReader final {
public:
    explicit OperationReader(Span<const u8> input) : m_input(input) {}

    [[nodiscard]] bool HasMore() const {
        return m_position < m_input.Size();
    }

    u8 Next() {
        return m_position < m_input.Size() ? m_input[m_position++] : u8{0};
    }

private:
    Span<const u8> m_input;
    usize m_position = 0;
};

struct ArrayModel {
    u32 values[kModelCapacity] = {};
    usize size = 0;
};

[[nodiscard]] bool ApplyArrayOperation(OperationReader& reader, Array<u32>& array, ArrayModel& model) {
    const u8 operation = reader.Next() % 7;
    const u32 value = reader.Next();
    const usize where = reader.Next();
    if (operation == 0 && model.size < kModelCapacity) { // push
        array.Push(value);
        model.values[model.size++] = value;
    } else if (operation == 1 && model.size > 0) { // pop
        const u32 popped = array.Pop();
        --model.size;
        return popped == model.values[model.size];
    } else if (operation == 2 && model.size < kModelCapacity) { // insert
        const usize index = where % (model.size + 1);
        array.Insert(index, value);
        for (usize position = model.size; position > index; --position) {
            model.values[position] = model.values[position - 1];
        }
        model.values[index] = value;
        ++model.size;
    } else if (operation == 3 && model.size > 0) { // remove, keeping order
        const usize index = where % model.size;
        array.RemoveAt(index);
        for (usize position = index; position + 1 < model.size; ++position) {
            model.values[position] = model.values[position + 1];
        }
        --model.size;
    } else if (operation == 4 && model.size > 0) { // remove by swapping the last element in
        const usize index = where % model.size;
        array.RemoveAtSwap(index);
        model.values[index] = model.values[model.size - 1];
        --model.size;
    } else if (operation == 5) { // resize; new elements are zero
        const usize size = where % 64;
        array.Resize(size);
        for (usize position = model.size; position < size; ++position) {
            model.values[position] = 0;
        }
        model.size = size;
    } else if (operation == 6) {
        array.Clear();
        model.size = 0;
    }
    return true;
}

struct MapEntry {
    u32 key = 0;
    u32 value = 0;
};

struct MapModel {
    MapEntry entries[kModelCapacity] = {};
    usize size = 0;

    [[nodiscard]] usize Find(u32 key) const {
        for (usize index = 0; index < size; ++index) {
            if (entries[index].key == key) {
                return index;
            }
        }
        return size;
    }
};

[[nodiscard]] bool ApplyMapOperation(OperationReader& reader, HashMap<u32, u32>& map, MapModel& model) {
    const u8 operation = reader.Next() % 5;
    const u32 key = reader.Next() % 48; // few keys, so that operations meet existing entries
    const u32 value = reader.Next();
    const usize found = model.Find(key);
    const bool present = found < model.size;
    if (operation == 0) { // insert if absent
        const bool inserted = map.Insert(key, value);
        if (!present) {
            model.entries[model.size++] = MapEntry{key, value};
        }
        return inserted == !present;
    }
    if (operation == 1) { // set
        map.Set(key, value);
        if (present) {
            model.entries[found].value = value;
        } else {
            model.entries[model.size++] = MapEntry{key, value};
        }
    } else if (operation == 2) { // remove: the last entry takes the gap (D-32)
        const bool removed = map.Remove(key);
        if (present) {
            model.entries[found] = model.entries[model.size - 1];
            --model.size;
        }
        return removed == present;
    } else if (operation == 3) { // remove, keeping order
        const bool removed = map.RemoveOrdered(key);
        if (present) {
            for (usize index = found; index + 1 < model.size; ++index) {
                model.entries[index] = model.entries[index + 1];
            }
            --model.size;
        }
        return removed == present;
    } else { // find
        const u32* const stored = map.Find(key);
        return present ? stored != nullptr && *stored == model.entries[found].value : stored == nullptr;
    }
    return true;
}

[[nodiscard]] bool SameArray(const Array<u32>& array, const ArrayModel& model) {
    if (array.Size() != model.size) {
        return false;
    }
    for (usize index = 0; index < model.size; ++index) {
        if (array[index] != model.values[index]) {
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool SameMap(const HashMap<u32, u32>& map, const MapModel& model) {
    if (map.Size() != model.size) {
        return false;
    }
    usize index = 0;
    for (const auto entry : map) {
        if (entry.key != model.entries[index].key || entry.value != model.entries[index].value) {
            return false;
        }
        ++index;
    }
    return true;
}

struct TextModel {
    char text[kModelCapacity] = {};
    usize size = 0;
};

[[nodiscard]] bool ApplyStringOperation(OperationReader& reader, String& string, TextModel& model) {
    const u8 operation = reader.Next() % 4;
    const char character = static_cast<char>('a' + (reader.Next() % 26));
    const usize where = reader.Next();
    if (operation == 0 && model.size < kModelCapacity) {
        string.Append(character);
        model.text[model.size++] = character;
    } else if (operation == 1 && model.size + 2 <= kModelCapacity) { // insert two characters
        const usize index = where % (model.size + 1);
        const char pair[2] = {character, character};
        string.Insert(index, StringView(pair, 2));
        for (usize position = model.size + 1; position >= index + 2; --position) {
            model.text[position] = model.text[position - 2];
        }
        model.text[index] = character;
        model.text[index + 1] = character;
        model.size += 2;
    } else if (operation == 2 && model.size > 0) { // remove up to three characters
        const usize index = where % model.size;
        const usize count = (model.size - index) < 3 ? model.size - index : 3;
        string.Remove(index, count);
        for (usize position = index; position + count < model.size; ++position) {
            model.text[position] = model.text[position + count];
        }
        model.size -= count;
    } else if (operation == 3) { // resize with a fill character
        const usize size = where % 48;
        string.Resize(size, character);
        for (usize position = model.size; position < size; ++position) {
            model.text[position] = character;
        }
        model.size = size;
    }
    return true;
}

} // namespace

NX_TEST(ContainersFuzz, ArrayMatchesAReferenceModel) {
    Memory::GuardAllocator allocator;
    u8 scratch[512] = {};
    usize mismatches = 0;
    Test::FuzzOptions options;
    options.seed = 0xA77A;
    static_cast<void>(Test::Fuzz(Span<const Span<const u8>>(kCorpus), scratch, options,
                                 [&allocator, &mismatches](Span<const u8> input) {
                                     Array<u32> array(allocator);
                                     ArrayModel model;
                                     OperationReader reader(input);
                                     while (reader.HasMore()) {
                                         mismatches += ApplyArrayOperation(reader, array, model) ? 0 : 1;
                                     }
                                     mismatches += SameArray(array, model) ? 0 : 1;
                                 }));
    NX_CHECK(mismatches == 0);
}

NX_TEST(ContainersFuzz, HashMapMatchesAReferenceModel) {
    Memory::GuardAllocator allocator;
    u8 scratch[512] = {};
    usize mismatches = 0;
    Test::FuzzOptions options;
    options.seed = 0x4A54;
    static_cast<void>(Test::Fuzz(Span<const Span<const u8>>(kCorpus), scratch, options,
                                 [&allocator, &mismatches](Span<const u8> input) {
                                     HashMap<u32, u32> map(allocator);
                                     MapModel model;
                                     OperationReader reader(input);
                                     while (reader.HasMore()) {
                                         mismatches += ApplyMapOperation(reader, map, model) ? 0 : 1;
                                     }
                                     mismatches += SameMap(map, model) ? 0 : 1;
                                 }));
    NX_CHECK(mismatches == 0);
}

NX_TEST(ContainersFuzz, StringMatchesAReferenceModel) {
    Memory::GuardAllocator allocator;
    u8 scratch[512] = {};
    usize mismatches = 0;
    Test::FuzzOptions options;
    options.seed = 0x5781;
    static_cast<void>(Test::Fuzz(Span<const Span<const u8>>(kCorpus), scratch, options,
                                 [&allocator, &mismatches](Span<const u8> input) {
                                     String string(allocator);
                                     TextModel model;
                                     OperationReader reader(input);
                                     while (reader.HasMore()) {
                                         mismatches += ApplyStringOperation(reader, string, model) ? 0 : 1;
                                     }
                                     mismatches += string.View() == StringView(model.text, model.size) ? 0 : 1;
                                 }));
    NX_CHECK(mismatches == 0);
}
