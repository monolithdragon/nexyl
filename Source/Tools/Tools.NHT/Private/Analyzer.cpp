#include "Analyzer.h"

#include "DocComment.h"

#include <clang-c/Index.h>

#include <cstdint>
#include <string>
#include <vector>

namespace Nexyl::Nht {

namespace {

constexpr std::uint64_t kTypeHashBasis = 0xCBF2'9CE4'8422'2325ULL;
constexpr std::uint64_t kTypeHashPrime = 0x100'0000'01B3ULL;
// The name of the in-memory translation unit that includes every header of the module.
constexpr const char* kMainFile = "nht_module_headers.cpp";

std::string Take(CXString text) {
    const char* const characters = clang_getCString(text);
    std::string result = characters != nullptr ? characters : "";
    clang_disposeString(text);
    return result;
}

std::string Spelling(CXCursor cursor) {
    return Take(clang_getCursorSpelling(cursor));
}

bool IsRecordCursor(CXCursorKind kind) {
    return kind == CXCursor_StructDecl || kind == CXCursor_ClassDecl;
}

class Analyzer final {
public:
    Analyzer(CXTranslationUnit unit, ModuleModel& model, std::vector<std::string>& errors)
        : m_unit(unit), m_model(model), m_errors(errors) {
        for (const std::string& header : model.headers) {
            CXFile file = clang_getFile(unit, header.c_str());
            if (file != nullptr) {
                m_files.push_back(file);
            }
        }
    }

    void Run() {
        VisitScope(clang_getTranslationUnitCursor(m_unit));
    }

private:
    void Report(CXCursor cursor, const std::string& message) {
        CXFile file = nullptr;
        unsigned line = 0;
        unsigned column = 0;
        clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, &line, &column, nullptr);
        const std::string path = file != nullptr ? Take(clang_getFileName(file)) : std::string("<unknown>");
        m_errors.push_back(path + ":" + std::to_string(line) + ":" + std::to_string(column) + ": error: " + message);
    }

    bool InModule(CXCursor cursor) const {
        CXFile file = nullptr;
        clang_getSpellingLocation(clang_getCursorLocation(cursor), &file, nullptr, nullptr, nullptr);
        if (file == nullptr) {
            return false;
        }
        for (CXFile header : m_files) {
            if (clang_File_isEqual(file, header) != 0) {
                return true;
            }
        }
        return false;
    }

    static std::vector<CXCursor> Children(CXCursor cursor) {
        std::vector<CXCursor> children;
        clang_visitChildren(
            cursor,
            [](CXCursor child, CXCursor /*parent*/, CXClientData data) {
                static_cast<std::vector<CXCursor>*>(data)->push_back(child);
                return CXChildVisit_Continue;
            },
            &children);
        return children;
    }

    void VisitScope(CXCursor scope) {
        for (const CXCursor child : Children(scope)) {
            const CXCursorKind kind = clang_getCursorKind(child);
            if (kind == CXCursor_Namespace || kind == CXCursor_LinkageSpec) {
                if (Spelling(child) != "Detail") {
                    VisitScope(child);
                }
                continue;
            }
            VisitDeclaration(child);
        }
    }

    // A record, enumeration or union declared at namespace scope or as a public member of a reflected record.
    void VisitDeclaration(CXCursor cursor) {
        const CXCursorKind kind = clang_getCursorKind(cursor);
        if (!IsRecordCursor(kind) && kind != CXCursor_EnumDecl && kind != CXCursor_UnionDecl) {
            return;
        }
        if (clang_isCursorDefinition(cursor) == 0 || !InModule(cursor) || clang_Cursor_isAnonymous(cursor) != 0) {
            return;
        }
        // Explicit specializations of templates are not in scope (section 8.3).
        if (clang_Cursor_getNumTemplateArguments(cursor) >= 0) {
            return;
        }
        if (kind == CXCursor_EnumDecl) {
            VisitEnum(cursor);
        } else {
            VisitRecord(cursor);
        }
    }

