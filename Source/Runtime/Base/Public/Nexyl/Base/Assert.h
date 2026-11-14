#pragma once

#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// What kind of check failed.
/// @since 0.1
enum class AssertKind : u8 {
    /// An NX_ASSERT or NX_VERIFY condition was false.
    Assertion,
    /// An NX_UNREACHABLE point was reached.
    Unreachable,
    /// NX_FATAL was called.
    Fatal,
};

/// What the assertion handler wants to happen after a failed check.
/// @since 0.1
enum class AssertAction : u8 {
    /// Stop in the debugger, then continue. For NX_UNREACHABLE and NX_FATAL the process terminates afterwards.
    Break,
    /// Continue as if the check had passed. Ignored for NX_UNREACHABLE and NX_FATAL.
    Continue,
    /// Terminate the process immediately.
    Abort,
};

/// Description of a failed check, passed to the assertion handler.
/// @threadsafety safe
/// @since 0.1
struct AssertInfo {
    /// Which kind of check failed.
    AssertKind kind = AssertKind::Assertion;
    /// The failed condition as source text, or an empty string for NX_UNREACHABLE and NX_FATAL.
    const char* condition = "";
    /// The message given at the check, or an empty string.
    const char* message = "";
    /// Where the check is written.
    SourceLocation location;
};

/// Function called when a check fails. It may be called from any thread, concurrently.
/// @since 0.1
using AssertHandler = AssertAction (*)(const AssertInfo& info);

/// Installs the function that is called when a check fails.
///
/// Without a handler, every failed check terminates the process. Higher modules install a handler at
/// startup that reports the failure (docs/NEXYL_ARCHITECTURE.md, section 4.4).
///
/// @param handler The new handler, or nullptr to remove the current one.
/// @return The previously installed handler, or nullptr.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API AssertHandler SetAssertHandler(AssertHandler handler);

namespace Detail {

/// Reports a failed NX_ASSERT or NX_VERIFY. Terminates if the handler asks for it or there is no handler.
/// @return Break or Continue.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API AssertAction ReportAssertionFailure(const char* condition, const char* message,
                                                const SourceLocation& location);

/// Reports that an NX_UNREACHABLE point was reached, then terminates.
/// @threadsafety safe
/// @since 0.1
[[noreturn]] NX_BASE_API void ReportUnreachable(const SourceLocation& location);

/// Reports an NX_FATAL failure, then terminates.
/// @threadsafety safe
/// @since 0.1
[[noreturn]] NX_BASE_API void ReportFatal(const char* message, const SourceLocation& location);

} // namespace Detail

} // namespace Nexyl

/// Checks a precondition or invariant (docs/CODING_STANDARDS.md, section 5.5).
/// Not evaluated when NX_ASSERTS_ENABLED is 0, so the condition MUST NOT have side effects (rule ERR-9).
/// @since 0.1
#if NX_ASSERTS_ENABLED
#define NX_ASSERT(condition, message)                                                                                  \
    do {                                                                                                               \
        if (!(condition)) [[unlikely]] {                                                                               \
            if (::Nexyl::Detail::ReportAssertionFailure(#condition, message, NX_SOURCE_LOCATION) ==                    \
                ::Nexyl::AssertAction::Break) {                                                                        \
                NX_DEBUG_BREAK();                                                                                      \
            }                                                                                                          \
        }                                                                                                              \
    } while (false)
#else
#define NX_ASSERT(condition, message)                                                                                  \
    do {                                                                                                               \
        static_cast<void>(sizeof(!(condition)));                                                                       \
        static_cast<void>(sizeof(message));                                                                            \
    } while (false)
#endif

/// Like NX_ASSERT, but the expression is always evaluated, also when assertions are disabled.
/// For expressions with a needed side effect.
/// @since 0.1
#if NX_ASSERTS_ENABLED
#define NX_VERIFY(condition) NX_ASSERT(condition, "")
#else
#define NX_VERIFY(condition)                                                                                           \
    do {                                                                                                               \
        static_cast<void>(condition);                                                                                  \
    } while (false)
#endif

/// Marks a code path that cannot execute. Reports and terminates when assertions are enabled;
/// otherwise it is an optimiser hint.
/// @since 0.1
#if NX_ASSERTS_ENABLED
#define NX_UNREACHABLE() ::Nexyl::Detail::ReportUnreachable(NX_SOURCE_LOCATION)
#else
#define NX_UNREACHABLE() NX_UNREACHABLE_HINT()
#endif

/// Lets the optimiser assume that a condition holds. Checked like NX_ASSERT when assertions are enabled.
/// Use only with a measured benefit (docs/CODING_STANDARDS.md, section 5.5).
/// @since 0.1
#if NX_ASSERTS_ENABLED
#define NX_ASSUME(condition) NX_ASSERT(condition, "assumption violated")
#elif NX_COMPILER_MSVC
#define NX_ASSUME(condition) __assume(condition)
#elif NX_COMPILER_CLANG
#define NX_ASSUME(condition) __builtin_assume(condition)
#else
#define NX_ASSUME(condition) __attribute__((assume(condition)))
#endif

/// Reports an unrecoverable failure and terminates, in every build configuration.
/// @since 0.1
#define NX_FATAL(message) ::Nexyl::Detail::ReportFatal(message, NX_SOURCE_LOCATION)
