#include <Nexyl/Base/BaseErrors.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Test/Test.h>

#include "TestTypes.h"

using namespace Nexyl;
using Nexyl::Tests::MoveOnly;
using Nexyl::Tests::Tracked;

static_assert(ErrorEnum<BaseError>);
static_assert(!ErrorEnum<u16>);
static_assert(ErrorCode::From(BaseError::OutOfRange).value == ((1U << 16U) | 2U));
static_assert(ErrorCode::From(BaseError::OutOfRange).GetModule() == ModuleId::Base);
static_assert(ErrorCode::From(BaseError::OutOfRange).GetCode() == 2);

namespace {

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

} // namespace

NX_TEST(Error, CarriesCodeModuleAndDetail) {
    const Error error(BaseError::Unsupported, 42);
    NX_CHECK(error.Is(BaseError::Unsupported));
    NX_CHECK(!error.Is(BaseError::Overflow));
    NX_CHECK(error.GetModule() == ModuleId::Base);
    NX_CHECK(error.GetDetail() == 42);
    NX_CHECK(error.GetCode() == ErrorCode::From(BaseError::Unsupported));
}

NX_TEST(Error, RecordsTheCreationSite) {
    const u32 line = NX_SOURCE_LOCATION.line + 1;
    const Error error(BaseError::Overflow);
#if NX_ERROR_LOCATIONS
    // The location names the line that created the error, not a line inside Error.h.
    NX_CHECK(error.GetLocation().line == line);
#else
    NX_CHECK(error.GetLocation().line == 0);
    static_cast<void>(line);
#endif
}

NX_TEST(Result, HoldsValueOrError) {
    const Result<i32, Error> ok = Halve(8);
    NX_CHECK(ok.IsOk());
    NX_CHECK(!ok.IsError());
    NX_CHECK(ok.GetValue() == 4);

    const Result<i32, Error> failed = Halve(3);
    NX_CHECK(failed.IsError());
    NX_CHECK(failed.GetError().Is(BaseError::InvalidArgument));
    NX_CHECK(failed.GetError().GetDetail() == 3);
    NX_CHECK(failed.ValueOr(-1) == -1);
}

NX_TEST(Result, TryPropagatesTheFirstError) {
    NX_CHECK(Quarter(12).GetValue() == 3);
    NX_CHECK(Quarter(6).IsError());
    NX_CHECK(Quarter(6).GetError().GetDetail() == 3);
    NX_CHECK(RequireEven(4).IsOk());
    NX_CHECK(RequireEven(5).IsError());
}

NX_TEST(Result, SupportsMoveOnlyValues) {
    Result<MoveOnly, Error> moveOnly = MakeMoveOnly(true);
    NX_REQUIRE(moveOnly.IsOk());
    NX_CHECK(Move(moveOnly).TakeValue().value == 11);
    NX_CHECK(MakeMoveOnly(false).GetError().Is(BaseError::Unsupported));
}

NX_TEST(Result, DestroysItsContentExactlyOnce) {
    {
        Result<Tracked, Error> tracked = Tracked(4);
        NX_CHECK(Tracked::s_alive == 1);
        Result<Tracked, Error> copy = tracked;
        NX_CHECK(Tracked::s_alive == 2);
        copy = Error(BaseError::Overflow);
        NX_CHECK(Tracked::s_alive == 1);
        NX_CHECK(copy.IsError());
        copy = Move(tracked);
        NX_CHECK(copy.IsOk());
        NX_CHECK(copy.GetValue().value == 4);
        NX_CHECK(Tracked::s_alive == 2);
    }
    NX_CHECK(Tracked::s_alive == 0);

    NX_IGNORE_RESULT(Halve(1)); // Deliberately discarded: exercises the macro.
}
