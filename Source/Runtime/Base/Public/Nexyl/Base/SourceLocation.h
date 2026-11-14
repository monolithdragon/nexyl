#pragma once

#include <Nexyl/Base/Std/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// A position in the source code: file, function and line.
/// @threadsafety safe
/// @since 0.1
struct SourceLocation {
    /// Path of the source file, as the compiler spells it.
    const char* file = "";
    /// Name of the enclosing function, as the compiler spells it.
    const char* function = "";
    /// One-based line number.
    u32 line = 0;

    /// Returns the location of the call site.
    ///
    /// Call it without arguments. The default argument is evaluated where the call appears,
    /// so the result names the caller, not this function.
    ///
    /// @param location Captured automatically; do not pass.
    /// @return The location of the expression that calls Current.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] static constexpr SourceLocation
    Current(const std::source_location& location = std::source_location::current()) noexcept {
        return SourceLocation{location.file_name(), location.function_name(), static_cast<u32>(location.line())};
    }
};

} // namespace Nexyl

/// The source location of the place where this macro is written.
/// @since 0.1
#define NX_SOURCE_LOCATION (::Nexyl::SourceLocation::Current())
