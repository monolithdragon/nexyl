# Tests for the module graph rules in cmake/NexylModule.cmake.
#
# Runs in script mode, without a compiler:   cmake -P Tests/BuildSystem/ModuleRulesTest.cmake
# Each case declares a small graph and states which rule, if any, must reject it.

cmake_minimum_required(VERSION 3.28)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
include(NexylModule)

set_property(GLOBAL PROPERTY NEXYL_TEST_FAILURES "")
set_property(GLOBAL PROPERTY NEXYL_TEST_CASES 0)

# Validates the modules registered since the last call and checks the outcome.
#   expected_rule   empty for a graph that must be accepted; otherwise the rule that must reject it.
function(expect_validation case expected_rule)
    nexyl_validate_modules(errors)

    set(failure "")
    if(expected_rule STREQUAL "")
        if(errors)
            set(failure "expected a valid graph, got: ${errors}")
        endif()
    else()
        set(found OFF)
        foreach(error IN LISTS errors)
            if(error MATCHES "^${expected_rule}: ")
                set(found ON)
            endif()
        endforeach()
        if(NOT found)
            set(failure "expected a '${expected_rule}' error, got: ${errors}")
        endif()
    endif()

    get_property(count GLOBAL PROPERTY NEXYL_TEST_CASES)
    math(EXPR count "${count} + 1")
    set_property(GLOBAL PROPERTY NEXYL_TEST_CASES "${count}")
    if(failure)
        string(REPLACE ";" " | " failure "${failure}")
        set_property(GLOBAL APPEND PROPERTY NEXYL_TEST_FAILURES "${case} -> ${failure}")
        message(STATUS "FAIL  ${case}")
    else()
        message(STATUS "ok    ${case}")
    endif()

    nexyl_reset_module_registry()
endfunction()

nexyl_reset_module_registry()

# ---- Accepted graphs -------------------------------------------------------------------------------------

nexyl_register_module(NAME Base KIND Foundation LAYER 0)
nexyl_register_module(NAME Platform KIND Foundation LAYER 1 DEPENDS Base)
nexyl_register_module(NAME Memory KIND Foundation LAYER 2 DEPENDS Base Platform)
expect_validation("valid foundation chain" "")

nexyl_register_module(NAME Base KIND Foundation LAYER 0)
nexyl_register_module(NAME Physics.Api KIND Contract LAYER 7 DEPENDS Base)
nexyl_register_module(NAME Physics.Nexyl KIND Impl LAYER 8 DEPENDS Base Physics.Api)
nexyl_register_module(NAME Engine KIND Feature LAYER 11 DEPENDS Base Physics.Api)
nexyl_register_module(NAME App.Player KIND App LAYER 16 DEPENDS Engine Physics.Nexyl)
expect_validation("app composes an implementation" "")

nexyl_register_module(NAME Base KIND Foundation LAYER 0)
nexyl_register_module(NAME Script.Visual KIND Impl LAYER 8 DEPENDS Base)
nexyl_register_module(NAME Editor KIND Editor LAYER 12 DEPENDS Base)
nexyl_register_module(NAME Script.Visual.Editor KIND Editor LAYER 15 DEPENDS Base Editor Script.Visual)
expect_validation("editor companion may depend on its implementation" "")

nexyl_register_module(NAME Base KIND Foundation LAYER 0)
nexyl_register_module(NAME Script.Lua KIND Adapter LAYER 8 DEPENDS Base THIRD_PARTY lua)
expect_validation("adapter may link third-party code" "")

# ---- R1: strictly lower layer ----------------------------------------------------------------------------

nexyl_register_module(NAME Memory KIND Foundation LAYER 2)
nexyl_register_module(NAME Math KIND Foundation LAYER 2 DEPENDS Memory)
expect_validation("R1 same-layer dependency" "R1")

nexyl_register_module(NAME Renderer KIND Feature LAYER 9)
nexyl_register_module(NAME ECS KIND Service LAYER 6 DEPENDS Renderer)
expect_validation("R1 upward dependency" "R1")

nexyl_register_module(NAME Jobs KIND Service LAYER 4 DEPENDS Jobs)
expect_validation("R1 self dependency" "R1")

# ---- R2: implementations and adapters are leaves ---------------------------------------------------------

nexyl_register_module(NAME RHI.Vulkan KIND Impl LAYER 8)
nexyl_register_module(NAME Renderer KIND Feature LAYER 9 DEPENDS RHI.Vulkan)
expect_validation("R2 feature depends on implementation" "R2")

