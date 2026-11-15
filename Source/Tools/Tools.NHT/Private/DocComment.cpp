#include "DocComment.h"

#include <cstdlib>
#include <string>
#include <vector>

namespace Nexyl::Nht {

namespace {

struct RawTag {
    std::string name;
    std::string value;
};

bool IsSpace(char character) {
    return character == ' ' || character == '\t' || character == '\r' || character == '\n';
}

std::string Trim(const std::string& text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && IsSpace(text[begin])) {
        ++begin;
    }
    while (end > begin && IsSpace(text[end - 1])) {
        --end;
    }
    return text.substr(begin, end - begin);
}

// The comment text without its markers (`///`, or `/**`, `*` and `*/`), one line per source line.
std::string StripMarkers(std::string raw) {
    raw = Trim(raw);
    const bool block = raw.rfind("/*", 0) == 0;
    if (block) {
        raw = raw.substr(raw.size() > 2 && (raw[2] == '*' || raw[2] == '!') ? 3 : 2);
        if (raw.size() >= 2 && raw.compare(raw.size() - 2, 2, "*/") == 0) {
            raw.resize(raw.size() - 2);
        }
    }
    std::string text;
    std::size_t position = 0;
    while (position <= raw.size()) {
        std::size_t end = raw.find('\n', position);
        if (end == std::string::npos) {
            end = raw.size();
        }
        std::string line = Trim(raw.substr(position, end - position));
        if (block) {
            if (!line.empty() && line[0] == '*') {
                line = line.substr(1);
            }
        } else if (line.rfind("///", 0) == 0) {
            line = line.substr(3);
        } else if (line.rfind("//", 0) == 0) {
            line = line.substr(2);
        }
        if (!line.empty() && line[0] == ' ') {
            line = line.substr(1);
        }
        text += line;
        text += '\n';
        position = end + 1;
    }
    return text;
}

bool IsTagCharacter(char character) {
    return (character >= 'a' && character <= 'z') || character == '-';
}

// Splits the text at tags: `@name` at the start of the text or after white space, outside backticks.
void SplitTags(const std::string& text, std::string& summary, std::vector<RawTag>& tags) {
    bool inCode = false;
    std::size_t sectionStart = 0;
    RawTag* current = nullptr;
    for (std::size_t position = 0; position < text.size(); ++position) {
        const char character = text[position];
        if (character == '`') {
            inCode = !inCode;
            continue;
        }
        const bool atWordStart = position == 0 || IsSpace(text[position - 1]);
        if (inCode || character != '@' || !atWordStart || position + 1 >= text.size() ||
            !IsTagCharacter(text[position + 1])) {
            continue;
        }
        const std::string section = text.substr(sectionStart, position - sectionStart);
        if (current == nullptr) {
            summary = section;
        } else {
            current->value = Trim(section);
        }
        std::size_t nameEnd = position + 1;
        while (nameEnd < text.size() && IsTagCharacter(text[nameEnd])) {
            ++nameEnd;
        }
        tags.push_back(RawTag{text.substr(position + 1, nameEnd - position - 1), ""});
        current = &tags.back();
        sectionStart = nameEnd;
        position = nameEnd - 1;
    }
    const std::string section = text.substr(sectionStart);
    if (current == nullptr) {
        summary = section;
    } else {
        current->value = Trim(section);
    }
}

// The first paragraph, with its lines joined by spaces.
std::string FirstParagraph(const std::string& text) {
    std::string result;
    std::size_t position = 0;
    while (position < text.size()) {
        std::size_t end = text.find('\n', position);
        if (end == std::string::npos) {
            end = text.size();
        }
        const std::string line = Trim(text.substr(position, end - position));
        if (line.empty()) {
            if (!result.empty()) {
                break;
            }
        } else {
            if (!result.empty()) {
                result += ' ';
            }
            result += line;
        }
        position = end + 1;
    }
    return result;
}

bool ParseNumber(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    char* end = nullptr;
    value = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size();
}

bool IsDocumentationTag(const std::string& name) {
    static constexpr const char* kTags[] = {"param", "return",      "error",        "threadsafety", "since",
                                            "pre",   "determinism", "experimental", "deprecated"};
    for (const char* tag : kTags) {
        if (name == tag) {
            return true;
        }
    }
    return false;
}

bool Allowed(const std::string& name, TagTarget target) {
    if (name == "hidden" || name == "renamed-from") {
        return true;
    }
    if (name == "category" || name == "noscript") {
        return target != TagTarget::EnumValue;
    }
    if (name == "opaque" || name == "version") {
        return target == TagTarget::Type;
    }
    return target == TagTarget::Field;
}

} // namespace

