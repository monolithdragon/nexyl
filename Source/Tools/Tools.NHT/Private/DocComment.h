#pragma once

#include "Model.h"

#include <string>

namespace Nexyl::Nht {

// What a documentation comment is attached to; it decides which reflection tags are allowed.
enum class TagTarget : std::uint8_t {
    Type,
    Field,
    EnumValue,
};

// Parses a raw `///` comment into its summary and tags. Documentation tags (@param, @since and the others) are
// accepted and ignored. Returns false and sets `error` for an unknown, misplaced or malformed tag.
bool ParseDocComment(const std::string& raw, TagTarget target, Tags& tags, std::string& error);

} // namespace Nexyl::Nht