nexyl_register_module(NAME Connect.Mqtt KIND Adapter LAYER 14)
nexyl_register_module(NAME Sim.Extra KIND Domain LAYER 15 DEPENDS Connect.Mqtt)
expect_validation("R2 domain depends on adapter" "R2")

nexyl_register_module(NAME Script.Visual KIND Impl LAYER 8)
nexyl_register_module(NAME Sim.Robotics.Editor KIND Editor LAYER 15 DEPENDS Script.Visual)
expect_validation("R2 editor module that is not the companion" "R2")

# ---- R3: contract modules are header-only ----------------------------------------------------------------

nexyl_register_module(NAME Audio.Api KIND Contract LAYER 7 HAS_SOURCES ON)
expect_validation("R3 contract with source files" "R3")

# ---- R4: tool libraries stay out of the runtime ----------------------------------------------------------

nexyl_register_module(NAME ShaderCompiler KIND ToolLib LAYER 6)
nexyl_register_module(NAME Shader KIND Feature LAYER 8 DEPENDS ShaderCompiler)
expect_validation("R4 runtime depends on tool library" "R4")

# ---- R5: nothing in the runtime depends on the editor ----------------------------------------------------

nexyl_register_module(NAME Editor KIND Editor LAYER 12)
nexyl_register_module(NAME Sim.Industrial KIND Domain LAYER 13 DEPENDS Editor)
expect_validation("R5 domain depends on editor" "R5")

# ---- R6: test libraries are not manifest dependencies ----------------------------------------------------

nexyl_register_module(NAME Test KIND TestLib LAYER 4)
nexyl_register_module(NAME ECS KIND Service LAYER 6 DEPENDS Test)
expect_validation("R6 module depends on test library" "R6")

# ---- R7: third-party code only in adapters and tools -----------------------------------------------------

nexyl_register_module(NAME Physics.Nexyl KIND Impl LAYER 8 THIRD_PARTY physx)
expect_validation("R7 implementation links third-party code" "R7")

nexyl_register_module(NAME Math KIND Foundation LAYER 2 THIRD_PARTY glm)
expect_validation("R7 foundation links third-party code" "R7")

# ---- Declaration errors ----------------------------------------------------------------------------------

nexyl_register_module(NAME Memory KIND Foundation LAYER 2 DEPENDS Platform)
expect_validation("unknown dependency" "DEPENDS")

nexyl_register_module(NAME Base KIND Foundation LAYER 0)
nexyl_register_module(NAME Memory KIND Foundation LAYER 2 DEPENDS Base Base)
expect_validation("duplicate dependency" "DEPENDS")

nexyl_register_module(NAME Memory KIND Library LAYER 2)
expect_validation("unknown kind" "KIND")

nexyl_register_module(NAME Memory KIND Foundation LAYER two)
expect_validation("non-numeric layer" "LAYER")

nexyl_register_module(NAME memory_pool KIND Foundation LAYER 2)
expect_validation("malformed name" "NAME")

nexyl_register_module(NAME Memory KIND Foundation LAYER 2)
nexyl_register_module(NAME Memory KIND Foundation LAYER 2)
expect_validation("module declared twice" "NAME")

nexyl_register_module(NAME Memory KIND Foundation LAYER 2 REFLECT Everything)
expect_validation("invalid reflect scope" "REFLECT")

# ---- Location --------------------------------------------------------------------------------------------

nexyl_register_module(NAME Memory KIND Foundation LAYER 2 DIRECTORY "/repo/Source/Runtime/Memory")
expect_validation("foundation module in Source/Runtime" "")

nexyl_register_module(NAME RHI.Vulkan KIND Impl LAYER 8 DIRECTORY "/repo/Source/Runtime/RHI.Vulkan")
expect_validation("implementation outside Source/Plugins" "LOCATION")

nexyl_register_module(NAME Memory KIND Foundation LAYER 2 DIRECTORY "/repo/Source/Runtime/Mem")
expect_validation("directory name differs from module name" "LOCATION")

# ---- Summary ---------------------------------------------------------------------------------------------

get_property(failures GLOBAL PROPERTY NEXYL_TEST_FAILURES)
get_property(count GLOBAL PROPERTY NEXYL_TEST_CASES)
list(LENGTH failures failure_count)
if(failures)
    foreach(failure IN LISTS failures)
        message(STATUS "  ${failure}")
    endforeach()
    message(FATAL_ERROR "${failure_count} of ${count} module-rule cases failed.")
endif()
message(STATUS "All ${count} module-rule cases passed.")
