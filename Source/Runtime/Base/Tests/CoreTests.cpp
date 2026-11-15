#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Base/Version.h>
#include <Nexyl/Test/Test.h>

#include "TestTypes.h"

using namespace Nexyl;
using Nexyl::Tests::MoveOnly;

static_assert(NX_PLATFORM_WINDOWS + NX_PLATFORM_LINUX + NX_PLATFORM_ANDROID + NX_PLATFORM_MACOS + NX_PLATFORM_IOS == 1,
              "Exactly one platform must be detected.");
static_assert(NX_COMPILER_MSVC + NX_COMPILER_CLANG + NX_COMPILER_GCC == 1, "Exactly one compiler must be detected.");
static_assert(NX_ARCH_X64 + NX_ARCH_ARM64 == 1, "Exactly one architecture must be detected.");

NX_TEST(Utility, SwapExchangesValues) {
    i32 first = 1;
    i32 second = 2;
    Swap(first, second);
    NX_CHECK(first == 2);
    NX_CHECK(second == 1);
}

NX_TEST(Utility, ExchangeReturnsOldValue) {
    i32 target = 5;
    const i32 previous = Exchange(target, 9);
    NX_CHECK(previous == 5);
    NX_CHECK(target == 9);
}

NX_TEST(Utility, MoveTransfersOwnership) {
    MoveOnly source(7);
    const MoveOnly destination(Move(source));
    NX_CHECK(destination.value == 7);
    NX_CHECK(source.value == 0);
}

NX_TEST(Version, MatchesTheBuild) {
    const Version version = GetEngineVersion();
    NX_CHECK(version.major == NX_ENGINE_VERSION_MAJOR);
    NX_CHECK(version.minor == NX_ENGINE_VERSION_MINOR);
    NX_CHECK(version.patch == NX_ENGINE_VERSION_PATCH);
}