    bool QualifiedName(CXCursor cursor, std::string& name) {
        std::vector<std::string> parts;
        for (CXCursor current = cursor; clang_getCursorKind(current) != CXCursor_TranslationUnit;
             current = clang_getCursorSemanticParent(current)) {
            const CXCursorKind kind = clang_getCursorKind(current);
            if (kind == CXCursor_LinkageSpec) {
                continue;
            }
            if (clang_Cursor_isNull(current) != 0 || clang_isInvalid(kind) != 0) {
                return false;
            }
            const std::string part = Spelling(current);
            if (part.empty() || clang_Cursor_isAnonymous(current) != 0) {
                return false;
            }
            parts.push_back(part);
        }
        name.clear();
        for (auto part = parts.rbegin(); part != parts.rend(); ++part) {
            if (!name.empty()) {
                name += "::";
            }
            name += *part;
        }
        return true;
    }

    // The TypeId of a type, by the canonical form documented with Reflection's TypeId.
    bool TypeIdOf(CXType type, std::uint64_t& id) {
        type = clang_getUnqualifiedType(clang_getCanonicalType(type));
        const long long size = clang_Type_getSizeOf(type);
        switch (type.kind) {
            case CXType_Bool:
                id = HashTypeName("bool");
                return true;
            case CXType_Char_S:
            case CXType_Char_U:
                id = HashTypeName("char");
                return true;
            case CXType_SChar:
            case CXType_Short:
            case CXType_Int:
            case CXType_Long:
            case CXType_LongLong:
                id = HashTypeName("i" + std::to_string(size * 8));
                return true;
            case CXType_UChar:
            case CXType_UShort:
            case CXType_UInt:
            case CXType_ULong:
            case CXType_ULongLong:
            case CXType_Char16:
            case CXType_Char32:
                id = HashTypeName("u" + std::to_string(size * 8));
                return true;
            case CXType_Float:
                id = HashTypeName("f32");
                return true;
            case CXType_Double:
                id = HashTypeName("f64");
                return true;
            case CXType_ConstantArray: {
                std::uint64_t element = 0;
                if (!TypeIdOf(clang_getArrayElementType(type), element)) {
                    return false;
                }
                id = ContinueTypeHash(ContinueTypeHash(HashTypeName("[]"), element),
                                      static_cast<std::uint64_t>(clang_getArraySize(type)));
                return true;
            }
            case CXType_Enum:
            case CXType_Record: {
                const CXCursor declaration = clang_getTypeDeclaration(type);
                const int argumentCount = clang_Type_getNumTemplateArguments(type);
                std::string name;
                if (argumentCount < 0) {
                    if (!QualifiedName(declaration, name)) {
                        return false;
                    }
                    id = HashTypeName(name);
                    return true;
                }
                if (!QualifiedName(clang_getSpecializedCursorTemplate(declaration), name)) {
                    return false;
                }
                std::uint64_t hash = HashTypeName(name);
                for (int index = 0; index < argumentCount; ++index) {
                    const CXType argument = clang_Type_getTemplateArgumentAsType(type, static_cast<unsigned>(index));
                    std::uint64_t argumentId = 0;
                    if (argument.kind == CXType_Invalid || !TypeIdOf(argument, argumentId)) {
                        return false;
                    }
                    hash = ContinueTypeHash(hash, argumentId);
                }
                id = hash;
                return true;
            }
            default:
                return false;
        }
    }

