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
constinit std::atomic<const LogSinkBinding*> g_logSink{nullptr};
constinit std::atomic<LogLevel> g_logMinimumLevel{LogLevel::Trace};
// WriteLog calls between taking the binding and returning from it. SetLogSink waits for zero after the exchange,
// so the replaced binding is no longer in use when it returns. Sequentially consistent on both sides: a writer
// either increments before the exchange (and is waited for) or loads the new binding.
constinit std::atomic<u64> g_logWritesInFlight{0};

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

const LogSinkBinding* SetLogSink(const LogSinkBinding* binding) {
    NX_ASSERT(binding == nullptr || binding->write != nullptr, "A log sink binding needs a write function.");
    const LogSinkBinding* const previous = g_logSink.exchange(binding, std::memory_order_seq_cst);
    // Base cannot yield to the operating system; replacing the binding is rare and writes are short.
    while (g_logWritesInFlight.load(std::memory_order_seq_cst) != 0) {}
    return previous;
}

void SetLogMinimumLevel(LogLevel level) {
    g_logMinimumLevel.store(level, std::memory_order_relaxed);
}

LogLevel GetLogMinimumLevel() {
    return g_logMinimumLevel.load(std::memory_order_relaxed);
}

bool IsLogEnabled(LogLevel level) {
    NX_ASSERT(level != LogLevel::Off, "LogLevel::Off is not a record level.");
    return level >= g_logMinimumLevel.load(std::memory_order_relaxed) &&
           g_logSink.load(std::memory_order_relaxed) != nullptr;
}

void WriteLog(const LogRecord& record) {
    NX_ASSERT(record.level != LogLevel::Off, "LogLevel::Off is not a record level.");
    if (record.level < g_logMinimumLevel.load(std::memory_order_relaxed)) {
        return;
    }
    g_logWritesInFlight.fetch_add(1, std::memory_order_seq_cst);
    const LogSinkBinding* const binding = g_logSink.load(std::memory_order_seq_cst);
    if (binding != nullptr) {
        binding->write(binding->context, record);
    }
    g_logWritesInFlight.fetch_sub(1, std::memory_order_seq_cst);
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
