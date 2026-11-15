# The Nexyl module system.
#
# A module is declared by exactly one nexyl_add_module() call in its CMakeLists.txt (the module manifest).
# This file registers every module, validates the module graph against the rules of
# docs/NEXYL_ARCHITECTURE.md section 4.1, creates the build targets and exports the graph for the CI gates.
#
# The registry and the validation are plain functions over global properties, so they also work in
# script mode (cmake -P). Tests/BuildSystem/ModuleRulesTest.cmake uses that to test the rules.

include_guard(GLOBAL)

set(NEXYL_MODULE_KINDS Foundation Service Contract Feature Domain Impl Adapter ToolLib TestLib Editor Tool App)

# Source groups under Source/ and the module kinds that live in each.
set(NEXYL_MODULE_GROUPS Runtime Plugins Editor Tools Apps)
set(NEXYL_MODULE_GROUP_Runtime Foundation Service Contract Feature Domain TestLib)
set(NEXYL_MODULE_GROUP_Plugins Impl Adapter)
set(NEXYL_MODULE_GROUP_Editor Editor)
set(NEXYL_MODULE_GROUP_Tools ToolLib Tool)
set(NEXYL_MODULE_GROUP_Apps App)

# Directory names under Private/ that hold platform-specific sources (rule PLAT-3).
set(NEXYL_PLATFORM_DIRECTORIES Windows Linux MacOS Android IOS Posix Apple)

set(_NEXYL_MODULE_FIELDS KIND LAYER DEPENDS THIRD_PARTY DIRECTORY REFLECT SIM_SAFE HAS_SOURCES TARGET_TYPE)

# ---------------------------------------------------------------------------------------------------------
# Registry
# ---------------------------------------------------------------------------------------------------------

# Clears the registry. Used by tests.
function(nexyl_reset_module_registry)
    get_property(names GLOBAL PROPERTY NEXYL_MODULES)
    foreach(name IN LISTS names)
        foreach(field IN LISTS _NEXYL_MODULE_FIELDS)
            set_property(GLOBAL PROPERTY "NEXYL_MODULE_${name}_${field}")
        endforeach()
    endforeach()
    set_property(GLOBAL PROPERTY NEXYL_MODULES "")
    set_property(GLOBAL PROPERTY NEXYL_MODULE_REGISTRATION_ERRORS "")
endfunction()

# Records one module declaration. Performs no validation beyond detecting duplicates.
function(nexyl_register_module)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;KIND;LAYER;DIRECTORY;REFLECT;SIM_SAFE;HAS_SOURCES" "DEPENDS;THIRD_PARTY")

    get_property(names GLOBAL PROPERTY NEXYL_MODULES)
    if("${arg_NAME}" IN_LIST names)
        set_property(GLOBAL APPEND PROPERTY NEXYL_MODULE_REGISTRATION_ERRORS
            "NAME: Module '${arg_NAME}' is declared more than once.")
        return()
    endif()

    if("${arg_REFLECT}" STREQUAL "")
        set(arg_REFLECT "None")
    endif()
    if("${arg_SIM_SAFE}" STREQUAL "")
        set(arg_SIM_SAFE "OFF")
    endif()
    if("${arg_HAS_SOURCES}" STREQUAL "")
        set(arg_HAS_SOURCES "OFF")
    endif()

    set_property(GLOBAL APPEND PROPERTY NEXYL_MODULES "${arg_NAME}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_KIND" "${arg_KIND}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_LAYER" "${arg_LAYER}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_DEPENDS" "${arg_DEPENDS}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_THIRD_PARTY" "${arg_THIRD_PARTY}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_DIRECTORY" "${arg_DIRECTORY}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_REFLECT" "${arg_REFLECT}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_SIM_SAFE" "${arg_SIM_SAFE}")
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${arg_NAME}_HAS_SOURCES" "${arg_HAS_SOURCES}")
endfunction()

