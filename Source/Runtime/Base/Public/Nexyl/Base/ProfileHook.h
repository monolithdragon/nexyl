#pragma once

#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// What a profile event marks.
/// @since 0.1
enum class ProfileEventKind : u8 {
    /// A named scope starts on the calling thread.
    ScopeBegin,
    /// The innermost open scope of the calling thread ends.
    ScopeEnd,
    /// A named counter has a new value.
    Counter,
    /// A frame ends. Emitted once per frame by the frame loop.
    FrameMark,
    /// The calling thread gets a display name.
    ThreadName,
};

/// One profile event. The receiver adds the time and the thread.
/// @threadsafety safe
/// @since 0.1
/// @opaque
struct ProfileEvent {
    /// What the event marks.
    ProfileEventKind kind = ProfileEventKind::ScopeBegin;
    /// Scope or counter name: a string literal, kept by pointer and valid while the module that wrote it is
    /// loaded. For ProfileEventKind::ThreadName, any NUL-terminated text, copied by the receiver.
    const char* name = "";
    /// Where the event was written. Empty for ScopeEnd.
    SourceLocation location;
    /// The value of a counter; zero otherwise.
    f64 value = 0.0;
};

/// The function that receives profile events, with the context it needs.
/// @threadsafety safe
/// @since 0.1
/// @opaque
struct ProfileSinkBinding {
    /// Receives one event. It may be called from any thread, concurrently, and must be fast: it runs inside
    /// every instrumented scope.
    void (*write)(void* context, const ProfileEvent& event) = nullptr;
    /// Passed unchanged to `write`.
    void* context = nullptr;
};

/// Installs the binding that receives profile events.
///
/// Base only forwards events; timing, buffering and output belong to the Profile module, which installs its
/// binding at startup (docs/NEXYL_ARCHITECTURE.md, sections 4.4 and 5.9). Without a binding, events are dropped.
///
/// Unlike the log hook, the replacement does not wait for calls in progress: an event costs one atomic load, not
/// a shared counter. The caller replaces or removes a binding only when no other thread can emit events, as the
/// startup and shutdown order guarantees.
///
/// @param binding The new binding, or nullptr to remove the current one. It must stay valid until it is replaced.
/// @return The previously installed binding, or nullptr.
/// @pre `binding` is nullptr or has a `write` function.
/// @pre No other thread is emitting profile events.
/// @threadsafety main
/// @since 0.1
NX_BASE_API const ProfileSinkBinding* SetProfileSink(const ProfileSinkBinding* binding);

/// Passes an event to the installed binding.
/// @param event The event. Borrowed for the duration of the call.
/// @return True if a binding received it, false if none is installed.
/// @threadsafety safe
/// @since 0.1
NX_BASE_API bool WriteProfileEvent(const ProfileEvent& event);

} // namespace Nexyl
