# Third-party dependencies (docs/CODING_STANDARDS.md, section 21). Each one becomes an imported target that only
# Adapter and Tool modules may name in THIRD_PARTY (rule R7). Every entry is in the register of section 21.1.
#
# ThirdParty.LibClang: the C API of libclang (clang-c/Index.h) for the header tool Tools.NHT. The LLVM major
# version is pinned to REQUIRED_LLVM_MAJOR in Scripts/ci/nexyl_ci.py, the same version as clang-format and
# clang-tidy, so that every machine generates the same reflection tables. It is a build requirement.

include_guard(GLOBAL)

# The pinned LLVM major version, read from its single source.
function(_nexyl_required_llvm_major out)
    file(STRINGS "${CMAKE_SOURCE_DIR}/Scripts/ci/nexyl_ci.py" line REGEX "^REQUIRED_LLVM_MAJOR = [0-9]+$")
    if(NOT line)
        message(FATAL_ERROR "REQUIRED_LLVM_MAJOR not found in Scripts/ci/nexyl_ci.py.")
    endif()
    string(REGEX REPLACE "^REQUIRED_LLVM_MAJOR = " "" major "${line}")
    set(${out} "${major}" PARENT_SCOPE)
endfunction()

function(nexyl_find_libclang)
    _nexyl_required_llvm_major(major)
    set(NEXYL_LLVM_ROOT "" CACHE PATH "Root of the LLVM ${major} installation that provides libclang (optional).")

    set(hints "")
    if(NEXYL_LLVM_ROOT)
        list(APPEND hints "${NEXYL_LLVM_ROOT}")
    elseif(DEFINED ENV{NEXYL_LLVM_ROOT})
        list(APPEND hints "$ENV{NEXYL_LLVM_ROOT}")
    endif()
    if(WIN32)
        list(APPEND hints "$ENV{ProgramFiles}/LLVM")
    elseif(APPLE)
        list(APPEND hints "/opt/homebrew/opt/llvm@${major}" "/opt/homebrew/opt/llvm" "/usr/local/opt/llvm@${major}"
             "/usr/local/opt/llvm")
    else()
        list(APPEND hints "/usr/lib/llvm-${major}")
    endif()

    find_path(NEXYL_LIBCLANG_INCLUDE_DIR clang-c/Index.h HINTS ${hints} PATH_SUFFIXES include NO_DEFAULT_PATH)
    find_library(NEXYL_LIBCLANG_LIBRARY NAMES libclang clang HINTS ${hints} PATH_SUFFIXES lib NO_DEFAULT_PATH)
    find_program(NEXYL_LIBCLANG_CLANG NAMES clang HINTS ${hints} PATH_SUFFIXES bin NO_DEFAULT_PATH)
    if(NOT NEXYL_LIBCLANG_INCLUDE_DIR OR NOT NEXYL_LIBCLANG_LIBRARY OR NOT NEXYL_LIBCLANG_CLANG)
        message(FATAL_ERROR
            "libclang of LLVM ${major} was not found; the header tool (Tools.NHT) needs it. Install LLVM ${major} "
            "with its development files (Windows: the LLVM installer; Linux: libclang-${major}-dev from apt.llvm.org; "
            "macOS: Homebrew llvm), or set NEXYL_LLVM_ROOT. Searched: ${hints}")
    endif()

    # The clang driver next to the library tells the version of the installation.
    execute_process(COMMAND "${NEXYL_LIBCLANG_CLANG}" --version OUTPUT_VARIABLE version_text RESULT_VARIABLE result)
    string(REGEX MATCH "version ([0-9]+)\\." version_match "${version_text}")
    if(NOT result EQUAL 0 OR NOT CMAKE_MATCH_1 STREQUAL major)
        message(FATAL_ERROR
            "libclang at ${NEXYL_LIBCLANG_LIBRARY} belongs to LLVM '${CMAKE_MATCH_1}', but LLVM ${major} is required "
            "(REQUIRED_LLVM_MAJOR). Set NEXYL_LLVM_ROOT to an LLVM ${major} installation.")
    endif()

    # libclang finds its built-in headers (stddef.h, arm_neon.h) and the system headers the way the clang driver
    # does, which can fail when it is loaded from an unusual place. The header tool passes both explicitly.
    execute_process(COMMAND "${NEXYL_LIBCLANG_CLANG}" -print-resource-dir
        OUTPUT_VARIABLE resource_directory OUTPUT_STRIP_TRAILING_WHITESPACE RESULT_VARIABLE result)
    if(NOT result EQUAL 0 OR NOT IS_DIRECTORY "${resource_directory}")
        message(FATAL_ERROR "Cannot find the resource directory of ${NEXYL_LIBCLANG_CLANG}.")
    endif()
    file(TO_CMAKE_PATH "${resource_directory}" resource_directory)
    set(parse_arguments "-resource-dir=${resource_directory}")
    if(APPLE)
        set(sysroot "${CMAKE_OSX_SYSROOT}")
        if(NOT sysroot)
            execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE sysroot OUTPUT_STRIP_TRAILING_WHITESPACE)
        endif()
        if(sysroot)
            list(APPEND parse_arguments "-isysroot" "${sysroot}")
        endif()
    endif()
    set(NEXYL_LIBCLANG_PARSE_ARGUMENTS "${parse_arguments}" CACHE INTERNAL "Arguments the header tool passes to libclang.")

    add_library(ThirdParty.LibClang SHARED IMPORTED GLOBAL)
    set_target_properties(ThirdParty.LibClang PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${NEXYL_LIBCLANG_INCLUDE_DIR}")
    if(WIN32)
        get_filename_component(llvm_root "${NEXYL_LIBCLANG_CLANG}" DIRECTORY)
        set_target_properties(ThirdParty.LibClang PROPERTIES
            IMPORTED_IMPLIB "${NEXYL_LIBCLANG_LIBRARY}"
            IMPORTED_LOCATION "${llvm_root}/libclang.dll")
    else()
        set_target_properties(ThirdParty.LibClang PROPERTIES IMPORTED_LOCATION "${NEXYL_LIBCLANG_LIBRARY}")
    endif()
    message(STATUS "Nexyl: libclang of LLVM ${major}: ${NEXYL_LIBCLANG_LIBRARY}")
endfunction()
