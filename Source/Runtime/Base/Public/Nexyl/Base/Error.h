#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Config.h>
#include <Nexyl/Base/SourceLocation.h>
#include <Nexyl/Base/Traits.h>
#include <Nexyl/Base/Types.h>

namespace Nexyl {

/// Identifies the module an error code belongs to. Forms the high 16 bits of every error code.
///
/// A module receives its ID when it is created. IDs are part of the C ABI (`NxResult`) and of saved
/// diagnostics, so an assigned ID never changes and is never reused, even if its module is removed.
/// Zero means "no module".
/// @since 0.1
enum class ModuleId : u16 {
    /// No module. Not a valid error source.
    None = 0,
    /// The Base module.
    Base = 1,
    /// The Platform module.
    Platform = 2,
};

/// Satisfied by an enumeration that holds one module's error codes (rule ERR, section 5.2).
///
/// The enumeration has `u16` as underlying type, uses zero for no error, and has a `constexpr`
/// function `GetErrorModule(TEnum)` in its own namespace that returns its ModuleId:
///
/// ```cpp
/// enum class MemoryError : u16 { OutOfMemory = 1, InvalidAlignment };
/// constexpr ModuleId GetErrorModule(MemoryError) { return ModuleId::Memory; }
/// ```
/// @since 0.1
template <typename T>
concept ErrorEnum = Enumeration<T> && SameAs<UnderlyingType<T>, u16> && requires(T code) {
    { GetErrorModule(code) } -> SameAs<ModuleId>;
};

/// A 32-bit error code: the module ID in the high 16 bits and the module-specific code in the low 16 bits.
/// Zero means success. The same value is used as `NxResult` at the C ABI (rule ERR-8).
/// @threadsafety safe
/// @since 0.1
struct ErrorCode {
    /// The packed code.
    u32 value = 0;

    /// Packs a module-specific error code.
    ///
    /// @param code The module-specific code. Must not be zero.
    /// @return The packed code.
    /// @threadsafety safe
    /// @since 0.1
    template <ErrorEnum TEnum>
    [[nodiscard]] static constexpr ErrorCode From(TEnum code) noexcept {
        return ErrorCode{(static_cast<u32>(GetErrorModule(code)) << 16U) | static_cast<u32>(code)};
    }

    /// Returns the module part.
    /// @return The module the code belongs to.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] constexpr ModuleId GetModule() const noexcept {
        return static_cast<ModuleId>(value >> 16U);
    }

    /// Returns the module-specific part.
    /// @return The low 16 bits.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] constexpr u16 GetCode() const noexcept {
        return static_cast<u16>(value & 0xFFFFU);
    }

    /// Compares two codes.
    /// @since 0.1
    constexpr bool operator==(const ErrorCode&) const noexcept = default;
};

/// A recoverable run-time failure (docs/CODING_STANDARDS.md, section 5.2).
///
/// A small, trivially copyable value. Creating one never allocates. When NX_ERROR_LOCATIONS is 1, it also
/// records where it was created.
///
/// ```cpp
/// return Error(MemoryError::OutOfMemory);
/// return Error(PlatformError::FileNotFound, osErrorNumber);
/// ```
/// @threadsafety safe
/// @since 0.1
class Error final {
public:
    /// Creates an error from a module's error code.
    ///
    /// @param code     The module-specific code. Must not be zero.
    /// @param detail   Optional extra value, such as an operating-system error number.
    /// @param location Captured automatically; do not pass.
    /// @threadsafety safe
    /// @since 0.1
    template <ErrorEnum TEnum>
    constexpr explicit Error(TEnum code, u32 detail = 0,
                             const SourceLocation& location = SourceLocation::Current()) noexcept
        : m_code(ErrorCode::From(code)), m_detail(detail)
#if NX_ERROR_LOCATIONS
          ,
          m_file(location.file), m_line(location.line)
#endif
    {
        static_cast<void>(location);
        NX_ASSERT(static_cast<u16>(code) != 0, "Error code zero means success and cannot be an error.");
    }

    /// Returns the packed error code.
    /// @return The code.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] constexpr ErrorCode GetCode() const noexcept {
        return m_code;
    }

    /// Returns the module the error comes from.
    /// @return The module ID.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] constexpr ModuleId GetModule() const noexcept {
        return m_code.GetModule();
    }

    /// Returns the extra value given at creation, or zero.
    /// @return The detail value.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] constexpr u32 GetDetail() const noexcept {
        return m_detail;
    }

    /// Checks whether this is a particular error.
    ///
    /// @param code The code to compare with.
    /// @return True if this error has exactly that module and code.
    /// @threadsafety safe
    /// @since 0.1
    template <ErrorEnum TEnum>
    [[nodiscard]] constexpr bool Is(TEnum code) const noexcept {
        return m_code == ErrorCode::From(code);
    }

    /// Returns where the error was created. File and line are empty when NX_ERROR_LOCATIONS is 0.
    /// The function name is never recorded.
    /// @return The creation site.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] constexpr SourceLocation GetLocation() const noexcept {
#if NX_ERROR_LOCATIONS
        return SourceLocation{m_file, "", m_line};
#else
        return SourceLocation{};
#endif
    }

private:
    ErrorCode m_code;
    u32 m_detail = 0;
#if NX_ERROR_LOCATIONS
    const char* m_file = "";
    u32 m_line = 0;
#endif
};

static_assert(TriviallyCopyable<Error>, "Error must stay a trivially copyable value.");

} // namespace Nexyl
