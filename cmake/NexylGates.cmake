# Registers the CI gates as CTest tests, so that one command runs them locally and in CI:
#
#   ctest --preset <preset>            everything
#   ctest --preset <preset> -L gate    gates only
#
# Gates are provider-independent scripts under Scripts/ci/. A gate whose tool is not installed exits
# with code 77 and is reported as skipped, unless NEXYL_REQUIRE_GATE_TOOLS is set (as it is in CI).

include_guard(GLOBAL)

function(nexyl_add_gates)
    find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

    set(ci "${CMAKE_SOURCE_DIR}/Scripts/ci")
    set(repo_arguments --repo "${CMAKE_SOURCE_DIR}")
    set(graph_arguments ${repo_arguments} --graph "${CMAKE_BINARY_DIR}/nexyl_modules.json")

    # Architecture rules R1-R7: the documented module table agrees with the manifests and obeys the rules.
    add_test(NAME gate.module_table
        COMMAND Python3::Interpreter "${ci}/check_module_table.py" ${graph_arguments})

    # A module manifest contains one nexyl_add_module() call and nothing else; module directories have the fixed shape.
    add_test(NAME gate.manifests
        COMMAND Python3::Interpreter "${ci}/check_manifests.py" ${graph_arguments})

    # Source rules: includes, standard library, platform conditionals, allocation, comment markers.
    add_test(NAME gate.sources
        COMMAND Python3::Interpreter "${ci}/check_sources.py" ${graph_arguments})

    # Section 18: formatting.
    add_test(NAME gate.format
        COMMAND Python3::Interpreter "${ci}/check_format.py" ${repo_arguments})

    # Section 22: static analysis profile.
    add_test(NAME gate.tidy
        COMMAND Python3::Interpreter "${ci}/check_tidy.py" ${repo_arguments} --build-dir "${CMAKE_BINARY_DIR}")

    # HDR-2: every public header compiles on its own.
    add_test(NAME gate.headers
        COMMAND "${CMAKE_COMMAND}" --build "${CMAKE_BINARY_DIR}" --config $<CONFIG>
                --target all_verify_interface_header_sets)

    # The gates themselves are tested, because a gate that cannot fail protects nothing.
    add_test(NAME selftest.module_rules
        COMMAND "${CMAKE_COMMAND}" -P "${CMAKE_SOURCE_DIR}/Tests/BuildSystem/ModuleRulesTest.cmake")
    add_test(NAME selftest.ci_scripts
        COMMAND Python3::Interpreter -m unittest discover --start-directory tests --top-level-directory .
        WORKING_DIRECTORY "${ci}")

    set(gates
        gate.module_table gate.manifests gate.sources gate.format gate.tidy gate.headers
        selftest.module_rules selftest.ci_scripts)
    set_tests_properties(${gates} PROPERTIES
        LABELS "gate"
        SKIP_RETURN_CODE 77
        ENVIRONMENT "PYTHONDONTWRITEBYTECODE=1")
    set_tests_properties(gate.headers gate.tidy PROPERTIES RUN_SERIAL TRUE)
endfunction()
