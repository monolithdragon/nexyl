# Central compiler configuration for the whole engine.
#
# Modules never set compiler flags themselves (docs/CODING_STANDARDS.md, section 3).
# They declare properties in their manifest and receive the flag set defined here.

include_guard(GLOBAL)

# Rejects toolchains below the minimum versions of rule LANG-2.
function(_nexyl_check_compiler_version)
    set(id "${CMAKE_CXX_COMPILER_ID}")
    if(id STREQUAL "MSVC")
        set(minimum "19.40")
    elseif(id STREQUAL "Clang")
        set(minimum "18.0")
    elseif(id STREQUAL "AppleClang")
        set(minimum "16.0")
    elseif(id STREQUAL "GNU")
        set(minimum "14.0")
    else()
        message(FATAL_ERROR "Nexyl: unsupported compiler '${id}'. Supported: MSVC, Clang, AppleClang, GNU.")
    endif()

    if(CMAKE_CXX_COMPILER_VERSION VERSION_LESS minimum)
        message(FATAL_ERROR
            "Nexyl: ${id} ${CMAKE_CXX_COMPILER_VERSION} is too old. "
            "Minimum is ${minimum} (docs/CODING_STANDARDS.md, rule LANG-2).")
    endif()

    if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
        message(FATAL_ERROR "Nexyl: only 64-bit targets are supported.")
    endif()
endfunction()

# Applies the engine-wide language mode, warnings and code-generation flags to the calling directory scope.
# A macro, not a function, so that variables and directory properties land in the caller's scope.
macro(nexyl_configure_compiler)
    _nexyl_check_compiler_version()

    # LANG-1: ISO C++23, no extensions. Classic headers only; C++20 modules are banned, so skip dependency scanning.
    set(CMAKE_CXX_STANDARD 23)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    set(CMAKE_CXX_EXTENSIONS OFF)
    set(CMAKE_CXX_SCAN_FOR_MODULES OFF)

    # Hidden visibility: a symbol is exported only through its module's NX_<MODULE>_API macro.
    set(CMAKE_CXX_VISIBILITY_PRESET hidden)
    set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)

    # One output directory, so dynamic modules are found next to the executables that load them.
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
    set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

    if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
        # CMake injects /EHsc (and on old policies /GR); remove them so exceptions and RTTI are really off.
        string(REGEX REPLACE "[/-]EH[a-z]+-?" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
        string(REGEX REPLACE "[/-]GR-?" "" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")

        add_compile_options(
            /W4 /WX            # CFG-1: warnings are errors
            /permissive-       # strict conformance
            /utf-8             # UTF-8 source and execution character sets
            /Zc:__cplusplus    # report the real language version
            /EHs-c-            # ERR-1: no exceptions
            /GR-)              # no RTTI
        add_compile_definitions(_HAS_EXCEPTIONS=0 NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)

        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
            # clang-cl. Its /fp:precise maps to -ffp-model=precise, which turns contraction ON,
            # so the strict floating-point mode is requested through the Clang option instead.
            add_compile_options(
                -Wconversion -Wshadow
                -Wglobal-constructors -Wexit-time-destructors   # INIT-1, INIT-2
                /clang:-ffp-contract=off)                       # strict floating point, see the note below
        else()
            # cl
            add_compile_options(
                /fp:precise        # strict floating point, see the note below; cl does not contract in this mode
                /Zc:preprocessor   # conforming preprocessor
                /w14242 /w14254    # narrowing conversions (CLS-9)
                /w14263 /w14265    # hidden virtual functions, non-virtual destructor in a polymorphic class
                /w14287 /w14296    # suspicious unsigned comparisons
                /w14545 /w14546 /w14547 /w14549 /w14555   # expressions with no effect
                /w14640            # thread-unsafe local static initialization (INIT-2)
                /w14826 /w14905 /w14906 /w14928)          # sign-extending and suspicious conversions
        endif()
    else()
        add_compile_options(
            -Wall -Wextra -Wpedantic -Werror   # CFG-1
            -Wconversion -Wshadow              # CLS-9
            -fno-exceptions                    # ERR-1
            -fno-rtti
            -ffp-contract=off -fno-fast-math)  # strict floating point, see the note below

        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            add_compile_options(-Wglobal-constructors -Wexit-time-destructors)   # INIT-1, INIT-2
        else()
            add_compile_options(-fexcess-precision=standard)
        endif()
    endif()

    # Strict floating point is applied to EVERY module, not only simulation-safe ones.
    # Inline functions shared by translation units compiled with different floating-point flags are merged
    # by the linker into a single definition. With per-module flags, simulation code could silently end up
    # calling a copy compiled without the strict flags. See docs/NEXYL_ARCHITECTURE.md, section 7.3.

    _nexyl_configure_sanitizer()
endmacro()

# Optional sanitizer instrumentation, selected by the NEXYL_SANITIZER cache variable.
macro(_nexyl_configure_sanitizer)
    if(NOT NEXYL_SANITIZER STREQUAL "None")
        if(CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
            if(NEXYL_SANITIZER STREQUAL "Address" AND CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
                add_compile_options(/fsanitize=address)
            else()
                message(FATAL_ERROR "Nexyl: sanitizer '${NEXYL_SANITIZER}' is not supported with this toolchain.")
            endif()
        elseif(NEXYL_SANITIZER STREQUAL "Address")
            # Undefined-behaviour checks ride along. The vptr check needs RTTI, which is disabled.
            add_compile_options(-fsanitize=address,undefined -fno-sanitize=vptr -fno-omit-frame-pointer)
            add_link_options(-fsanitize=address,undefined -fno-sanitize=vptr)
        elseif(NEXYL_SANITIZER STREQUAL "Thread")
            add_compile_options(-fsanitize=thread -fno-omit-frame-pointer)
            add_link_options(-fsanitize=thread)
        else()
            message(FATAL_ERROR "Nexyl: unknown NEXYL_SANITIZER value '${NEXYL_SANITIZER}'. Use None, Address or Thread.")
        endif()
    endif()
endmacro()
