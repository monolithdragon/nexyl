#pragma once

// Reflected test fixtures: the build generates their table, kSerializationTestsTypeTable, into the test executable.

#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/String.h>

namespace Nexyl::SerializationTest {

/// A kind of machine.
enum class Kind : u16 {
    /// A belt.
    Belt,
    /// A robot.
    Robot = 300,
};

/// A point in space.
struct Point {
    /// Forward.
    f32 x = 0.0F;
    /// Left.
    f32 y = 0.0F;
    /// Up.
    f32 z = 0.0F;
};

/// A world placement.
struct Placement {
    /// The world position.
    f64 position[3] = {};
    /// The facing direction.
    Point direction;
};

/// One waypoint.
struct Waypoint {
    /// Where.
    Point position;
    /// How long to stay, in seconds.
    f32 dwell = 0.0F;
};

/// A record with a field of every built-in kind and private state.
class Machine final {
public:
    /// The kind.
    Kind kind = Kind::Belt;
    /// Whether it runs.
    bool running = false;
    /// A letter.
    char code = 'a';
    /// A signed counter.
    i64 balance = 0;
    /// A small number.
    u8 level = 0;
    /// A precise number.
    f64 precise = 0.0;
    /// Where it stands.
    Placement placement;
    /// A fixed grid.
    i16 grid[2][3] = {};
    /// Its name.
    Containers::String name;
    /// Its route.
    Containers::Array<Waypoint> route;
    /// Labels by number.
    Containers::HashMap<u32, Containers::String> labels;
    /// Nested lists.
    Containers::Array<Containers::Array<u32>> batches;
    /// Another machine.
    Containers::Handle<Machine> next;
    /// Rebuilt on load.
    /// @transient
    u32 cache = 0;

    /// Sets the private counter.
    void SetTicks(u64 ticks) {
        m_ticks = ticks;
    }

    /// Returns the private counter.
    [[nodiscard]] u64 GetTicks() const {
        return m_ticks;
    }

private:
    u64 m_ticks = 0;
};

/// The first version of a conveyor, as an archive written earlier holds it.
struct OldConveyor {
    /// Speed.
    f32 speed = 0.0F;
    /// A field the new version dropped.
    u32 legacy = 0;
    /// Items on the belt.
    u32 count = 0;
    /// A level that the new version stores as f32.
    u32 level = 0;
};

/// The current version of the conveyor: renamed, with a renamed, a dropped, a retyped and a new field.
/// @renamed-from Nexyl::SerializationTest::OldConveyor
/// @version 2
struct NewConveyor {
    /// Belt speed.
    /// @renamed-from speed
    f32 beltSpeed = 0.0F;
    /// Items on the belt.
    u32 count = 0;
    /// The level, now fractional; the old u32 value cannot be read into it.
    f32 level = 0.5F;
    /// Added in version 2; keeps its value when an old archive is read.
    u32 added = 7;
};

/// A single bool, to place one at a known position in an archive.
struct Flag {
    /// The flag.
    bool value = false;
};

} // namespace Nexyl::SerializationTest
