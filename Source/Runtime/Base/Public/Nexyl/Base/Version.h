#pragma once

#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// Semantic version number.
/// @since 0.1
struct Version {
    /// Incremented when public API breaks.
    u16 major = 0;
    /// Incremented when functionality is added compatibly.
    u16 minor = 0;
    /// Incremented for compatible fixes.
    u16 patch = 0;
};

/// Returns the version of the engine this module was built from.
///
/// @return The engine version.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] NX_BASE_API Version GetEngineVersion();

} // namespace Nexyl