    // Whether reflection can store a field of this type; records the built-in instantiations it needs.
    bool IsStorable(CXType type, std::string& why) {
        type = clang_getUnqualifiedType(clang_getCanonicalType(type));
        switch (type.kind) {
            case CXType_Bool:
            case CXType_Char_S:
            case CXType_Char_U:
            case CXType_SChar:
            case CXType_UChar:
            case CXType_Short:
            case CXType_UShort:
            case CXType_Int:
            case CXType_UInt:
            case CXType_Long:
            case CXType_ULong:
            case CXType_LongLong:
            case CXType_ULongLong:
            case CXType_Char16:
            case CXType_Char32:
            case CXType_Float:
            case CXType_Double:
            case CXType_Enum:
                return true;
            case CXType_ConstantArray:
                if (!IsStorable(clang_getArrayElementType(type), why)) {
                    return false;
                }
                return UseIntrinsic(type);
            case CXType_Record: {
                const CXCursor declaration = clang_getTypeDeclaration(type);
                if (clang_Cursor_isAnonymous(declaration) != 0) {
                    why = "an anonymous struct or union";
                    return false;
                }
                const int argumentCount = clang_Type_getNumTemplateArguments(type);
                std::string name;
                if (argumentCount < 0) {
                    if (QualifiedName(declaration, name) && name == "Nexyl::Containers::StringView") {
                        why = "a view, which does not own its text";
                        return false;
                    }
                    return true;
                }
                if (!QualifiedName(clang_getSpecializedCursorTemplate(declaration), name)) {
                    why = "a template that cannot be named";
                    return false;
                }
                if (name == "Nexyl::Containers::Handle") {
                    return UseIntrinsic(type);
                }
                if (name != "Nexyl::Containers::Array" && name != "Nexyl::Containers::HashMap") {
                    why = "an instantiation of '" + name +
                          "', which is not a built-in kind (String, Array, HashMap, Handle)";
                    return false;
                }
                for (int index = 0; index < argumentCount; ++index) {
                    if (!IsStorable(clang_Type_getTemplateArgumentAsType(type, static_cast<unsigned>(index)), why)) {
                        return false;
                    }
                }
                return UseIntrinsic(type);
            }
            case CXType_Pointer:
            case CXType_MemberPointer:
            case CXType_BlockPointer:
                why = "a pointer (rule REFL-4)";
                return false;
            case CXType_LValueReference:
            case CXType_RValueReference:
                why = "a reference";
                return false;
            default:
                why = "of a kind reflection does not store";
                return false;
        }
    }

    bool UseIntrinsic(CXType type) {
        std::uint64_t id = 0;
        if (!TypeIdOf(type, id)) {
            return false;
        }
        for (const IntrinsicUse& use : m_model.intrinsics) {
            if (use.id == id) {
                return true;
            }
        }
        m_model.intrinsics.push_back(IntrinsicUse{Take(clang_getTypeSpelling(type)), id});
        return true;
    }

    bool ParseTags(CXCursor cursor, TagTarget target, Tags& tags) {
        std::string error;
        if (!ParseDocComment(Take(clang_Cursor_getRawCommentText(cursor)), target, tags, error)) {
            Report(cursor, error);
            return false;
        }
        return true;
    }

    void VisitRecord(CXCursor cursor) {
        TypeModel type;
        if (!QualifiedName(cursor, type.name)) {
            Report(cursor, "a type in reflection scope needs a name outside anonymous namespaces");
            return;
        }
        type.id = HashTypeName(type.name);
        if (!ParseTags(cursor, TagTarget::Type, type.tags)) {
            return;
        }
        const CXType recordType = clang_getCursorType(cursor);
        type.size = static_cast<std::uint64_t>(clang_Type_getSizeOf(recordType));
        type.alignment = static_cast<std::uint64_t>(clang_Type_getAlignOf(recordType));

        const std::vector<CXCursor> children = Children(cursor);
        const bool isUnion = clang_getCursorKind(cursor) == CXCursor_UnionDecl;
        if (type.tags.opaque || clang_CXXRecord_isAbstract(cursor) != 0) {
            type.shape = TypeShape::Opaque;
        } else if (isUnion) {
            Report(cursor,
                   "the union '" + type.name + "' cannot be reflected; mark it '@opaque' or move it into Detail");
            return;
        }

        if (type.shape == TypeShape::Record) {
            BuildRecord(cursor, children, type);
        }
        m_model.types.push_back(type);

        // Public nested types are in scope too.
        for (const CXCursor child : children) {
            if (clang_getCXXAccessSpecifier(child) == CX_CXXPublic) {
                VisitDeclaration(child);
            }
        }
    }

