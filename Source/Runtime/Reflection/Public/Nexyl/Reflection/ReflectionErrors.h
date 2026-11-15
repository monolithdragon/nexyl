#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Reflection {

/// Errors of the Reflection module.
/// @since 0.1
enum class ReflectionError : u16 {
    /// Two different type names hash to the same TypeId.
    TypeIdCollision = 1,
    /// A type is described twice with a different size, alignment or kind.
    TypeMismatch,
};

/// Associates ReflectionError with the Reflection module (required by the ErrorEnum concept).
/// @return ModuleId::Reflection.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr ModuleId GetErrorModule(ReflectionError /*code*/) noexcept {
    return ModuleId::Reflection;
}

} // namespace Nexyl::Reflection
