#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Serialization {

/// Errors of the Serialization module. Readers return them for every kind of malformed input (rule ERR-7).
/// @since 0.1
enum class SerializationError : u16 {
    /// The data is not an archive of this kind, or its structure is broken.
    InvalidFormat = 1,
    /// The archive was written by a newer format version.
    UnsupportedVersion,
    /// The data ends before the archive does.
    Truncated,
    /// A count, a length or the nesting depth exceeds what the data or the reader's limits allow.
    LimitExceeded,
    /// A value is out of its domain, for example a bool that is neither 0 nor 1.
    InvalidValue,
    /// The archive holds another type than the one requested.
    TypeMismatch,
    /// A type the archive or the object needs is not in the TypeRegistry.
    UnknownType,
    /// The type cannot be serialized: it is opaque, or a record with a base class (not supported yet).
    NotSerializable,
};

/// Associates SerializationError with the Serialization module (required by the ErrorEnum concept).
/// @return ModuleId::Serialization.
/// @threadsafety safe
/// @since 0.1
[[nodiscard]] constexpr ModuleId GetErrorModule(SerializationError /*code*/) noexcept {
    return ModuleId::Serialization;
}

} // namespace Nexyl::Serialization
