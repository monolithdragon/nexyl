#include <Nexyl/Base/Profile.h>
#include <Nexyl/Base/ProfileHook.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;

namespace {

struct Captured {
    i32 count = 0;
    ProfileEventKind kinds[8] = {};
    const char* names[8] = {};
    f64 values[8] = {};
    u32 lines[8] = {};
};

void Capture(void* context, const ProfileEvent& event) {
    auto& captured = *static_cast<Captured*>(context);
    if (captured.count < 8) {
        captured.kinds[captured.count] = event.kind;
        captured.names[captured.count] = event.name;
        captured.values[captured.count] = event.value;
        captured.lines[captured.count] = event.location.line;
    }
    ++captured.count;
}

} // namespace

NX_TEST(ProfileHook, ScopesEmitBeginAndEnd) {
    Captured captured;
    const ProfileSinkBinding binding{&Capture, &captured};
    const ProfileSinkBinding* const previous = SetProfileSink(&binding);
    u32 line = 0;
    {
        line = SourceLocation::Current().line + 1;
        const ProfileScope scope("Test.Scope");
    }
    NX_REQUIRE(captured.count == 2);
    NX_CHECK(captured.kinds[0] == ProfileEventKind::ScopeBegin);
    NX_CHECK(captured.kinds[1] == ProfileEventKind::ScopeEnd);
    NX_CHECK(captured.names[0] == captured.names[1]);
    NX_CHECK(captured.lines[0] == line);
    static_cast<void>(SetProfileSink(previous));
}

NX_TEST(ProfileHook, CountersFramesAndThreadNames) {
    Captured captured;
    const ProfileSinkBinding binding{&Capture, &captured};
    const ProfileSinkBinding* const previous = SetProfileSink(&binding);
    ProfileCounter("Test.Counter", 2.5);
    ProfileFrameMark();
    ProfileThreadName("Main");
    NX_REQUIRE(captured.count == 3);
    NX_CHECK(captured.kinds[0] == ProfileEventKind::Counter);
    NX_CHECK(captured.values[0] == 2.5);
    NX_CHECK(captured.kinds[1] == ProfileEventKind::FrameMark);
    NX_CHECK(captured.kinds[2] == ProfileEventKind::ThreadName);
    NX_CHECK(captured.names[2][0] == 'M');
    static_cast<void>(SetProfileSink(previous));
}

NX_TEST(ProfileHook, AScopeOpenedWithoutABindingDoesNotClose) {
    Captured captured;
    const ProfileSinkBinding binding{&Capture, &captured};
    const ProfileSinkBinding* const previous = SetProfileSink(nullptr);
    {
        const ProfileScope scope("Test.Unseen");
        static_cast<void>(SetProfileSink(&binding));
    }
    // The receiver never saw the start, so it gets no end either.
    NX_CHECK(captured.count == 0);
    NX_CHECK(WriteProfileEvent(ProfileEvent{}));
    NX_CHECK(captured.count == 1);
    static_cast<void>(SetProfileSink(nullptr));
    NX_CHECK(!WriteProfileEvent(ProfileEvent{}));
    static_cast<void>(SetProfileSink(previous));
}
