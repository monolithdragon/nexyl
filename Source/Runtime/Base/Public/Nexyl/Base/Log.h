#pragma once

#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/LogHook.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Std/SourceLocation.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// The largest formatted message in bytes. A longer message is cut and ends with `...`.
/// @since 0.1
inline constexpr usize kMaxLogMessageSize = 512;

/// A format string for a log call, checked at compile time, together with the place of the call.
///
/// It converts implicitly from a string literal and records the caller's source location, so the log functions
/// need no macro. See FormatString for the syntax.
/// @threadsafety safe
/// @since 0.1
template <typename... T>
class LogFormat final {
public:
    /// Checks `text` against the argument types and records where the call is.
    ///
    /// Implicit by design, like FormatString (the documented exception to rule CLS-6).
    /// @param text     The format string literal.
    /// @param location Filled in by the compiler with the caller's location; never passed explicitly.
    /// @since 0.1
    template <usize N>
    consteval explicit(false)
        LogFormat(const char (&text)[N], const std::source_location& location = std::source_location::current())
        : m_format(text), m_location(SourceLocation::Current(location)) {}

    /// Returns the checked format string.
    /// @return The format string.
    /// @since 0.1
    [[nodiscard]] constexpr const FormatString<T...>& GetFormat() const {
        return m_format;
    }

    /// Returns the place of the log call.
    /// @return The caller's source location.
    /// @since 0.1
    [[nodiscard]] constexpr const SourceLocation& GetLocation() const {
        return m_location;
    }

private:
    FormatString<T...> m_format;
    SourceLocation m_location;
};

namespace Detail {

/// Formats a message and passes it to WriteLog. Internal; the log functions call it.
/// @since 0.1
NX_BASE_API void WriteFormattedLog(LogLevel level, const char* text, usize size, Span<const FormatArg> args,
                                   const SourceLocation& location);

/// Packs the arguments and calls WriteFormattedLog if the level is enabled. Internal. @since 0.1
template <typename... T>
void LogMessage(LogLevel level, const LogFormat<TypeIdentity<T>...>& format, const T&... args) {
    if (!IsLogEnabled(level)) {
        return;
    }
    const FormatArg packed[] = {MakeFormatArg(args)..., FormatArg{}};
    WriteFormattedLog(level, format.GetFormat().Data(), format.GetFormat().Size(),
                      Span<const FormatArg>(packed, sizeof...(T)), format.GetLocation());
}

} // namespace Detail

/// Logs a message at LogLevel::Trace.
///
/// The message is formatted only if the level is enabled (IsLogEnabled), into a buffer on the stack of at most
/// kMaxLogMessageSize bytes; nothing is allocated. Any module may log; the Log module decides where records go.
///
/// ```cpp
/// LogInfo("Loaded {} assets in {:.1f} ms", count, milliseconds);
/// ```
/// @param format The format string, checked at compile time, with the caller's location.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void LogTrace(LogFormat<TypeIdentity<T>...> format, const T&... args) {
    Detail::LogMessage<T...>(LogLevel::Trace, format, args...);
}

/// Logs a message at LogLevel::Debug. See LogTrace.
/// @param format The format string, checked at compile time, with the caller's location.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void LogDebug(LogFormat<TypeIdentity<T>...> format, const T&... args) {
    Detail::LogMessage<T...>(LogLevel::Debug, format, args...);
}

/// Logs a message at LogLevel::Info. See LogTrace.
/// @param format The format string, checked at compile time, with the caller's location.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void LogInfo(LogFormat<TypeIdentity<T>...> format, const T&... args) {
    Detail::LogMessage<T...>(LogLevel::Info, format, args...);
}

/// Logs a message at LogLevel::Warning. See LogTrace.
/// @param format The format string, checked at compile time, with the caller's location.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void LogWarning(LogFormat<TypeIdentity<T>...> format, const T&... args) {
    Detail::LogMessage<T...>(LogLevel::Warning, format, args...);
}

/// Logs a message at LogLevel::Error. See LogTrace. An error is logged once, where it is handled (rule ERR-6).
/// @param format The format string, checked at compile time, with the caller's location.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void LogError(LogFormat<TypeIdentity<T>...> format, const T&... args) {
    Detail::LogMessage<T...>(LogLevel::Error, format, args...);
}

/// Logs a message at LogLevel::Fatal. See LogTrace. It does not end the process; NX_FATAL does.
/// @param format The format string, checked at compile time, with the caller's location.
/// @param args   The arguments, one per replacement field. Borrowed for the duration of the call.
/// @threadsafety safe
/// @since 0.1
template <Formattable... T>
void LogFatal(LogFormat<TypeIdentity<T>...> format, const T&... args) {
    Detail::LogMessage<T...>(LogLevel::Fatal, format, args...);
}

} // namespace Nexyl