# Reads one field of a registered module into <out>.
function(_nexyl_module_get name field out)
    get_property(value GLOBAL PROPERTY "NEXYL_MODULE_${name}_${field}")
    set(${out} "${value}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------------------------------------
# Validation
# ---------------------------------------------------------------------------------------------------------

# Checks one declaration in isolation. Returns a list of error strings in <out_errors>.
function(_nexyl_validate_declaration name out_errors)
    set(errors "")
    _nexyl_module_get("${name}" KIND kind)
    _nexyl_module_get("${name}" LAYER layer)
    _nexyl_module_get("${name}" DEPENDS depends)
    _nexyl_module_get("${name}" THIRD_PARTY third_party)
    _nexyl_module_get("${name}" DIRECTORY directory)
    _nexyl_module_get("${name}" REFLECT reflect)
    _nexyl_module_get("${name}" HAS_SOURCES has_sources)

    if(NOT name MATCHES "^[A-Z][A-Za-z0-9]*(\\.[A-Z][A-Za-z0-9]*)*$")
        list(APPEND errors "NAME: Module name '${name}' must be dot-separated PascalCase words.")
    endif()
    # Error strings are list elements, so they must not contain semicolons.
    string(REPLACE ";" ", " kinds_text "${NEXYL_MODULE_KINDS}")
    string(REPLACE ";" ", " groups_text "${NEXYL_MODULE_GROUPS}")

    if(NOT kind IN_LIST NEXYL_MODULE_KINDS)
        list(APPEND errors "KIND: Module '${name}' has unknown kind '${kind}'. Valid kinds are ${kinds_text}.")
    endif()
    if(NOT layer MATCHES "^[0-9]+$")
        list(APPEND errors "LAYER: Module '${name}' needs a non-negative integer LAYER, got '${layer}'.")
    endif()
    if(NOT reflect MATCHES "^(None|Public)$")
        list(APPEND errors "REFLECT: Module '${name}' has invalid REFLECT '${reflect}'. Use None or Public.")
    endif()

    set(seen "")
    foreach(dependency IN LISTS depends)
        if(dependency STREQUAL name)
            list(APPEND errors "R1: Module '${name}' depends on itself.")
        endif()
        if(dependency IN_LIST seen)
            list(APPEND errors "DEPENDS: Module '${name}' lists dependency '${dependency}' more than once.")
        endif()
        list(APPEND seen "${dependency}")
    endforeach()

    # R3: a contract module holds interfaces, handles and plain data only, so it has nothing to compile.
    if(kind STREQUAL "Contract" AND has_sources)
        list(APPEND errors
            "R3: Contract module '${name}' has source files in Private/. Contract modules are header-only.")
    endif()

    # R7: third-party code is linked only by adapters and tools.
    if(third_party AND NOT kind MATCHES "^(Adapter|Tool)$")
        list(APPEND errors
            "R7: Module '${name}' (${kind}) links third-party code. Only Adapter and Tool modules may do so.")
    endif()

    # Location: Source/<Group>/<Name>/, with the group matching the kind.
    if(directory AND kind IN_LIST NEXYL_MODULE_KINDS)
        get_filename_component(leaf "${directory}" NAME)
        get_filename_component(parent "${directory}" DIRECTORY)
        get_filename_component(group "${parent}" NAME)
        if(NOT leaf STREQUAL name)
            list(APPEND errors "LOCATION: Module '${name}' lives in directory '${leaf}'. Directory and module name must match.")
        endif()
        if(NOT group IN_LIST NEXYL_MODULE_GROUPS)
            list(APPEND errors "LOCATION: Module '${name}' is in '${group}/', which is not a source group (${groups_text}).")
        elseif(NOT kind IN_LIST NEXYL_MODULE_GROUP_${group})
            list(APPEND errors "LOCATION: Module '${name}' (${kind}) does not belong in Source/${group}/.")
        endif()
    endif()

    set(${out_errors} "${errors}" PARENT_SCOPE)
endfunction()

# Validates every declaration and the whole graph. Returns a list of error strings in <out_errors>.
# Each error starts with the rule it violates, for example "R1: ...".
function(nexyl_validate_modules out_errors)
    get_property(names GLOBAL PROPERTY NEXYL_MODULES)
    get_property(errors GLOBAL PROPERTY NEXYL_MODULE_REGISTRATION_ERRORS)

    foreach(name IN LISTS names)
        _nexyl_validate_declaration("${name}" declaration_errors)
        list(APPEND errors ${declaration_errors})

        _nexyl_module_get("${name}" KIND kind)
        _nexyl_module_get("${name}" LAYER layer)
        _nexyl_module_get("${name}" DEPENDS depends)

        foreach(dependency IN LISTS depends)
            if(dependency STREQUAL name)
                continue()
            endif()
            if(NOT dependency IN_LIST names)
                list(APPEND errors "DEPENDS: Module '${name}' depends on unknown module '${dependency}'.")
                continue()
            endif()

            _nexyl_module_get("${dependency}" KIND dependency_kind)
            _nexyl_module_get("${dependency}" LAYER dependency_layer)

            # R1: strictly lower layer.
            if(layer MATCHES "^[0-9]+$" AND dependency_layer MATCHES "^[0-9]+$")
                if(NOT dependency_layer LESS layer)
                    list(APPEND errors
                        "R1: Module '${name}' (layer ${layer}) depends on '${dependency}' (layer ${dependency_layer}). Dependencies must be in a strictly lower layer.")
                endif()
            endif()

            # R2: implementations and adapters are leaves.
            if(dependency_kind MATCHES "^(Impl|Adapter)$")
                set(is_companion OFF)
                if(kind STREQUAL "Editor" AND name STREQUAL "${dependency}.Editor")
                    set(is_companion ON)
                endif()
                if(NOT kind MATCHES "^(App|Tool)$" AND NOT is_companion)
                    list(APPEND errors
                        "R2: Module '${name}' (${kind}) depends on '${dependency}' (${dependency_kind}). Only App and Tool modules and the '${dependency}.Editor' companion may depend on it.")
                endif()
            endif()

            # R4: tool libraries never reach a shipping runtime.
            if(dependency_kind STREQUAL "ToolLib" AND NOT kind MATCHES "^(ToolLib|Editor|Tool|App)$")
                list(APPEND errors
                    "R4: Module '${name}' (${kind}) depends on tool library '${dependency}'. Only ToolLib, Editor, Tool and App modules may.")
            endif()

            # R5: nothing in the runtime depends on the editor.
            if(dependency_kind STREQUAL "Editor" AND NOT kind MATCHES "^(Editor|Tool|App)$")
                list(APPEND errors
                    "R5: Module '${name}' (${kind}) depends on editor module '${dependency}'. Only Editor, Tool and App modules may.")
            endif()

            # R6: test libraries are linked by test targets only.
            if(dependency_kind STREQUAL "TestLib")
                list(APPEND errors
                    "R6: Module '${name}' depends on test library '${dependency}'. Test libraries are linked into test targets automatically.")
            endif()
        endforeach()
    endforeach()

    set(${out_errors} "${errors}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------------------------------------
# Target creation
# ---------------------------------------------------------------------------------------------------------

# Maps a module name to its export macro: "Memory" -> NX_MEMORY_API, "Physics.Api" -> NX_PHYSICS_API.
function(_nexyl_api_macro name out)
    string(REGEX REPLACE "\\.Api$" "" base "${name}")
    string(REPLACE "." "_" base "${base}")
    string(TOUPPER "${base}" base)
    set(${out} "NX_${base}_API" PARENT_SCOPE)
endfunction()

# Platform-specific source directories that are compiled for the current target system.
function(_nexyl_active_platform_directories out)
    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
        set(active Windows)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set(active Linux Posix)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
        set(active MacOS Apple Posix)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
        set(active Android Posix)
    elseif(CMAKE_SYSTEM_NAME STREQUAL "iOS")
        set(active IOS Apple Posix)
    else()
        set(active "")
    endif()
    set(${out} "${active}" PARENT_SCOPE)
endfunction()

# Collects the files of a module directory, dropping sources of platforms other than the target (PLAT-3).
function(_nexyl_collect_sources directory out_public_headers out_private_sources out_test_sources)
    file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS LIST_DIRECTORIES false "${directory}/Public/*.h")
    file(GLOB_RECURSE private_all CONFIGURE_DEPENDS LIST_DIRECTORIES false
        "${directory}/Private/*.cpp" "${directory}/Private/*.mm" "${directory}/Private/*.h" "${directory}/Private/*.inl")
    file(GLOB_RECURSE test_sources CONFIGURE_DEPENDS LIST_DIRECTORIES false "${directory}/Tests/*.cpp")

    _nexyl_active_platform_directories(active)
    string(REPLACE ";" "|" platform_pattern "${NEXYL_PLATFORM_DIRECTORIES}")

    set(private_sources "")
    foreach(file IN LISTS private_all)
        file(RELATIVE_PATH relative "${directory}/Private" "${file}")
        if("/${relative}" MATCHES "/(${platform_pattern})/")
            if(NOT CMAKE_MATCH_1 IN_LIST active)
                continue()
            endif()
        endif()
        if(file MATCHES "\\.mm$" AND NOT APPLE)
            continue()
        endif()
        list(APPEND private_sources "${file}")
    endforeach()

    set(${out_public_headers} "${public_headers}" PARENT_SCOPE)
    set(${out_private_sources} "${private_sources}" PARENT_SCOPE)
    set(${out_test_sources} "${test_sources}" PARENT_SCOPE)
endfunction()

# Declares a module. This is the only command a module manifest may contain.
#
#   nexyl_add_module(
#       NAME      <Name>                 # dot-separated PascalCase, equal to the directory name
#       KIND      <Kind>                 # one of NEXYL_MODULE_KINDS
#       LAYER     <n>                    # one above the highest dependency
#       [DEPENDS  <Module>...]           # direct dependencies, all in strictly lower layers
#       [REFLECT  None|Public]           # reflection scope (recorded; the header tool arrives in phase 2)
#       [SIM_SAFE ON|OFF]                # simulation-safe: enables the determinism lint
#       [THIRD_PARTY <target>...])       # Adapter and Tool modules only
function(nexyl_add_module)
    cmake_parse_arguments(PARSE_ARGV 0 arg "" "NAME;KIND;LAYER;REFLECT;SIM_SAFE" "DEPENDS;THIRD_PARTY")
    if(arg_UNPARSED_ARGUMENTS)
        message(FATAL_ERROR "nexyl_add_module: unknown arguments: ${arg_UNPARSED_ARGUMENTS}")
    endif()
    foreach(required NAME KIND LAYER)
        if(NOT DEFINED arg_${required} OR "${arg_${required}}" STREQUAL "")
            message(FATAL_ERROR "nexyl_add_module: ${required} is required (${CMAKE_CURRENT_SOURCE_DIR}).")
        endif()
    endforeach()
    if(NOT DEFINED arg_REFLECT)
        set(arg_REFLECT "None")
    endif()
    if(NOT DEFINED arg_SIM_SAFE)
        set(arg_SIM_SAFE "OFF")
    endif()

    set(name "${arg_NAME}")
    set(directory "${CMAKE_CURRENT_SOURCE_DIR}")
    _nexyl_collect_sources("${directory}" public_headers private_sources test_sources)

    set(has_sources OFF)
    foreach(file IN LISTS private_sources)
        if(file MATCHES "\\.(cpp|mm)$")
            set(has_sources ON)
            break()
        endif()
    endforeach()

    nexyl_register_module(
        NAME "${name}" KIND "${arg_KIND}" LAYER "${arg_LAYER}" DIRECTORY "${directory}"
        REFLECT "${arg_REFLECT}" SIM_SAFE "${arg_SIM_SAFE}" HAS_SOURCES "${has_sources}"
        DEPENDS ${arg_DEPENDS} THIRD_PARTY ${arg_THIRD_PARTY})

    _nexyl_validate_declaration("${name}" errors)
    if(errors)
        foreach(error IN LISTS errors)
            message(SEND_ERROR "Nexyl module manifest: ${error}")
        endforeach()
        message(FATAL_ERROR "Invalid module manifest in ${directory}. See docs/NEXYL_ARCHITECTURE.md, section 4.")
    endif()

    set(target "Nexyl.${name}")
    _nexyl_api_macro("${name}" api_macro)

    if(arg_KIND MATCHES "^(Tool|App)$")
        set(type EXECUTABLE)
        add_executable(${target} ${private_sources})
    elseif(NOT has_sources)
        set(type INTERFACE)
        add_library(${target} INTERFACE)
    elseif(NEXYL_LINKAGE STREQUAL "Dynamic")
        set(type SHARED)
        add_library(${target} SHARED ${private_sources})
    else()
        set(type STATIC)
        add_library(${target} STATIC ${private_sources})
    endif()
    set_property(GLOBAL PROPERTY "NEXYL_MODULE_${name}_TARGET_TYPE" "${type}")

    # Public headers form a header set: this adds the include directory and lets CMake verify that
    # every public header compiles on its own (rule HDR-2).
    if(public_headers AND NOT type STREQUAL "EXECUTABLE")
        if(type STREQUAL "INTERFACE")
            set(scope INTERFACE)
        else()
            set(scope PUBLIC)
        endif()
        target_sources(${target} ${scope} FILE_SET HEADERS BASE_DIRS "${directory}/Public" FILES ${public_headers})
    endif()

    if(NOT type STREQUAL "INTERFACE")
        target_include_directories(${target} PRIVATE "${directory}/Private")
    endif()

    # The export macro is supplied by the build system, so no module needs an export header.
    if(type STREQUAL "SHARED")
        if(WIN32)
            target_compile_definitions(${target}
                PRIVATE "${api_macro}=__declspec(dllexport)"
                INTERFACE "${api_macro}=__declspec(dllimport)")
        else()
            target_compile_definitions(${target} PUBLIC "${api_macro}=__attribute__((visibility(\"default\")))")
        endif()
    elseif(type STREQUAL "STATIC")
        target_compile_definitions(${target} PUBLIC "${api_macro}=")
    elseif(type STREQUAL "INTERFACE")
        target_compile_definitions(${target} INTERFACE "${api_macro}=")
    endif()

    get_filename_component(group_directory "${directory}" DIRECTORY)
    get_filename_component(group "${group_directory}" NAME)
    set_target_properties(${target} PROPERTIES FOLDER "${group}")

    # Unit tests: every .cpp under Tests/ is linked into one test executable for the module. Its test table and
    # main() are generated from the NX_TEST definitions, because static registration is forbidden (rule INIT-1).
    if(test_sources AND NOT type STREQUAL "EXECUTABLE")
        set(generator "${CMAKE_SOURCE_DIR}/Scripts/build/generate_test_registry.py")
        set(registry "${CMAKE_CURRENT_BINARY_DIR}/${target}.TestRegistry.gen.cpp")
        add_custom_command(
            OUTPUT "${registry}"
            COMMAND Python3::Interpreter "${generator}" --repo "${CMAKE_SOURCE_DIR}" --module "${name}"
                    --output "${registry}" ${test_sources}
            DEPENDS ${test_sources} "${generator}" "${CMAKE_SOURCE_DIR}/Scripts/ci/nexyl_ci.py"
            COMMENT "Generating the test registry of ${name}"
            VERBATIM)
        add_executable(${target}.Tests ${test_sources} "${registry}")
        target_link_libraries(${target}.Tests PRIVATE ${target})
        target_include_directories(${target}.Tests PRIVATE "${directory}/Tests")
        set_target_properties(${target}.Tests PROPERTIES FOLDER "Tests")
        add_test(NAME "unit.${name}" COMMAND ${target}.Tests)
        set_tests_properties("unit.${name}" PROPERTIES LABELS "unit")
        set_property(GLOBAL APPEND PROPERTY NEXYL_TEST_TARGETS "${target}.Tests")
    endif()
endfunction()

# Adds every module found under Source/<Group>/<Name>/CMakeLists.txt.
function(nexyl_add_module_directories)
    foreach(group IN LISTS NEXYL_MODULE_GROUPS)
        file(GLOB manifests CONFIGURE_DEPENDS LIST_DIRECTORIES false "${CMAKE_SOURCE_DIR}/Source/${group}/*/CMakeLists.txt")
        list(SORT manifests)
        foreach(manifest IN LISTS manifests)
            get_filename_component(module_directory "${manifest}" DIRECTORY)
            add_subdirectory("${module_directory}")
        endforeach()
    endforeach()
endfunction()

# ---------------------------------------------------------------------------------------------------------
# Finalization
# ---------------------------------------------------------------------------------------------------------

# Converts a CMake list into the body of a JSON string array.
function(_nexyl_json_array list_value out)
    set(items "")
    foreach(item IN LISTS list_value)
        list(APPEND items "\"${item}\"")
    endforeach()
    string(REPLACE ";" ", " joined "${items}")
    set(${out} "${joined}" PARENT_SCOPE)
endfunction()

# Writes the validated module graph as JSON. The CI gate scripts read this file.
function(_nexyl_write_module_graph path)
    get_property(names GLOBAL PROPERTY NEXYL_MODULES)
    set(entries "")
    foreach(name IN LISTS names)
        _nexyl_module_get("${name}" KIND kind)
        _nexyl_module_get("${name}" LAYER layer)
        _nexyl_module_get("${name}" DEPENDS depends)
        _nexyl_module_get("${name}" THIRD_PARTY third_party)
        _nexyl_module_get("${name}" DIRECTORY directory)
        _nexyl_module_get("${name}" REFLECT reflect)
        _nexyl_module_get("${name}" SIM_SAFE sim_safe)
        _nexyl_module_get("${name}" TARGET_TYPE target_type)

        file(RELATIVE_PATH relative_directory "${CMAKE_SOURCE_DIR}" "${directory}")
        _nexyl_json_array("${depends}" depends_json)
        _nexyl_json_array("${third_party}" third_party_json)
        if(sim_safe)
            set(sim_safe_json "true")
        else()
            set(sim_safe_json "false")
        endif()

        string(CONCAT entry
            "    {\n"
            "      \"name\": \"${name}\",\n"
            "      \"kind\": \"${kind}\",\n"
            "      \"layer\": ${layer},\n"
            "      \"dependencies\": [${depends_json}],\n"
            "      \"thirdParty\": [${third_party_json}],\n"
            "      \"directory\": \"${relative_directory}\",\n"
            "      \"reflect\": \"${reflect}\",\n"
            "      \"simSafe\": ${sim_safe_json},\n"
            "      \"targetType\": \"${target_type}\"\n"
            "    }")
        list(APPEND entries "${entry}")
    endforeach()

    string(REPLACE ";" ",\n" body "${entries}")
    file(WRITE "${path}" "{\n  \"version\": 1,\n  \"modules\": [\n${body}\n  ]\n}\n")
endfunction()

# Validates the graph, links the dependencies and exports the graph. Called once, after all modules are added.
function(nexyl_finalize_modules)
    nexyl_validate_modules(errors)
    if(errors)
        foreach(error IN LISTS errors)
            message(SEND_ERROR "Nexyl module graph: ${error}")
        endforeach()
        message(FATAL_ERROR "The module graph violates the dependency rules. See docs/NEXYL_ARCHITECTURE.md, section 4.1.")
    endif()

    get_property(names GLOBAL PROPERTY NEXYL_MODULES)
    foreach(name IN LISTS names)
        _nexyl_module_get("${name}" DEPENDS depends)
        _nexyl_module_get("${name}" THIRD_PARTY third_party)
        _nexyl_module_get("${name}" TARGET_TYPE type)

        if(type STREQUAL "INTERFACE")
            set(scope INTERFACE)
        elseif(type STREQUAL "EXECUTABLE")
            set(scope PRIVATE)
        else()
            set(scope PUBLIC)
        endif()

        foreach(dependency IN LISTS depends)
            target_link_libraries("Nexyl.${name}" ${scope} "Nexyl.${dependency}")
        endforeach()
        foreach(library IN LISTS third_party)
            if(NOT TARGET "${library}")
                message(FATAL_ERROR "Module '${name}' lists third-party target '${library}', which does not exist.")
            endif()
            target_link_libraries("Nexyl.${name}" PRIVATE "${library}")
        endforeach()
    endforeach()

    # R6: the test library is linked into test targets here, never through a manifest. Every test executable
    # needs it, because its generated main() calls the runner.
    get_property(test_targets GLOBAL PROPERTY NEXYL_TEST_TARGETS)
    if(test_targets AND NOT TARGET Nexyl.Test)
        message(FATAL_ERROR "Modules have unit tests, but the Test module (Source/Runtime/Test) is missing.")
    endif()
    foreach(test_target IN LISTS test_targets)
        if(NOT test_target STREQUAL "Nexyl.Test.Tests")
            target_link_libraries(${test_target} PRIVATE Nexyl.Test)
        endif()
    endforeach()

    _nexyl_write_module_graph("${CMAKE_BINARY_DIR}/nexyl_modules.json")
    list(LENGTH names module_count)
    message(STATUS "Nexyl: ${module_count} module(s) validated; graph written to nexyl_modules.json")
endfunction()
