#pragma once

#include <Nexyl/Base/Types.h>
#include <Nexyl/Math/Quat.h>
#include <Nexyl/Math/Vec3.h>
#include <Nexyl/Math/Vec3d.h>

namespace Nexyl::Math {

/// The placement of an object in the world: an f64 position and an f32 rotation (docs/NEXYL_ARCHITECTURE.md,
/// section 1.1). The layout matches `NxPose` of the C ABI.
///
/// ```cpp
/// const Vec3d muzzle = TransformPoint(weaponPose, Vec3{0.6f, 0.0f, 0.1f});
/// ```
/// @threadsafety compatible @determinism deterministic @since 0.1
struct Pose {
    /// The world position of the origin.
    Vec3d position;
    /// The orientation, a unit quaternion.
    Quat rotation;

    /// Compares exactly. @since 0.1
    constexpr bool operator==(const Pose&) const = default;
};

static_assert(sizeof(Pose) == 40, "Pose is a Vec3d followed by a Quat, like NxPose.");

/// Moves a point from the pose's local frame into the world. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3d TransformPoint(const Pose& pose, const Vec3& local) {
    return Offset(pose.position, Rotate(pose.rotation, local));
}

/// Rotates a direction from the pose's local frame into the world. @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 TransformDirection(const Pose& pose, const Vec3& local) {
    return Rotate(pose.rotation, local);
}

/// Moves a world point into the pose's local frame, narrowing against the pose's own origin (section 7.5).
/// @determinism deterministic @since 0.1
[[nodiscard]] constexpr Vec3 InverseTransformPoint(const Pose& pose, const Vec3d& world) {
    return Rotate(Conjugate(pose.rotation), ToLocal(world, pose.position));
}

} // namespace Nexyl::Math
