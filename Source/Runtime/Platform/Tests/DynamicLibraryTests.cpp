#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Platform/DynamicLibrary.h>
#include <Nexyl/Platform/PlatformErrors.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Platform;

// A system library that always exists and exports a function returning the process ID (never zero).
namespace {

#if NX_PLATFORM_WINDOWS
constexpr const char* kSystemLibrary = "kernel32.dll";
constexpr const char* kProcessIdFunction = "GetCurrentProcessId";
using ProcessIdFunction = u32 (*)();
#elif NX_PLATFORM_APPLE
constexpr const char* kSystemLibrary = "/usr/lib/libSystem.B.dylib";
constexpr const char* kProcessIdFunction = "getpid";
using ProcessIdFunction = i32 (*)();
#elif NX_PLATFORM_ANDROID
constexpr const char* kSystemLibrary = "libc.so";
constexpr const char* kProcessIdFunction = "getpid";
using ProcessIdFunction = i32 (*)();
#else
constexpr const char* kSystemLibrary = "libc.so.6";
constexpr const char* kProcessIdFunction = "getpid";
using ProcessIdFunction = i32 (*)();
#endif

} // namespace

NX_TEST(DynamicLibrary, LoadsASystemLibraryAndCallsAFunction) {
    Result<DynamicLibrary, Error> opened = DynamicLibrary::Open(kSystemLibrary);
    NX_REQUIRE(opened.IsOk());
    DynamicLibrary library = Move(opened).TakeValue();
    NX_CHECK(library.IsOpen());

    NX_CHECK(library.FindSymbol(kProcessIdFunction).HasValue());
    const Option<ProcessIdFunction> function = library.FindFunction<ProcessIdFunction>(kProcessIdFunction);
    NX_REQUIRE(function.HasValue());
    NX_CHECK(function.GetValue()() != 0);

    NX_CHECK(!library.FindSymbol("NexylSymbolThatDoesNotExist").HasValue());

    library.Close();
    NX_CHECK(!library.IsOpen());
}

NX_TEST(DynamicLibrary, MissingLibraryIsNotFound) {
    const Result<DynamicLibrary, Error> opened = DynamicLibrary::Open("NexylLibraryThatDoesNotExist.bin");
    NX_REQUIRE(opened.IsError());
    NX_CHECK(opened.GetError().Is(PlatformError::NotFound));
}

NX_TEST(DynamicLibrary, ReportsThePlatformExtension) {
    const char* const extension = DynamicLibrary::GetFileExtension();
    NX_CHECK(extension[0] == '.');
    NX_CHECK(extension[1] == 'd' || extension[1] == 's');
}