bool ParseDocComment(const std::string& raw, TagTarget target, Tags& tags, std::string& error) {
    std::string summary;
    std::vector<RawTag> rawTags;
    SplitTags(StripMarkers(raw), summary, rawTags);
    tags.description = FirstParagraph(summary);

    for (const RawTag& tag : rawTags) {
        if (IsDocumentationTag(tag.name)) {
            continue;
        }
        static constexpr const char* kReflectionTags[] = {"units",        "range",    "step",   "transient",
                                                          "presentation", "readonly", "hidden", "category",
                                                          "renamed-from", "noscript", "opaque", "version"};
        bool known = false;
        for (const char* name : kReflectionTags) {
            known = known || tag.name == name;
        }
        if (!known) {
            error = "unknown documentation tag '@" + tag.name + "'";
            return false;
        }
        if (!Allowed(tag.name, target)) {
            error = "the tag '@" + tag.name + "' does not apply here";
            return false;
        }
        const bool needsValue = tag.name == "units" || tag.name == "range" || tag.name == "step" ||
                                tag.name == "category" || tag.name == "renamed-from" || tag.name == "version";
        if (needsValue && tag.value.empty()) {
            error = "the tag '@" + tag.name + "' needs a value";
            return false;
        }
        if (!needsValue && !tag.value.empty()) {
            error = "the tag '@" + tag.name + "' takes no value";
            return false;
        }
        if (tag.name == "units") {
            tags.units = tag.value;
        } else if (tag.name == "category") {
            tags.category = tag.value;
        } else if (tag.name == "renamed-from") {
            tags.renamedFrom = tag.value;
        } else if (tag.name == "range") {
            const std::size_t separator = tag.value.find("..");
            if (separator == std::string::npos ||
                !ParseNumber(Trim(tag.value.substr(0, separator)), tags.rangeMinimum) ||
                !ParseNumber(Trim(tag.value.substr(separator + 2)), tags.rangeMaximum) ||
                tags.rangeMinimum > tags.rangeMaximum) {
                error = "'@range' needs the form <min>..<max> with min <= max";
                return false;
            }
            tags.hasRange = true;
        } else if (tag.name == "step") {
            if (!ParseNumber(tag.value, tags.step) || tags.step <= 0.0) {
                error = "'@step' needs a positive number";
                return false;
            }
        } else if (tag.name == "version") {
            double version = 0.0;
            if (!ParseNumber(tag.value, version) || version < 1.0 || version > 1'000'000.0 ||
                version != static_cast<double>(static_cast<std::uint32_t>(version))) {
                error = "'@version' needs a whole number from 1";
                return false;
            }
            tags.version = static_cast<std::uint32_t>(version);
        } else if (tag.name == "opaque") {
            tags.opaque = true;
        } else if (tag.name == "transient") {
            tags.transient = true;
        } else if (tag.name == "presentation") {
            tags.presentation = true;
        } else if (tag.name == "readonly") {
            tags.readOnly = true;
        } else if (tag.name == "hidden") {
            tags.hidden = true;
        } else if (tag.name == "noscript") {
            tags.noScript = true;
        }
    }
    if (tags.transient && tags.presentation) {
        error = "a field cannot be both '@transient' and '@presentation'";
        return false;
    }
    return true;
}

} // namespace Nexyl::Nht
