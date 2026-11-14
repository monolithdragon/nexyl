#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// General-purpose error codes for conditions that do not belong to one module.
/// A module with a more specific meaning declares its own codes instead.
/// @since 0.1
enum class BaseError : u16 {
    /// An argument is outside the documented domain of the function.
    InvalidArgument = 1,
    /// An index or range lies outside the valid bounds.
    OutOfRange,
    /// The operation is not supported on this platform, device or configuration.
    Unsupported,
    /// The result does not fit in the destination type or buffer.
    Overflow,
};

/// Associates BaseError with the Base module (required by the ErrorEnum concept).
///
/// @return ModuleId::Base.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr ModuleId GetErrorModule(BaseError /*code*/) noexcept {
    return ModuleId::Base;
}

} // namespace Nexyl