    void BuildRecord(CXCursor cursor, const std::vector<CXCursor>& children, TypeModel& type) {
        int baseCount = 0;
        bool hasVirtual = false;
        bool anyPublic = false;
        bool anyNonPublic = false;
        for (const CXCursor child : children) {
            const CXCursorKind kind = clang_getCursorKind(child);
            if (kind == CXCursor_CXXBaseSpecifier) {
                ++baseCount;
                if (baseCount > 1) {
                    Report(child, "'" + type.name + "' has more than one base class (rule REFL-3)");
                    continue;
                }
                if (!TypeIdOf(clang_getCursorType(child), type.baseId)) {
                    Report(child, "the base class of '" + type.name + "' has no TypeId");
                }
            } else if (kind == CXCursor_CXXMethod && clang_CXXMethod_isVirtual(child) != 0) {
                hasVirtual = true;
            } else if (kind == CXCursor_FieldDecl) {
                FieldModel field;
                if (!BuildField(type, child, field)) {
                    continue;
                }
                anyPublic = anyPublic || field.isPublic;
                anyNonPublic = anyNonPublic || !field.isPublic;
                type.fields.push_back(field);
            }
        }
        static_cast<void>(cursor);
        type.checkOffsets = baseCount == 0 && !hasVirtual && !(anyPublic && anyNonPublic);
    }

    // Returns false for a field that is not reflected; errors are reported.
    bool BuildField(const TypeModel& owner, CXCursor cursor, FieldModel& field) {
        field.name = Spelling(cursor);
        field.isPublic = clang_getCXXAccessSpecifier(cursor) == CX_CXXPublic;
        if (!ParseTags(cursor, TagTarget::Field, field.tags)) {
            return false;
        }
        const std::string where = "'" + owner.name + "::" + field.name + "'";
        if (clang_Cursor_isBitField(cursor) != 0) {
            Report(cursor, where + " is a bit-field (rule REFL-2)");
            return false;
        }
        const CXType type = clang_getCursorType(cursor);
        const CXType canonical = clang_getCanonicalType(type);
        if (canonical.kind == CXType_LValueReference || canonical.kind == CXType_RValueReference) {
            Report(cursor, where + " is a reference (rule REFL-2)");
            return false;
        }
        if (field.name.empty()) {
            Report(cursor, "'" + owner.name + "' has an anonymous member (rule REFL-2)");
            return false;
        }
        std::string why;
        if (!IsStorable(type, why)) {
            if (field.tags.transient) {
                return false; // A transient field of a type reflection cannot store is left out.
            }
            Report(cursor, where + " has the type '" + Take(clang_getTypeSpelling(type)) + "', which is " + why +
                               "; mark the field '@transient' or the type '@opaque'");
            return false;
        }
        if (!TypeIdOf(type, field.typeId)) {
            Report(cursor, where + " has a type without a TypeId");
            return false;
        }
        const long long offset = clang_Cursor_getOffsetOfField(cursor);
        if (offset < 0 || offset % 8 != 0) {
            Report(cursor, where + " has no byte offset");
            return false;
        }
        field.offset = static_cast<std::uint64_t>(offset / 8);
        field.typeSpelling = Take(clang_getTypeSpelling(clang_getCanonicalType(type)));
        return true;
    }

    void VisitEnum(CXCursor cursor) {
        TypeModel type;
        type.shape = TypeShape::Enum;
        if (!QualifiedName(cursor, type.name)) {
            Report(cursor, "an enumeration in reflection scope needs a name outside anonymous namespaces");
            return;
        }
        type.id = HashTypeName(type.name);
        if (!ParseTags(cursor, TagTarget::Type, type.tags)) {
            return;
        }
        if (type.tags.opaque) {
            Report(cursor, "'@opaque' applies to classes, not to the enumeration '" + type.name + "'");
            return;
        }
        const CXType enumType = clang_getCursorType(cursor);
        type.size = static_cast<std::uint64_t>(clang_Type_getSizeOf(enumType));
        type.alignment = static_cast<std::uint64_t>(clang_Type_getAlignOf(enumType));
        const CXType underlying = clang_getCanonicalType(clang_getEnumDeclIntegerType(cursor));
        if (!TypeIdOf(underlying, type.underlyingId)) {
            Report(cursor, "the underlying type of '" + type.name + "' has no TypeId");
            return;
        }
        const bool isUnsigned = underlying.kind == CXType_UChar || underlying.kind == CXType_UShort ||
                                underlying.kind == CXType_UInt || underlying.kind == CXType_ULong ||
                                underlying.kind == CXType_ULongLong || underlying.kind == CXType_Bool;
        for (const CXCursor child : Children(cursor)) {
            if (clang_getCursorKind(child) != CXCursor_EnumConstantDecl) {
                continue;
            }
            EnumValueModel value;
            value.name = Spelling(child);
            value.value = isUnsigned ? static_cast<std::int64_t>(clang_getEnumConstantDeclUnsignedValue(child))
                                     : clang_getEnumConstantDeclValue(child);
            if (!ParseTags(child, TagTarget::EnumValue, value.tags)) {
                continue;
            }
            type.values.push_back(value);
        }
        m_model.types.push_back(type);
    }

