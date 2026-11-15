#pragma once

// A Machine with a value in every field, shared by the Serialization tests.

#include "SerializationTestTypes.h"

#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Array.h>
#include <Nexyl/Containers/Handle.h>
#include <Nexyl/Containers/HashMap.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Memory/IAllocator.h>

namespace Nexyl::SerializationTest {

/// Gives every field of a machine a value that differs from its default.
inline void FillMachine(Machine& machine, Memory::IAllocator& allocator) {
    machine.kind = Kind::Robot;
    machine.running = true;
    machine.code = 'z';
    machine.balance = -1234567890123LL;
    machine.level = 200;
    machine.precise = 0.1;
    machine.placement.position[2] = 1e300;
    machine.placement.direction = Point{0.0F, 0.0F, 1.0F};
    machine.grid[1][2] = -7;
    machine.name = Containers::String(allocator, "Unit 7");
    machine.route = Containers::Array<Waypoint>(allocator);
    machine.route.Push(Waypoint{Point{1.0F, 2.0F, 3.0F}, 0.5F});
    machine.route.Push(Waypoint{Point{4.0F, 5.0F, 6.0F}, 1.5F});
    machine.labels = Containers::HashMap<u32, Containers::String>(allocator);
    machine.labels.Set(10, Containers::String(allocator, "ten"));
    machine.labels.Set(3, Containers::String(allocator, "three"));
    machine.batches = Containers::Array<Containers::Array<u32>>(allocator);
    machine.batches.Push(Containers::Array<u32>(allocator, {1, 2, 3}));
    machine.batches.Push(Containers::Array<u32>(allocator));
    machine.next = Containers::Handle<Machine>{4, 9};
    machine.cache = 99;
    machine.SetTicks(0xFFFF'FFFF'FFFFULL);
}

} // namespace Nexyl::SerializationTest
