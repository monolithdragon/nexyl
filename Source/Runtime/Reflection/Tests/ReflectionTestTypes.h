#pragma once

// Reflected test fixtures: the build generates their table, kReflectionTestsTypeTable, into the test executable.

#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Math/Vec3.h>

namespace Nexyl::ReflectionTest {

/// An enumeration with tags.
/// @version 3
enum class Mode : u8 {
    /// Off.
    Off,
    /// Fast.
    /// @renamed-from Quick
    Fast,
    /// Not shown.
    /// @hidden
    Secret,
};

/// A record with private fields, whose offsets only the generated layout check can confirm.
class Counter final {
public:
    /// Sets every field.
    void Set(u8 flag, u32 count, f64 weight, Math::Vec3 direction) {
        m_flag = flag;
        m_count = count;
        m_weight = weight;
        m_directions[1] = direction;
    }

    /// Sums the fields, so that each is used.
    [[nodiscard]] f64 Sum() const {
        return static_cast<f64>(m_flag) + static_cast<f64>(m_count) + m_weight + static_cast<f64>(m_directions[1].x);
    }

private:
    u8 m_flag = 0;
    u32 m_count = 0;
    f64 m_weight = 0.0;
    Math::Vec3 m_directions[2] = {};
};

/// A record with a field of every built-in kind.
/// @category Tests
/// @version 2
struct Everything {
    /// Speed of the thing.
    /// @units m/s
    /// @range 0..5
    /// @step 0.5
    f32 speed = 1.0F;
    /// Whether it runs.
    bool enabled = true;
    /// A letter.
    char letter = 'a';
    /// A large number.
    i64 big = 0;
    /// The mode.
    Mode mode = Mode::Off;
    /// A position.
    Math::Vec3 position;
    /// A small matrix.
    f32 matrix[2][3] = {};
    /// A name.
    Containers::String name;
    /// A path.
    Containers::Array<Math::Vec3> path;
    /// Labels by number.
    Containers::HashMap<u32, Containers::String> labels;
    /// Another counter.
    Containers::Handle<Counter> counter;
    /// A counter held by value.
    Counter nested;
    /// A cache, rebuilt on load; left out of the table.
    /// @transient
    void* cache = nullptr;
    /// A visual effect.
    /// @presentation
    /// @readonly
    f32 glow = 0.0F;
};

} // namespace Nexyl::ReflectionTest
