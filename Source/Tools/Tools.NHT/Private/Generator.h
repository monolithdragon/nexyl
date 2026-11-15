#pragma once

#include "Model.h"

#include <string>
#include <vector>

namespace Nexyl::Nht {

// The C++ of a module's reflection table (docs/NEXYL_ARCHITECTURE.md, section 8.7): constant descriptions, the
// table object `Nexyl::Reflection::Generated::k<Module>TypeTable`, and static_asserts that let the compiler
// confirm the tool's identities, sizes, alignments, offsets and enumeration values.
std::string GenerateTable(const ModuleModel& model);

// The C++ of GetFoundationTypeTables(): the tables of the modules below Reflection, compiled into Reflection.
std::string GenerateAggregate(const std::vector<std::string>& modules);

// The name of a module's table object, for example kMathTypeTable.
std::string TableObjectName(const std::string& module);

} // namespace Nexyl::Nht
