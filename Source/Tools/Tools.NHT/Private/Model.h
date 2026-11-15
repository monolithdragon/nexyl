#pragma once

// The type model the header tool extracts from a module's public headers and turns into reflection tables
// (docs/NEXYL_ARCHITECTURE.md, sections 8.3 to 8.7). A build-time tool: it uses the standard library (STL-5).

#include <cstdint>
#include <string>
#include <vector>

namespace Nexyl::Nht {

// Metadata from a documentation comment (section 8.5).
struct Tags {
    std::string description;
    std::string units;
    std::string category;
    std::string renamedFrom;
    bool opaque = false;
    bool transient = false;
    bool presentation = false;
    bool readOnly = false;
    bool hidden = false;
    bool noScript = false;
    bool hasRange = false;
    double rangeMinimum = 0.0;
    double rangeMaximum = 0.0;
    double step = 0.0;
    std::uint32_t version = 1;
};

struct FieldModel {
    std::string name;
    // The canonical C++ spelling of the field's type, for pointers to private members.
    std::string typeSpelling;
    std::uint64_t typeId = 0;
    std::uint64_t offset = 0;
    bool isPublic = false;
    Tags tags;
};

struct EnumValueModel {
    std::string name;
    std::int64_t value = 0;
    Tags tags;
};

enum class TypeShape : std::uint8_t {
    Record,
    Opaque,
    Enum,
};

struct TypeModel {
    std::string name;
    std::uint64_t id = 0;
    TypeShape shape = TypeShape::Record;
    Tags tags;
    std::uint64_t size = 0;
    std::uint64_t alignment = 0;
    std::uint64_t baseId = 0;
    // Offsets may be checked with offsetof: no base, no virtual function, every field with the same access.
    bool checkOffsets = false;
    std::vector<FieldModel> fields;
    std::vector<EnumValueModel> values;
    std::uint64_t underlyingId = 0;
};

// A built-in instantiation that a field uses, such as Array<Vec3>, spelled as C++.
struct IntrinsicUse {
    std::string spelling;
    std::uint64_t id = 0;
};

struct ModuleModel {
    std::string module;
    std::vector<std::string> headers;
    std::vector<TypeModel> types;
    std::vector<IntrinsicUse> intrinsics;
    std::vector<std::string> dependencies;
};

} // namespace Nexyl::Nht
