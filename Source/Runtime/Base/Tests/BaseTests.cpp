// Unit tests for the Base module.
//
// TODO(phase-1): move these onto the engine's Test module once it exists. Until then this file is the
// module's only test source: it counts failed checks and returns the count as the exit code.

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/BaseErrors.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Base/Version.h>

namespace {

using namespace Nexyl;

i32 g_failures = 0;

void Check(bool condition) {
    if (!condition) {
        ++g_failures;
    }
}

// ---- Config -------------------------------------------------------------------------------------------------

static_assert(NX_PLATFORM_WINDOWS + NX_PLATFORM_LINUX + NX_PLATFORM_ANDROID + NX_PLATFORM_MACOS + NX_PLATFORM_IOS == 1,
              "Exactly one platform must be detected.");
static_assert(NX_COMPILER_MSVC + NX_COMPILER_CLANG + NX_COMPILER_GCC == 1, "Exactly one compiler must be detected.");
static_assert(NX_ARCH_X64 + NX_ARCH_ARM64 == 1, "Exactly one architecture must be detected.");

// ---- Utility ------------------------------------------------------------------------------------------------

/// Counts how often objects of this type are constructed and destroyed, to prove that the wrappers neither
/// leak nor double-destroy their contents.
struct Tracked {
    static i32 s_alive;

    i32 value = 0;

    explicit Tracked(i32 initial) : value(initial) {
        ++s_alive;
    }
    Tracked(const Tracked& other) : value(other.value) {
        ++s_alive;
    }
    Tracked(Tracked&& other) noexcept : value(other.value) {
        other.value = -1;
        ++s_alive;
    }
    Tracked& operator=(const Tracked& other) = default;
    Tracked& operator=(Tracked&& other) noexcept {
        value = other.value;
        other.value = -1;
        return *this;
    }
    ~Tracked() {
        --s_alive;
    }
};
i32 Tracked::s_alive = 0;

/// A type that can be moved but not copied.
struct MoveOnly {
    i32 value = 0;

    explicit MoveOnly(i32 initial) : value(initial) {}
    MoveOnly(const MoveOnly&) = delete;
    MoveOnly& operator=(const MoveOnly&) = delete;
    MoveOnly(MoveOnly&& other) noexcept : value(Exchange(other.value, 0)) {}
    MoveOnly& operator=(MoveOnly&& other) noexcept {
        value = Exchange(other.value, 0);
        return *this;
    }
    ~MoveOnly() = default;
};

void TestUtility() {
    i32 first = 1;
    i32 second = 2;
    Swap(first, second);
    Check(first == 2 && second == 1);

    i32 target = 5;
    const i32 previous = Exchange(target, 9);
    Check(previous == 5 && target == 9);

    MoveOnly source(7);
    const MoveOnly destination(Move(source));
    Check(destination.value == 7 && source.value == 0);
}

// ---- Errors -------------------------------------------------------------------------------------------------

static_assert(ErrorEnum<BaseError>);
static_assert(!ErrorEnum<u16>);
static_assert(ErrorCode::From(BaseError::OutOfRange).value == ((1U << 16U) | 2U));
static_assert(ErrorCode::From(BaseError::OutOfRange).GetModule() == ModuleId::Base);
static_assert(ErrorCode::From(BaseError::OutOfRange).GetCode() == 2);

void TestError() {
    const u32 line = NX_SOURCE_LOCATION.line + 1;
    const Error error(BaseError::Unsupported, 42);

    Check(error.Is(BaseError::Unsupported));
    Check(!error.Is(BaseError::Overflow));
    Check(error.GetModule() == ModuleId::Base);
    Check(error.GetDetail() == 42);
    Check(error.GetCode() == ErrorCode::From(BaseError::Unsupported));

#if NX_ERROR_LOCATIONS
    // The location names the line that created the error, not a line inside Error.h.
    Check(error.GetLocation().line == line);
#else
    Check(error.GetLocation().line == 0);
    static_cast<void>(line);
#endif
}

// ---- Option -------------------------------------------------------------------------------------------------

static_assert(!Option<i32>().HasValue());
static_assert(Option<i32>(3).GetValue() == 3);
static_assert(Option<i32>(kNone).ValueOr(8) == 8);

Option<i32> FindFirstEven(Span<const i32> values) {
    for (const i32 value : values) {
        if (value % 2 == 0) {
            return value;
        }
    }
    return kNone;
}

void TestOption() {
    const i32 values[] = {1, 3, 4, 6};
    const i32 odd[] = {1, 3};
    Check(FindFirstEven(values).ValueOr(0) == 4);
    Check(!FindFirstEven(odd).HasValue());

    {
        Option<Tracked> option(Tracked(1));
        Check(Tracked::s_alive == 1);

        const Option<Tracked> copy(option);
        Check(Tracked::s_alive == 2 && copy.GetValue().value == 1);

        option.Emplace(5);
        Check(Tracked::s_alive == 2 && option.GetValue().value == 5);

        option.Reset();
        Check(!option.HasValue() && Tracked::s_alive == 1);

        option = copy;
        Check(option.HasValue() && option.GetValue().value == 1 && Tracked::s_alive == 2);
    }
    Check(Tracked::s_alive == 0);

    Option<MoveOnly> moveOnly(MoveOnly(3));
    const MoveOnly taken = Move(moveOnly).TakeValue();
    Check(taken.value == 3);
}

// ---- Result -------------------------------------------------------------------------------------------------

Result<i32, Error> Halve(i32 value) {
    if (value % 2 != 0) {
        return Error(BaseError::InvalidArgument, static_cast<u32>(value));
    }
    return value / 2;
}

Result<i32, Error> Quarter(i32 value) {
    NX_TRY_ASSIGN(const i32 half, Halve(value));
    NX_TRY_ASSIGN(const i32 quarter, Halve(half));
    return quarter;
}

Result<void, Error> RequireEven(i32 value) {
    NX_TRY(Halve(value));
    return {};
}

Result<MoveOnly, Error> MakeMoveOnly(bool succeed) {
    if (!succeed) {
        return Error(BaseError::Unsupported);
    }
    return MoveOnly(11);
}

void TestResult() {
    const Result<i32, Error> ok = Halve(8);
    Check(ok.IsOk() && !ok.IsError() && ok.GetValue() == 4);

    const Result<i32, Error> failed = Halve(3);
    Check(failed.IsError() && failed.GetError().Is(BaseError::InvalidArgument));
    Check(failed.GetError().GetDetail() == 3);
    Check(failed.ValueOr(-1) == -1);

    Check(Quarter(12).GetValue() == 3);
    Check(Quarter(6).IsError() && Quarter(6).GetError().GetDetail() == 3);

    Check(RequireEven(4).IsOk());
    Check(RequireEven(5).IsError());

    Result<MoveOnly, Error> moveOnly = MakeMoveOnly(true);
    Check(moveOnly.IsOk() && Move(moveOnly).TakeValue().value == 11);
    Check(MakeMoveOnly(false).GetError().Is(BaseError::Unsupported));

    {
        Result<Tracked, Error> tracked = Tracked(4);
        Check(Tracked::s_alive == 1);
        Result<Tracked, Error> copy = tracked;
        Check(Tracked::s_alive == 2);
        copy = Error(BaseError::Overflow);
        Check(Tracked::s_alive == 1 && copy.IsError());
        copy = Move(tracked);
        Check(copy.IsOk() && copy.GetValue().value == 4 && Tracked::s_alive == 2);
    }
    Check(Tracked::s_alive == 0);

    NX_IGNORE_RESULT(Halve(1)); // Deliberately discarded: exercises the macro.
}

// ---- Span ---------------------------------------------------------------------------------------------------

/// A minimal contiguous container, standing in for Array until Containers exists.
struct FixedBuffer {
    i32 items[3] = {4, 5, 6};

