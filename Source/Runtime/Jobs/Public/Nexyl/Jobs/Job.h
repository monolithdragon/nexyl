#pragma once

#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Containers/Handle.h>

namespace Nexyl::Jobs {

class JobSystem;

namespace Detail {

/// One job slot of a JobSystem. Defined in the implementation. @since 0.1
struct JobSlot;

} // namespace Detail

/// Identifies a scheduled job. A null handle, or the handle of a finished job, counts as done.
///
/// The generation detects a stale handle: once the job finishes, its slot gets a new generation and the handle
/// no longer matches.
/// @since 0.1
using JobHandle = Containers::Handle<Detail::JobSlot>;

/// How urgently a job runs. Workers take higher priorities first (docs/NEXYL_ARCHITECTURE.md, section 6.2).
/// @since 0.1
enum class JobPriority : u8 {
    /// Frame-critical work.
    High,
    /// Everything else that belongs to the frame.
    Normal,
    /// Work that may slip to a later frame.
    Low,
    /// Long-running work such as asset processing.
    Background,
};

/// The number of JobPriority values.
/// @since 0.1
inline constexpr usize kJobPriorityCount = 4;

/// The largest callable a job stores inline, in bytes. Larger data lives in frame or tick memory and the job
/// captures a pointer to it.
/// @since 0.1
inline constexpr usize kJobPayloadSize = 48;

/// The strictest alignment of a callable a job stores inline.
/// @since 0.1
inline constexpr usize kJobPayloadAlignment = 8;

/// What a running job knows about itself.
/// @threadsafety safe
/// @since 0.1
struct JobContext {
    /// The system that runs the job. Use it to schedule children.
    JobSystem* system = nullptr;
    /// The running job. Pass it as the parent of children that must finish before it counts as done. Null for a
    /// job that ran inline because the job pool was full.
    JobHandle job;
};

/// Satisfied by a callable that a job can store and call: `void(const JobContext&)`, trivially copyable and
/// destructible, at most kJobPayloadSize bytes and kJobPayloadAlignment aligned. A lambda that captures pointers
/// and numbers by value qualifies.
/// @since 0.1
template <typename F>
concept JobCallable =
    TriviallyCopyable<F> && TriviallyDestructible<F> && sizeof(F) <= kJobPayloadSize &&
    alignof(F) <= kJobPayloadAlignment && requires(const F& function, const JobContext& context) { function(context); };

/// Satisfied by a ParallelFor body: `void(u32 batchIndex, u32 begin, u32 end)`, callable concurrently on a
/// `const` object.
/// @since 0.1
template <typename F>
concept ParallelForBody = requires(const F& body, u32 batchIndex, u32 begin, u32 end) { body(batchIndex, begin, end); };

} // namespace Nexyl::Jobs
