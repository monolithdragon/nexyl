cmake_minimum_required(VERSION 3.28)

project(Nexyl
    VERSION 0.1.0
    DESCRIPTION "Nexyl real-time 2D/3D game and simulation engine"
    LANGUAGES CXX)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds are not supported. Use a preset: cmake --preset <name>.")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# ---- Build options ---------------------------------------------------------------------------------------
set(NEXYL_LINKAGE "Static" CACHE STRING "How engine modules are linked: Static or Dynamic")
set_property(CACHE NEXYL_LINKAGE PROPERTY STRINGS Static Dynamic)
if(NOT NEXYL_LINKAGE MATCHES "^(Static|Dynamic)$")
    message(FATAL_ERROR "NEXYL_LINKAGE must be Static or Dynamic, got '${NEXYL_LINKAGE}'.")
endif()

set(NEXYL_SANITIZER "None" CACHE STRING "Sanitizer instrumentation: None, Address or Thread")
set_property(CACHE NEXYL_SANITIZER PROPERTY STRINGS None Address Thread)

# ---- Toolchain -------------------------------------------------------------------------------------------
include(NexylCompiler)
include(NexylConformance)
include(NexylModule)
include(NexylGates)

# Python runs the CI gates and the build-time generators (the test registry).
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

nexyl_configure_compiler()
nexyl_check_conformance()

# The engine version has one source: the project() call above.
add_compile_definitions(
    NX_ENGINE_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    NX_ENGINE_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    NX_ENGINE_VERSION_PATCH=${PROJECT_VERSION_PATCH})

# ---- Modules ---------------------------------------------------------------------------------------------
enable_testing()

# Rule HDR-2: every public header must compile on its own.
set(CMAKE_VERIFY_INTERFACE_HEADER_SETS ON)

nexyl_add_module_directories()
nexyl_finalize_modules()

# ---- CI gates (run through ctest) ------------------------------------------------------------------------
nexyl_add_gates()

# ---- Editor support --------------------------------------------------------------------------------------
# clangd looks for compile_commands.json in the source tree. Without it, an editor knows neither the include
# paths of the modules nor the language mode, and reports every engine header as missing.
option(NEXYL_EDITOR_DATABASE "Copy compile_commands.json to the source root after each build, for clangd" OFF)
if(NEXYL_EDITOR_DATABASE)
    add_custom_target(Nexyl.EditorDatabase ALL
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different
                "${CMAKE_BINARY_DIR}/compile_commands.json" "${CMAKE_SOURCE_DIR}/compile_commands.json"
        COMMENT "Updating compile_commands.json in the source root for editors"
        VERBATIM)
endif()