    [[nodiscard]] i32* Data() {
        return items;
    }
    [[nodiscard]] usize Size() const {
        return 3;
    }
};

i32 Sum(Span<const i32> values) {
    i32 total = 0;
    for (const i32 value : values) {
        total += value;
    }
    return total;
}

void TestSpan() {
    i32 values[] = {1, 2, 3, 4, 5};
    const Span<i32> all(values);
    Check(all.Size() == 5 && all.SizeInBytes() == 5 * sizeof(i32) && !all.IsEmpty());
    Check(Sum(all) == 15);
    Check(Sum(all.First(2)) == 3);
    Check(Sum(all.Last(2)) == 9);
    Check(Sum(all.Subspan(1, 3)) == 9);
    Check(Sum(all.Subspan(4)) == 5);
    Check(all.Subspan(5).IsEmpty());

    all[0] = 10;
    Check(values[0] == 10);

    FixedBuffer buffer;
    Check(Sum(buffer) == 15);

    const Span<i32> empty;
    Check(empty.IsEmpty() && empty.Data() == nullptr && Sum(empty) == 0);
}

// ---- Hooks --------------------------------------------------------------------------------------------------

AssertInfo g_lastAssert;
i32 g_assertCount = 0;

AssertAction RecordAssert(const AssertInfo& info) {
    g_lastAssert = info;
    ++g_assertCount;
    return AssertAction::Continue;
}

LogRecord g_lastLog;
i32 g_logCount = 0;

void RecordLog(const LogRecord& record) {
    g_lastLog = record;
    ++g_logCount;
}

void TestHooks() {
    const AssertHandler previousHandler = SetAssertHandler(&RecordAssert);
    Check(previousHandler == nullptr);

    NX_ASSERT(1 + 1 == 2, "never fires");
    Check(g_assertCount == 0);

#if NX_ASSERTS_ENABLED
    const u32 line = NX_SOURCE_LOCATION.line + 1;
    NX_ASSERT(g_assertCount < 0, "expected failure");
    Check(g_assertCount == 1);
    Check(g_lastAssert.kind == AssertKind::Assertion);
    Check(g_lastAssert.location.line == line);
    Check(g_lastAssert.message[0] == 'e');
    Check(g_lastAssert.condition[0] == 'g');
#endif

    i32 sideEffects = 0;
    NX_VERIFY(++sideEffects == 1);
    Check(sideEffects == 1);

    Check(SetAssertHandler(previousHandler) == &RecordAssert);

    Check(SetLogSink(&RecordLog) == nullptr);
    const char text[] = "hello";
    WriteLog(LogRecord{LogLevel::Warning, text, 5, NX_SOURCE_LOCATION});
    Check(g_logCount == 1 && g_lastLog.level == LogLevel::Warning && g_lastLog.length == 5);
    Check(SetLogSink(nullptr) == &RecordLog);

    WriteLog(LogRecord{LogLevel::Info, text, 5, NX_SOURCE_LOCATION});
    Check(g_logCount == 1);
}

// ---- Version ------------------------------------------------------------------------------------------------

void TestVersion() {
    const Version version = GetEngineVersion();
    Check(version.major == NX_ENGINE_VERSION_MAJOR && version.minor == NX_ENGINE_VERSION_MINOR &&
          version.patch == NX_ENGINE_VERSION_PATCH);
}

} // namespace

int main() {
    TestUtility();
    TestError();
    TestOption();
    TestResult();
    TestSpan();
    TestHooks();
    TestVersion();
    return g_failures;
}
