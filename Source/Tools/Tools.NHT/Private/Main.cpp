// Tools.NHT, the Nexyl header tool: generates reflection tables from a module's public headers
// (docs/NEXYL_ARCHITECTURE.md, section 8.6). The build runs it; nobody edits its output.
//
//   Tools.NHT generate --module <Name> --output <file.cpp> --arguments <file.rsp> [--depfile <file.d>]
//                      --header <path>...
//   Tools.NHT aggregate --output <file.cpp> [--table <Module>...]

#include "Analyzer.h"
#include "Generator.h"
#include "Model.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace {

using Nexyl::Nht::ModuleModel;

struct Options {
    std::string command;
    std::string module;
    std::string output;
    std::string arguments;
    std::string depfile;
    std::vector<std::string> headers;
    std::vector<std::string> tables;
};

bool ParseOptions(int count, const char* const* values, Options& options) {
    if (count < 2) {
        return false;
    }
    options.command = values[1];
    for (int index = 2; index < count; ++index) {
        const std::string name = values[index];
        if (index + 1 >= count) {
            return false;
        }
        const std::string value = values[++index];
        if (name == "--module") {
            options.module = value;
        } else if (name == "--output") {
            options.output = value;
        } else if (name == "--arguments") {
            options.arguments = value;
        } else if (name == "--depfile") {
            options.depfile = value;
        } else if (name == "--header") {
            options.headers.push_back(value);
        } else if (name == "--table") {
            options.tables.push_back(value);
        } else {
            return false;
        }
    }
    return !options.output.empty();
}

bool ReadFile(const std::string& path, std::string& text) {
    const std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }
    std::ostringstream stream;
    stream << file.rdbuf();
    text = stream.str();
    return true;
}

// Writes only when the content changes, so that an unchanged module does not trigger a rebuild (section 8.6).
bool WriteIfChanged(const std::string& path, const std::string& text) {
    std::string existing;
    if (ReadFile(path, existing) && existing == text) {
        return true;
    }
    // An output stream truncates by default.
    std::ofstream file(path, std::ios::binary);
    file << text;
    return static_cast<bool>(file);
}

std::vector<std::string> ReadArguments(const std::string& text) {
    std::vector<std::string> arguments;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (!line.empty()) {
            arguments.push_back(line);
        }
    }
    return arguments;
}

std::string EscapeDependency(const std::string& path) {
    std::string escaped;
    for (const char character : path) {
        if (character == ' ' || character == '#') {
            escaped += '\\';
        }
        escaped += character;
    }
    return escaped;
}

int Generate(const Options& options) {
    std::string argumentText;
    if (options.module.empty() || options.headers.empty() || !ReadFile(options.arguments, argumentText)) {
        static_cast<void>(
            std::fprintf(stderr, "Tools.NHT: generate needs --module, --arguments and at least one --header.\n"));
        return 2;
    }
    ModuleModel model;
    model.module = options.module;
    model.headers = options.headers;
    std::vector<std::string> errors;
    if (!Nexyl::Nht::AnalyzeModule(ReadArguments(argumentText), model, errors)) {
        for (const std::string& error : errors) {
            static_cast<void>(std::fprintf(stderr, "%s\n", error.c_str()));
        }
        static_cast<void>(std::fprintf(stderr,
                                       "Tools.NHT: the reflection table of %s was not generated (%zu error(s)).\n",
                                       options.module.c_str(), errors.size()));
        return 1;
    }
    if (!WriteIfChanged(options.output, Nexyl::Nht::GenerateTable(model))) {
        static_cast<void>(std::fprintf(stderr, "Tools.NHT: cannot write %s.\n", options.output.c_str()));
        return 1;
    }
    if (!options.depfile.empty()) {
        std::string depfile = EscapeDependency(options.output) + ":";
        for (const std::string& dependency : model.dependencies) {
            depfile += " \\\n  " + EscapeDependency(dependency);
        }
        depfile += '\n';
        if (!WriteIfChanged(options.depfile, depfile)) {
            static_cast<void>(std::fprintf(stderr, "Tools.NHT: cannot write %s.\n", options.depfile.c_str()));
            return 1;
        }
    }
    return 0;
}

int Aggregate(const Options& options) {
    if (!WriteIfChanged(options.output, Nexyl::Nht::GenerateAggregate(options.tables))) {
        static_cast<void>(std::fprintf(stderr, "Tools.NHT: cannot write %s.\n", options.output.c_str()));
        return 1;
    }
    return 0;
}

} // namespace

namespace Nexyl {

int ProgramMain(int argumentCount, const char* const* arguments);

} // namespace Nexyl

// The build generates main(), which calls this (rule TOOL-1).
int Nexyl::ProgramMain(int argumentCount, const char* const* arguments) {
    Options options;
    if (!ParseOptions(argumentCount, arguments, options)) {
        static_cast<void>(std::fprintf(stderr,
                                       "usage: Tools.NHT generate --module <Name> --output <file> --arguments <file> "
                                       "[--depfile <file>] --header <path>...\n"
                                       "       Tools.NHT aggregate --output <file> [--table <Module>...]\n"));
        return 2;
    }
    if (options.command == "generate") {
        return Generate(options);
    }
    if (options.command == "aggregate") {
        return Aggregate(options);
    }
    static_cast<void>(std::fprintf(stderr, "Tools.NHT: unknown command '%s'.\n", options.command.c_str()));
    return 2;
}
