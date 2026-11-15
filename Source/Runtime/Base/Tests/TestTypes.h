#pragma once

#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl::Tests {

/// Counts live objects, to prove that wrapper types neither leak nor double-destroy their contents.
struct Tracked {
    /// Number of Tracked objects currently alive.
    static inline i32 s_alive = 0;

    i32 value = 0;

    explicit Tracked(i32 initial) : value(initial) {
        ++s_alive;
    }
    Tracked(const Tracked& other) : value(other.value) {
        ++s_alive;
    }
    Tracked(Tracked&& other) noexcept : value(other.value) {
        other.value = -1;
        ++s_alive;
    }
    Tracked& operator=(const Tracked& other) = default;
    Tracked& operator=(Tracked&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Tracked() {
        --s_alive;
    }
};

/// A type that can be moved but not copied.
struct MoveOnly {
    i32 value = 0;

    explicit MoveOnly(i32 initial) : value(initial) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : value(Exchange(other.value, 0)) {}
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        value = Exchange(other.value, 0);
        return *this;
    }
    ~MoveOnly() = default;
};

} // namespace Nexyl::Tests
