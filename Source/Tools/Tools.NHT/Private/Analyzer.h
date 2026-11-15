#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace Nexyl::Nht {

// Parses a module's public headers as one translation unit and fills `model` with the types in reflection scope
// (docs/NEXYL_ARCHITECTURE.md, section 8.3). `arguments` are the compiler arguments of the module (include
// directories and definitions). Errors are appended to `errors` as "file:line:column: error: text".
bool AnalyzeModule(const std::vector<std::string>& arguments, ModuleModel& model, std::vector<std::string>& errors);

// FNV-1a 64 of a canonical type name, and its continuation over 8 little-endian bytes (see Reflection's TypeId).
std::uint64_t HashTypeName(const std::string& name);
std::uint64_t ContinueTypeHash(std::uint64_t hash, std::uint64_t value);

} // namespace Nexyl::Nht
