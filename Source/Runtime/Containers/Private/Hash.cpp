#include <Nexyl/Containers/Hash.h>

#include <Nexyl/Base/Assert.h>

// The byte hash follows wyhash, version 4.2, by Wang Yi (public domain, The Unlicense), written anew for the
// engine. Reads are assembled byte by byte in little-endian order, so the result is the same on every platform;
// compilers turn them into single loads.

namespace Nexyl::Containers {

namespace {

using Detail::HashMix;
using Detail::kHashSecret0;
using Detail::kHashSecret1;
using Detail::kHashSecret2;
using Detail::kHashSecret3;

u64 Read64(const u8* bytes) {
    u64 value = 0;
    for (u32 index = 0; index < 8; ++index) {
        value |= static_cast<u64>(bytes[index]) << (8U * index);
    }
    return value;
}

u64 Read32(const u8* bytes) {
    u64 value = 0;
    for (u32 index = 0; index < 4; ++index) {
        value |= static_cast<u64>(bytes[index]) << (8U * index);
    }
    return value;
}

// One to three bytes, spread over 24 bits.
u64 Read1To3(const u8* bytes, usize size) {
    return (static_cast<u64>(bytes[0]) << 16U) | (static_cast<u64>(bytes[size >> 1U]) << 8U) | bytes[size - 1];
}

} // namespace

u64 HashBytes(const void* data, usize size, u64 seed) {
    NX_ASSERT(data != nullptr || size == 0, "HashBytes needs data when the size is not zero.");
    const u8* bytes = static_cast<const u8*>(data);
    seed ^= HashMix(seed ^ kHashSecret0, kHashSecret1);

    u64 first = 0;
    u64 second = 0;
    if (size <= 16) {
        if (size >= 4) {
            const usize offset = (size >> 3U) << 2U;
            first = (Read32(bytes) << 32U) | Read32(bytes + offset);
            second = (Read32(bytes + size - 4) << 32U) | Read32(bytes + size - 4 - offset);
        } else if (size > 0) {
            first = Read1To3(bytes, size);
        }
    } else {
        usize remaining = size;
        if (remaining >= 48) {
            u64 seed1 = seed;
            u64 seed2 = seed;
            do {
                seed = HashMix(Read64(bytes) ^ kHashSecret1, Read64(bytes + 8) ^ seed);
                seed1 = HashMix(Read64(bytes + 16) ^ kHashSecret2, Read64(bytes + 24) ^ seed1);
                seed2 = HashMix(Read64(bytes + 32) ^ kHashSecret3, Read64(bytes + 40) ^ seed2);
                bytes += 48;
                remaining -= 48;
            } while (remaining >= 48);
            seed ^= seed1 ^ seed2;
        }
        while (remaining > 16) {
            seed = HashMix(Read64(bytes) ^ kHashSecret1, Read64(bytes + 8) ^ seed);
            bytes += 16;
            remaining -= 16;
        }
        first = Read64(bytes + remaining - 16);
        second = Read64(bytes + remaining - 8);
    }

    first ^= kHashSecret1;
    second ^= seed;
    const u64 low = first * second;
    const u64 high = MultiplyHigh(first, second);
    return HashMix(low ^ kHashSecret0 ^ static_cast<u64>(size), high ^ kHashSecret1);
}

} // namespace Nexyl::Containers
