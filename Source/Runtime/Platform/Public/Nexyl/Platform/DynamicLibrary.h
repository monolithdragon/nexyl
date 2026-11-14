#pragma once

#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Option.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>

namespace Nexyl::Platform {

/// A loaded shared library (`.dll`, `.so`, `.dylib`). The base of PluginHost's dynamic plugin source
/// (docs/PLUGIN_ABI.md, section 4).
///
/// Loading runs the library's own initialisation code; engine modules have none (rule INIT-5).
/// A DynamicLibrary is a movable handle and unloads the library when destroyed. Every pointer obtained from
/// the library becomes invalid when it is unloaded.
/// @threadsafety compatible
/// @since 0.1
class DynamicLibrary final {
public:
    /// Loads a library.
    ///
    /// A path without a directory is searched in the system's library locations; the current directory is not
    /// searched on Windows. A full path loads exactly that file.
    ///
    /// @param path The library file including its extension, NUL-terminated UTF-8.
    /// @return The loaded library.
    /// @error NotFound The file, or a library it depends on, cannot be found or loaded.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API static Result<DynamicLibrary, Error> Open(const char* path);

    /// Returns the file extension of shared libraries on this platform, including the dot.
    /// @return ".dll", ".so" or ".dylib".
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API static const char* GetFileExtension();

    /// Creates a handle that refers to no library. @since 0.1
    DynamicLibrary() noexcept = default;
    /// Moves a handle; `other` becomes empty. @since 0.1
    NX_PLATFORM_API DynamicLibrary(DynamicLibrary&& other) noexcept;
    /// Unloads this library and takes over `other`. @since 0.1
    NX_PLATFORM_API DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;
    /// Unloads the library. @since 0.1
    NX_PLATFORM_API ~DynamicLibrary();

    DynamicLibrary(const DynamicLibrary&) = delete;
    DynamicLibrary& operator=(const DynamicLibrary&) = delete;

    /// Checks whether the handle refers to a loaded library. @since 0.1
    [[nodiscard]] bool IsOpen() const noexcept {
        return m_native != 0;
    }

    /// Looks up an exported symbol.
    /// @param name The symbol name, NUL-terminated.
    /// @return Its address, or kNone if the library does not export it.
    /// @pre IsOpen().
    /// @since 0.1
    [[nodiscard]] NX_PLATFORM_API Option<void*> FindSymbol(const char* name) const;

    /// Looks up an exported function and returns it with its type.
    ///
    /// ```cpp
    /// using GetDescriptorFn = const NxPluginDescriptor* (*)();
    /// const Option<GetDescriptorFn> entry = library.FindFunction<GetDescriptorFn>("nx_plugin_get_descriptor");
    /// ```
    /// @param name The function name, NUL-terminated.
    /// @return The function, or kNone if the library does not export it.
    /// @pre IsOpen(). `TFunction` is the function's real type; a wrong type is undefined behaviour when called.
    /// @since 0.1
    template <typename TFunction>
    [[nodiscard]] Option<TFunction> FindFunction(const char* name) const {
        const Option<void*> symbol = FindSymbol(name);
        if (!symbol.HasValue()) {
            return kNone;
        }
        // Object-to-function pointer conversion: conditionally supported in C++, guaranteed by POSIX and Windows.
        return reinterpret_cast<TFunction>(symbol.GetValue());
    }

    /// Unloads the library. The handle becomes empty.
    /// @pre IsOpen().
    /// @since 0.1
    NX_PLATFORM_API void Close();

private:
    // Windows HMODULE or POSIX dlopen handle; zero when closed.
    usize m_native = 0;
};

} // namespace Nexyl::Platform
