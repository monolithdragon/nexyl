#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/Std/Atomic.h>

#if NX_COMPILER_MSVC
extern "C" __declspec(noreturn) void __fastfail(unsigned int code);
#pragma intrinsic(__fastfail)
#endif

namespace Nexyl {

namespace {

// Constant-initialized and trivially destructible: no static constructor or destructor runs (rule INIT-1).
constinit std::atomic<AssertHandler> g_assertHandler{nullptr};
constinit std::atomic<LogSink> g_logSink{nullptr};

// Ends the process without calling into the operating system or the C runtime.
[[noreturn]] void Terminate() {
#if NX_COMPILER_MSVC
    constexpr unsigned int kFastFailFatalAppExit = 7;
    __fastfail(kFastFailFatalAppExit);
#else
    __builtin_trap();
#endif
}

AssertAction CallHandler(const AssertInfo& info) {
    const AssertHandler handler = g_assertHandler.load(std::memory_order_acquire);
    return handler != nullptr ? handler(info) : AssertAction::Abort;
}

} // namespace

AssertHandler SetAssertHandler(AssertHandler handler) {
    return g_assertHandler.exchange(handler, std::memory_order_acq_rel);
}

LogSink SetLogSink(LogSink sink) {
    return g_logSink.exchange(sink, std::memory_order_acq_rel);
}

void WriteLog(const LogRecord& record) {
    const LogSink sink = g_logSink.load(std::memory_order_acquire);
    if (sink != nullptr) {
        sink(record);
    }
}

namespace Detail {

AssertAction ReportAssertionFailure(const char* condition, const char* message, const SourceLocation& location) {
    const AssertAction action = CallHandler(AssertInfo{AssertKind::Assertion, condition, message, location});
    if (action == AssertAction::Abort) {
        Terminate();
    }
    return action;
}

void ReportUnreachable(const SourceLocation& location) {
    if (CallHandler(AssertInfo{AssertKind::Unreachable, "", "unreachable code reached", location}) ==
        AssertAction::Break) {
        NX_DEBUG_BREAK();
    }
    Terminate();
}

void ReportFatal(const char* message, const SourceLocation& location) {
    if (CallHandler(AssertInfo{AssertKind::Fatal, "", message, location}) == AssertAction::Break) {
        NX_DEBUG_BREAK();
    }
    Terminate();
}

} // namespace Detail

} // namespace Nexyl
