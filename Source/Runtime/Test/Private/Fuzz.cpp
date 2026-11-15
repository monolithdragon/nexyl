#include <Nexyl/Test/Fuzz.h>

#include <Nexyl/Base/Assert.h>

namespace Nexyl::Test {

namespace {

class Random final {
public:
    explicit Random(u64 seed) : m_state(seed) {}

    // SplitMix64: the same sequence on every platform.
    u64 Next() {
        m_state += 0x9E37'79B9'7F4A'7C15ULL;
        u64 z = m_state;
        z = (z ^ (z >> 30U)) * 0xBF58'476D'1CE4'E5B9ULL;
        z = (z ^ (z >> 27U)) * 0x94D0'49BB'1331'11EBULL;
        return z ^ (z >> 31U);
    }

    // A number below `bound`, which is at least 1.
    usize Below(usize bound) {
        return static_cast<usize>(Next() % bound);
    }

private:
    u64 m_state = 0;
};

constexpr u8 kInterestingBytes[] = {0x00, 0x01, 0x7F, 0x80, 0xFF, 0x20, 0x30, 0x39};
constexpr u64 kInterestingWords[] = {
    0,      1,           0x7F,        0x80,        0xFF,          0x7FFF,        0x8000,
    0xFFFF, 0x7FFF'FFFF, 0x8000'0000, 0xFFFF'FFFF, ~u64{0} >> 1U, u64{1} << 63U, ~u64{0}};

// An input under construction in the caller's scratch buffer.
class Input final {
public:
    explicit Input(Span<u8> scratch) : m_scratch(scratch) {}

    void Assign(Span<const u8> bytes) {
        m_size = 0;
        Insert(0, bytes);
    }

    [[nodiscard]] usize Size() const {
        return m_size;
    }

    [[nodiscard]] Span<const u8> View() const {
        return Span<const u8>(m_scratch.Data(), m_size);
    }

    u8& At(usize index) {
        return m_scratch[index];
    }

    // Inserts as many bytes as fit.
    void Insert(usize position, Span<const u8> bytes) {
        const usize room = m_scratch.Size() - m_size;
        const usize count = bytes.Size() < room ? bytes.Size() : room;
        for (usize index = m_size; index > position; --index) {
            m_scratch[index - 1 + count] = m_scratch[index - 1];
        }
        for (usize index = 0; index < count; ++index) {
            m_scratch[position + index] = bytes[index];
        }
        m_size += count;
    }

    void Erase(usize position, usize count) {
        for (usize index = position; index + count < m_size; ++index) {
            m_scratch[index] = m_scratch[index + count];
        }
        m_size -= count;
    }

private:
    Span<u8> m_scratch;
    usize m_size = 0;
};

void Mutate(Input& input, Random& random, Span<const Span<const u8>> corpus, const FuzzOptions& options) {
    const usize size = input.Size();
    switch (random.Below(9)) {
        case 0: // flip a bit
            if (size > 0) {
                input.At(random.Below(size)) ^= static_cast<u8>(1U << random.Below(8));
            }
            break;
        case 1: // replace a byte
            if (size > 0) {
                input.At(random.Below(size)) = static_cast<u8>(random.Next());
            }
            break;
        case 2: // an interesting byte
            if (size > 0) {
                input.At(random.Below(size)) = kInterestingBytes[random.Below(sizeof(kInterestingBytes))];
            }
            break;
        case 3: { // an interesting little-endian word of 2, 4 or 8 bytes
            const usize width = usize{2} << random.Below(3);
            if (size >= width) {
                const u64 word = kInterestingWords[random.Below(sizeof(kInterestingWords) / sizeof(u64))];
                const usize position = random.Below(size - width + 1);
                for (usize byte = 0; byte < width; ++byte) {
                    input.At(position + byte) = static_cast<u8>(word >> (byte * 8U));
                }
            }
            break;
        }
        case 4: { // insert random bytes
            u8 bytes[8] = {};
            const usize count = 1 + random.Below(sizeof(bytes));
            for (usize index = 0; index < count; ++index) {
                bytes[index] = static_cast<u8>(random.Next());
            }
            input.Insert(random.Below(size + 1), Span<const u8>(bytes, count));
            break;
        }
        case 5: // delete a range
            if (size > 0) {
                const usize position = random.Below(size);
                input.Erase(position, 1 + random.Below(size - position));
            }
            break;
        case 6: { // duplicate a short range
            if (size > 0) {
                u8 bytes[16] = {};
                const usize position = random.Below(size);
                const usize available = size - position;
                const usize count = 1 + random.Below(available < sizeof(bytes) ? available : sizeof(bytes));
                for (usize index = 0; index < count; ++index) {
                    bytes[index] = input.At(position + index);
                }
                input.Insert(random.Below(size + 1), Span<const u8>(bytes, count));
            }
            break;
        }
        case 7: { // splice: the head of this input, the tail of another entry
            const Span<const u8> other = corpus[random.Below(corpus.Size())];
            if (!other.IsEmpty()) {
                const usize keep = random.Below(size + 1);
                input.Erase(keep, size - keep);
                const usize from = random.Below(other.Size());
                input.Insert(keep, other.Subspan(from));
            }
            break;
        }
        default: { // a dictionary token, or random bytes without a dictionary
            if (!options.dictionary.IsEmpty()) {
                const Span<const char> token = options.dictionary[random.Below(options.dictionary.Size())];
                u8 bytes[64] = {};
                const usize count = token.Size() < sizeof(bytes) ? token.Size() : sizeof(bytes);
                for (usize index = 0; index < count; ++index) {
                    bytes[index] = static_cast<u8>(token[index]);
                }
                input.Insert(random.Below(size + 1), Span<const u8>(bytes, count));
            } else if (size > 0) {
                input.At(random.Below(size)) = static_cast<u8>(random.Next());
            }
            break;
        }
    }
}

} // namespace

u32 Fuzz(Span<const Span<const u8>> corpus, Span<u8> scratch, const FuzzOptions& options, FuzzTarget target,
         const void* context) {
    NX_ASSERT(!corpus.IsEmpty() && !scratch.IsEmpty() && target != nullptr, "Fuzz needs a corpus and a buffer.");
    u32 runs = 0;
    Input input(scratch);
    for (const Span<const u8> entry : corpus) {
        input.Assign(entry);
        target(context, input.View());
        ++runs;
    }
    Random random(options.seed);
    for (u32 iteration = 0; iteration < options.iterations; ++iteration) {
        input.Assign(corpus[random.Below(corpus.Size())]);
        const usize mutations = 1 + random.Below(4);
        for (usize mutation = 0; mutation < mutations; ++mutation) {
            Mutate(input, random, corpus, options);
        }
        target(context, input.View());
        ++runs;
    }
    return runs;
}

} // namespace Nexyl::Test
