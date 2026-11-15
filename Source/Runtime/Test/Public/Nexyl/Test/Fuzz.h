#pragma once

#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl::Test {

/// How a fuzz run mutates its inputs.
/// @threadsafety safe
/// @since 0.1
struct FuzzOptions {
    /// Seed of the mutation sequence; the same seed gives the same inputs on every platform (rule TEST-4).
    u64 seed = 1;
    /// Number of mutated inputs passed to the target, after the corpus itself.
    u32 iterations = 2000;
    /// Tokens that are inserted whole, such as `{`, `"` or `1e308` for a text format. May be empty.
    Span<const Span<const char>> dictionary;
};

/// Receives one input. It must not crash or assert, whatever the bytes: invalid input is an error result.
/// @since 0.1
using FuzzTarget = void (*)(const void* context, Span<const u8> input);

/// Runs a deterministic, mutation-based fuzz test (rule TEST-5).
///
/// Every corpus entry is passed to the target unchanged, then `options.iterations` inputs made from them by
/// mutations: flipped bits, replaced, inserted and deleted bytes, interesting values (0, 1, 0x7F, 0x80, 0xFF and
/// the extremes of 16-, 32- and 64-bit integers), duplicated ranges, splices of two entries, and dictionary
/// tokens. A crash or a failed assertion ends the test run; run the same seed again to reproduce it. Memory
/// errors show under the address sanitizer, which CI runs.
///
/// ```cpp
/// u8 scratch[4096];
/// Test::Fuzz(corpus, scratch, Test::FuzzOptions{.seed = 7}, [&](Span<const u8> input) {
///     static_cast<void>(ReadBinary(registry, input, object, allocator)); // must return, never crash
/// });
/// ```
/// @param corpus  Valid or interesting inputs to start from. At least one entry.
/// @param scratch Holds each mutated input; its size is the largest input. Borrowed for the duration of the call.
/// @param options The seed, the number of iterations and the dictionary.
/// @param target  Called once per input.
/// @param context Passed to `target`.
/// @return The number of inputs passed to the target.
/// @pre `corpus` is not empty and `scratch` is not empty.
/// @threadsafety compatible
/// @since 0.1
NX_TEST_API u32 Fuzz(Span<const Span<const u8>> corpus, Span<u8> scratch, const FuzzOptions& options, FuzzTarget target,
                     const void* context);

/// Runs a fuzz test with a callable target `void(Span<const u8>)`. See the other overload.
/// @param corpus  Valid or interesting inputs to start from. At least one entry.
/// @param scratch Holds each mutated input. Borrowed for the duration of the call.
/// @param options The seed, the number of iterations and the dictionary.
/// @param target  Called once per input.
/// @return The number of inputs passed to the target.
/// @threadsafety compatible
/// @since 0.1
template <typename F>
u32 Fuzz(Span<const Span<const u8>> corpus, Span<u8> scratch, const FuzzOptions& options, const F& target) {
    return Fuzz(
        corpus, scratch, options,
        [](const void* context, Span<const u8> input) { (*static_cast<const F*>(context))(input); }, &target);
}

} // namespace Nexyl::Test
