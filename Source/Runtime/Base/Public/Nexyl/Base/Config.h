#pragma once

// Platform, compiler, architecture and build-configuration detection.
//
// This is the only file in the engine that tests the toolchain's own predefined macros (rule PLAT-1).
// Every other file uses the NX_ macros below. Each NX_PLATFORM_*, NX_COMPILER_* and NX_ARCH_* macro is
// always defined, as 1 or 0, so that it is used with #if and never with #ifdef.

// ---- Platform ----------------------------------------------------------------------------------------------

/// 1 when compiling for Windows, otherwise 0. @since 0.1
#define NX_PLATFORM_WINDOWS 0
/// 1 when compiling for Linux (not Android), otherwise 0. @since 0.1
#define NX_PLATFORM_LINUX 0
/// 1 when compiling for Android, otherwise 0. @since 0.1
#define NX_PLATFORM_ANDROID 0
/// 1 when compiling for macOS, otherwise 0. @since 0.1
#define NX_PLATFORM_MACOS 0
/// 1 when compiling for iOS, otherwise 0. @since 0.1
#define NX_PLATFORM_IOS 0

#if defined(_WIN32)
#undef NX_PLATFORM_WINDOWS
#define NX_PLATFORM_WINDOWS 1
#elif defined(__ANDROID__)
#undef NX_PLATFORM_ANDROID
#define NX_PLATFORM_ANDROID 1
#elif defined(__linux__)
#undef NX_PLATFORM_LINUX
#define NX_PLATFORM_LINUX 1
#elif defined(__APPLE__)
// Apple targets are told apart with a Clang builtin, so no system header is needed.
// It is tested in a nested #if because other preprocessors cannot parse it.
#if __is_target_os(ios)
#undef NX_PLATFORM_IOS
#define NX_PLATFORM_IOS 1
#elif __is_target_os(macos)
#undef NX_PLATFORM_MACOS
#define NX_PLATFORM_MACOS 1
#else
#error "Nexyl: unsupported Apple platform."
#endif
#else
// Console platforms are added by their private overlay repositories (docs/NEXYL_ARCHITECTURE.md, section 2).
#error "Nexyl: unsupported platform."
#endif

/// 1 on platforms with a POSIX system interface (Linux, Android, macOS, iOS), otherwise 0. @since 0.1
#define NX_PLATFORM_POSIX (NX_PLATFORM_LINUX || NX_PLATFORM_ANDROID || NX_PLATFORM_MACOS || NX_PLATFORM_IOS)
/// 1 on Apple platforms (macOS, iOS), otherwise 0. @since 0.1
#define NX_PLATFORM_APPLE (NX_PLATFORM_MACOS || NX_PLATFORM_IOS)
/// 1 on desktop development hosts (Windows, Linux, macOS), otherwise 0. @since 0.1
#define NX_PLATFORM_DESKTOP (NX_PLATFORM_WINDOWS || NX_PLATFORM_LINUX || NX_PLATFORM_MACOS)

// ---- Compiler ----------------------------------------------------------------------------------------------

/// 1 when the compiler is Microsoft's cl, otherwise 0. clang-cl counts as Clang. @since 0.1
#define NX_COMPILER_MSVC 0
/// 1 when the compiler is Clang in any driver mode (clang++, clang-cl, Apple Clang), otherwise 0. @since 0.1
#define NX_COMPILER_CLANG 0
/// 1 when the compiler is GCC, otherwise 0. @since 0.1
#define NX_COMPILER_GCC 0

#if defined(__clang__)
#undef NX_COMPILER_CLANG
#define NX_COMPILER_CLANG 1
#elif defined(_MSC_VER)
#undef NX_COMPILER_MSVC
#define NX_COMPILER_MSVC 1
#elif defined(__GNUC__)
#undef NX_COMPILER_GCC
#define NX_COMPILER_GCC 1
#else
#error "Nexyl: unsupported compiler."
#endif

/// 1 when the compiler follows the Microsoft C++ ABI and language extensions (cl, clang-cl), otherwise 0.
/// @since 0.1
#if defined(_MSC_VER)
#define NX_COMPILER_MSVC_ABI 1
#else
#define NX_COMPILER_MSVC_ABI 0
#endif

// ---- Architecture ------------------------------------------------------------------------------------------

/// 1 when compiling for x86-64, otherwise 0. @since 0.1
#define NX_ARCH_X64 0
/// 1 when compiling for 64-bit ARM, otherwise 0. @since 0.1
#define NX_ARCH_ARM64 0

#if defined(_M_ARM64) || defined(__aarch64__)
#undef NX_ARCH_ARM64
#define NX_ARCH_ARM64 1
#elif defined(_M_X64) || defined(__x86_64__)
#undef NX_ARCH_X64
#define NX_ARCH_X64 1
#else
#error "Nexyl: unsupported CPU architecture. Only x86-64 and ARM64 are supported."
#endif

// ---- Build configuration -----------------------------------------------------------------------------------

/// 1 when NX_ASSERT and its relatives are checked, otherwise 0.
/// Defaults to 1 in builds without NDEBUG; a build may define it explicitly to override the default.
/// @since 0.1
#if !defined(NX_ASSERTS_ENABLED)
#if defined(NDEBUG)
#define NX_ASSERTS_ENABLED 0
#else
#define NX_ASSERTS_ENABLED 1
#endif
#endif

/// 1 when Nexyl::Error records the source location where it was created, otherwise 0.
/// Defaults to 1 in builds without NDEBUG, so that shipping binaries contain no source paths.
/// It changes the layout of Error, so it is part of the Tier 1 build fingerprint (docs/PLUGIN_ABI.md, 5.1).
/// @since 0.1
#if !defined(NX_ERROR_LOCATIONS)
#if defined(NDEBUG)
#define NX_ERROR_LOCATIONS 0
#else
#define NX_ERROR_LOCATIONS 1
#endif
#endif

// ---- Utility macros ----------------------------------------------------------------------------------------

/// Pastes two tokens together after expanding them. @since 0.1
#define NX_CONCAT(a, b) NX_CONCAT_IMPL(a, b)
#define NX_CONCAT_IMPL(a, b) a##b

/// Turns the expansion of its argument into a string literal. @since 0.1
#define NX_STRINGIZE(x) NX_STRINGIZE_IMPL(x)
#define NX_STRINGIZE_IMPL(x) #x

/// Stops in an attached debugger. Without a debugger the process terminates. @since 0.1
#if NX_COMPILER_MSVC
extern "C" void __cdecl __debugbreak(void);
#pragma intrinsic(__debugbreak)
#define NX_DEBUG_BREAK() __debugbreak()
#elif NX_COMPILER_CLANG
#define NX_DEBUG_BREAK() __builtin_debugtrap()
#else
#define NX_DEBUG_BREAK() __builtin_trap()
#endif

/// Tells the optimiser that this point cannot be reached. Use NX_UNREACHABLE() instead, which also checks.
/// @since 0.1
#if NX_COMPILER_MSVC
#define NX_UNREACHABLE_HINT() __assume(0)
#else
#define NX_UNREACHABLE_HINT() __builtin_unreachable()
#endif
