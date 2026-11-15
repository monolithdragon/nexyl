#pragma once

#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Types.h>

// Shared between Format.cpp and FormatFloat.cpp; not part of the public API.

namespace Nexyl::Detail {

// Writes a number made of a prefix (sign, `0x`) and a body, honouring width, alignment and, when `allowZeroPad`,
// zero padding between the prefix and the body.
void WriteNumber(FormatWriter& writer, const FormatSpec& spec, const char* prefix, usize prefixSize, const char* body,
                 usize bodySize, bool allowZeroPad = true);

// Formats an f32 (`size` 4, `bits` holds its 32 bits) or an f64 (`size` 8).
void FormatFloatingPoint(FormatWriter& writer, const FormatSpec& spec, u64 bits, usize size);

} // namespace Nexyl::Detail
