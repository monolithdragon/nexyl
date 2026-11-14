#pragma once

// Helpers shared by the container tests.

#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl::Containers::Tests {

/// Counts live objects, copies and moves, to prove that containers neither leak nor destroy twice.
struct Counted {
    static inline i32 s_alive = 0;

    i32 value = 0;

    explicit Counted(i32 initial = 0) : value(initial) {
        ++s_alive;
    }
    Counted(const Counted& other) : value(other.value) {
        ++s_alive;
    }
    Counted(Counted&& other) noexcept : value(Exchange(other.value, -1)) {
        ++s_alive;
    }
    Counted& operator=(const Counted& other) = default;
    Counted& operator=(Counted&& other) noexcept {
        value = Exchange(other.value, -1);
        return *this;
    }
    ~Counted() {
        --s_alive;
    }

    bool operator==(const Counted& other) const {
        return value == other.value;
    }
    bool operator<(const Counted& other) const {
        return value < other.value;
    }
};

/// A small deterministic random generator for tests (xorshift64*), with a fixed seed (rule TEST-4).
class TestRandom final {
public:
    explicit TestRandom(u64 seed) : m_state(seed != 0 ? seed : 1) {}

    u64 Next() {
        m_state ^= m_state >> 12U;
        m_state ^= m_state << 25U;
        m_state ^= m_state >> 27U;
        return m_state * u64{0x2545F4914F6CDD1D};
    }

    /// A value in [0, bound).
    u32 Below(u32 bound) {
        return static_cast<u32>(Next() % bound);
    }

private:
    u64 m_state = 1;
};

} // namespace Nexyl::Containers::Tests
