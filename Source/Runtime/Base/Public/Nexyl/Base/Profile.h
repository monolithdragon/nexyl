#pragma once

#include <Nexyl/Base/ProfileHook.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Std/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// Marks the lifetime of a local object as a named profile scope on the calling thread.
///
/// Costs one atomic load and a branch when no profiler is installed. Any module may profile; the Profile module
/// decides what is recorded.
///
/// ```cpp
/// void PhysicsWorld::Step() {
///     const ProfileScope scope("Physics.Step");
///     ...
/// }
/// ```
/// @threadsafety safe
/// @since 0.1
class ProfileScope final {
public:
    /// Opens the scope.
    /// @param name     A string literal naming the scope; kept by pointer.
    /// @param location Filled in by the compiler with the caller's location; never passed explicitly.
    /// @since 0.1
    template <usize N>
    explicit ProfileScope(const char (&name)[N], const std::source_location& location = std::source_location::current())
        : m_name(name), m_open(WriteProfileEvent(ProfileEvent{ProfileEventKind::ScopeBegin, name,
                                                              SourceLocation::Current(location), 0.0})) {}

    /// Closes the scope, if a profiler received its start. @since 0.1
    ~ProfileScope() {
        if (m_open) {
            static_cast<void>(WriteProfileEvent(ProfileEvent{ProfileEventKind::ScopeEnd, m_name, {}, 0.0}));
        }
    }

    ProfileScope(const ProfileScope&) = delete;
    ProfileScope& operator=(const ProfileScope&) = delete;
    ProfileScope(ProfileScope&&) = delete;
    ProfileScope& operator=(ProfileScope&&) = delete;

private:
    const char* m_name = "";
    bool m_open = false;
};

/// Records a new value of a named counter, such as memory in use or entities alive.
/// @param name     A string literal naming the counter; kept by pointer.
/// @param value    The value.
/// @param location Filled in by the compiler with the caller's location; never passed explicitly.
/// @threadsafety safe
/// @since 0.1
template <usize N>
void ProfileCounter(const char (&name)[N], f64 value,
                    const std::source_location& location = std::source_location::current()) {
    static_cast<void>(
        WriteProfileEvent(ProfileEvent{ProfileEventKind::Counter, name, SourceLocation::Current(location), value}));
}

/// Marks the end of a frame. The frame loop calls it once per frame.
/// @threadsafety safe
/// @since 0.1
inline void ProfileFrameMark() {
    static_cast<void>(WriteProfileEvent(ProfileEvent{ProfileEventKind::FrameMark, "Frame", {}, 0.0}));
}

/// Gives the calling thread a display name in profiles. Threads call it when they start.
/// @param name NUL-terminated UTF-8 text; copied, so it may be a temporary buffer.
/// @threadsafety safe
/// @since 0.1
inline void ProfileThreadName(const char* name) {
    static_cast<void>(WriteProfileEvent(ProfileEvent{ProfileEventKind::ThreadName, name, {}, 0.0}));
}

} // namespace Nexyl
