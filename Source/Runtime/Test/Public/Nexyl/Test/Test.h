#pragma once

#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/SourceLocation.h>

namespace Nexyl::Test::Detail {

/// Records a failed check in the running test and reports it. Called by NX_CHECK and NX_REQUIRE.
///
/// @param expression The failed condition as source text.
/// @param location   Where the check is written.
/// @pre A test is running on the calling thread; terminates otherwise.
/// @threadsafety main
/// @since 0.1
NX_TEST_API void ReportCheckFailure(const char* expression, const SourceLocation& location);

} // namespace Nexyl::Test::Detail

/// Defines a unit test (docs/CODING_STANDARDS.md, section 19).
///
/// The build finds every NX_TEST in a module's Tests/ sources and generates the test table and main(), so
/// nothing else has to be written. `suite` and `name` are PascalCase identifiers; the pair is unique within the
/// module. NX_TEST MUST NOT appear inside conditional compilation: skip work inside the test body instead.
///
/// ```cpp
/// NX_TEST(Option, ValueOrReturnsFallbackWhenEmpty) {
///     const Option<i32> empty;
///     NX_CHECK(empty.ValueOr(3) == 3);
/// }
/// ```
/// @since 0.1
#define NX_TEST(suite, name)                                                                                           \
    void NX_CONCAT(NX_CONCAT(NxTest, suite), name)();                                                                  \
    void NX_CONCAT(NX_CONCAT(NxTest, suite), name)()

/// Checks a condition. On failure the test is marked failed, the condition is reported, and the test continues.
/// Call it only from the thread that runs the test.
/// @since 0.1
#define NX_CHECK(condition)                                                                                            \
    do {                                                                                                               \
        if (!(condition)) [[unlikely]] {                                                                               \
            ::Nexyl::Test::Detail::ReportCheckFailure(#condition, NX_SOURCE_LOCATION);                                 \
        }                                                                                                              \
    } while (false)

/// Like NX_CHECK, but on failure it also returns from the current function. Use it where continuing would be
/// meaningless or unsafe. Only valid in functions that return void.
/// @since 0.1
#define NX_REQUIRE(condition)                                                                                          \
    do {                                                                                                               \
        if (!(condition)) [[unlikely]] {                                                                               \
            ::Nexyl::Test::Detail::ReportCheckFailure(#condition, NX_SOURCE_LOCATION);                                 \
            return;                                                                                                    \
        }                                                                                                              \
    } while (false)