    CXTranslationUnit m_unit = nullptr;
    ModuleModel& m_model;
    std::vector<std::string>& m_errors;
    std::vector<CXFile> m_files;
};

std::string ForwardSlashes(std::string path) {
    for (char& character : path) {
        if (character == '\\') {
            character = '/';
        }
    }
    return path;
}

} // namespace

std::uint64_t HashTypeName(const std::string& name) {
    std::uint64_t hash = kTypeHashBasis;
    for (const char character : name) {
        hash = (hash ^ static_cast<std::uint8_t>(character)) * kTypeHashPrime;
    }
    return hash;
}

std::uint64_t ContinueTypeHash(std::uint64_t hash, std::uint64_t value) {
    for (unsigned byte = 0; byte < 8; ++byte) {
        hash = (hash ^ ((value >> (byte * 8U)) & 0xFFU)) * kTypeHashPrime;
    }
    return hash;
}

bool AnalyzeModule(const std::vector<std::string>& arguments, ModuleModel& model, std::vector<std::string>& errors) {
    // One combined translation unit: every header is parsed once (section 8.6).
    std::string source;
    for (const std::string& header : model.headers) {
        source += "#include \"" + ForwardSlashes(header) + "\"\n";
    }
    CXUnsavedFile unsaved{kMainFile, source.c_str(), static_cast<unsigned long>(source.size())};

    std::vector<const char*> argumentPointers;
    argumentPointers.reserve(arguments.size());
    for (const std::string& argument : arguments) {
        argumentPointers.push_back(argument.c_str());
    }

    CXIndex index = clang_createIndex(0, 0);
    CXTranslationUnit unit = nullptr;
    const CXErrorCode result = clang_parseTranslationUnit2(index, kMainFile, argumentPointers.data(),
                                                           static_cast<int>(argumentPointers.size()), &unsaved, 1,
                                                           CXTranslationUnit_SkipFunctionBodies, &unit);
    if (result != CXError_Success || unit == nullptr) {
        errors.push_back("error: libclang could not parse the headers of " + model.module + " (code " +
                         std::to_string(static_cast<int>(result)) + ")");
        clang_disposeIndex(index);
        return false;
    }

    const unsigned diagnosticCount = clang_getNumDiagnostics(unit);
    for (unsigned diagnosticIndex = 0; diagnosticIndex < diagnosticCount; ++diagnosticIndex) {
        CXDiagnostic diagnostic = clang_getDiagnostic(unit, diagnosticIndex);
        if (clang_getDiagnosticSeverity(diagnostic) >= CXDiagnostic_Error) {
            errors.push_back(Take(clang_formatDiagnostic(diagnostic, clang_defaultDiagnosticDisplayOptions())));
        }
        clang_disposeDiagnostic(diagnostic);
    }

    if (errors.empty()) {
        Analyzer analyzer(unit, model, errors);
        analyzer.Run();
        clang_getInclusions(
            unit,
            [](CXFile file, CXSourceLocation* /*stack*/, unsigned /*depth*/, CXClientData data) {
                auto& dependencies = *static_cast<std::vector<std::string>*>(data);
                const std::string name = Take(clang_getFileName(file));
                if (!name.empty() && name != kMainFile) {
                    dependencies.push_back(ForwardSlashes(name));
                }
            },
            &model.dependencies);
    }
    clang_disposeTranslationUnit(unit);
    clang_disposeIndex(index);
    return errors.empty();
}

} // namespace Nexyl::Nht
