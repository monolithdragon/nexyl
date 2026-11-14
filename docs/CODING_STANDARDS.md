# Nexyl Engine — Coding Standards

| | |
|---|---|
| Status | Baseline 0.1 — foundation design, pre-implementation |
| Date | 2026-11-13 |
| Audience | Everyone who writes or reviews Nexyl code, human or AI |
| Companion documents | [NEXYL_ARCHITECTURE.md](NEXYL_ARCHITECTURE.md), [PLUGIN_ABI.md](PLUGIN_ABI.md), [../CLAUDE.md](../CLAUDE.md) |

The key words MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as defined in RFC 2119.

## Contents

1. [Scope and enforcement](#1-scope-and-enforcement)
2. [Language baseline](#2-language-baseline)
3. [Compiler configuration](#3-compiler-configuration)
4. [Standard library policy](#4-standard-library-policy)
5. [Error handling](#5-error-handling)
6. [Ownership and lifetime](#6-ownership-and-lifetime)
7. [Naming](#7-naming)
8. [Files and headers](#8-files-and-headers)
9. [Types and functions](#9-types-and-functions)
10. [Initialization and globals](#10-initialization-and-globals)
11. [Templates and compile time](#11-templates-and-compile-time)
12. [Concurrency](#12-concurrency)
13. [Simulation-safe code](#13-simulation-safe-code)
14. [Platform-specific code](#14-platform-specific-code)
15. [Reflection](#15-reflection)
16. [API documentation](#16-api-documentation)
17. [API stability](#17-api-stability)
18. [Formatting](#18-formatting)
19. [Testing](#19-testing)
20. [CI gates](#20-ci-gates)
21. [Third-party code](#21-third-party-code)
22. [Rule basis and deviations](#22-rule-basis-and-deviations)

---

## 1. Scope and enforcement

These rules apply to all code under `Source/Runtime`, `Source/Plugins` and `Source/Editor`. Host tools under `Source/Tools` and the private parts of adapter modules follow them except where a rule says otherwise.

Each rule has an ID and an enforcement level:

| Level | Meaning |
|---|---|
| CI | Checked by a machine. A violation blocks the merge. |
| Review | Checked by the reviewer. |

A rule may be broken only through the deviation process in section 22.

---

## 2. Language baseline

| ID | Rule | Level |
|---|---|---|
| LANG-1 | The language is ISO C++23 with compiler extensions disabled. The mode is set centrally by the build system; modules never set it themselves. | CI |
| LANG-2 | Minimum toolchains: MSVC 19.40, Clang 18, GCC 14, Apple Clang 16 (Xcode 16), Android NDK r27, and the current console SDK compilers. | CI |
| LANG-3 | A language feature may be used only if it is in the Allowed or Restricted column below. Moving a feature into those columns requires that the compiler-conformance test passes on every toolchain in LANG-2, including console compilers. | Review |

| Allowed | Restricted (conditions apply) | Banned |
|---|---|---|
| Concepts and `requires` | Coroutines: only in non-simulation asynchronous code, with Nexyl task types and an explicit allocator | Exceptions: `throw`, `try`, `catch` |
| `constexpr`, `consteval`, `constinit` | `union`: only inside a tagged wrapper; prefer `Variant` | RTTI: `dynamic_cast`, `typeid` |
| Three-way comparison | `friend`: only for operators and tightly coupled helpers in the same header | C++20 modules (until all toolchains support them) |
| Designated initializers | `mutable`: only for caches and synchronisation members | `long double` |
| Structured bindings (on aggregates) | `auto`: when the type is evident from the initializer or cannot be named | `goto`, `setjmp`, `longjmp` |
| Lambdas, including generic and `constexpr` | Macros: section 9, CLS-12 | Variable-length arrays, `alloca` |
| `[[nodiscard]]`, `[[maybe_unused]]`, `[[likely]]`, `[[unlikely]]` | `reinterpret_cast`: only in `Base`, `Memory`, `Platform` and RHI backends; elsewhere use `BitCast` | C-style and function-style casts |
| `using enum`, class template argument deduction, fold expressions | `const_cast`: only at documented third-party boundaries | `volatile` as a synchronisation tool |
| `enum class` with explicit underlying type | Multiple inheritance: interfaces only | `using namespace` at header scope |
| | `thread_local`: only in `Platform`, `Memory`, `Jobs`, `Profile` | Bit-fields in reflected, serialized or ABI types |

C++23 features not listed (deducing `this`, `if consteval`, multidimensional subscript, static call operators, `[[assume]]`, `auto(x)`, the `uz` literal suffix, `#elifdef`) are **pending**. Until one is listed here, use the wrapper macro where one exists (`NX_ASSUME`) or do not use the feature.

The compiler-conformance probes in `cmake/NexylConformance.cmake` run at every configure. A baseline feature that fails rejects the toolchain; the result of each pending feature is printed and written to `nexyl_conformance.json` in the build directory. As of 2026-11-14 every pending feature passes on MSVC 19.51, Clang 19 and 23, and GCC 14.2. They stay pending until the probes have also passed on the minimum versions of LANG-2 that are still unverified (MSVC 19.40, Clang 18, Apple Clang 16, Android NDK r27) and on the console SDK compilers.

---

## 3. Compiler configuration

Flags are owned by the build system. A module MUST NOT add or remove compiler flags; it declares properties in its manifest (`SIM_SAFE`, `KIND`) and receives the matching flag set.

| Purpose | MSVC | Clang / GCC |
|---|---|---|
| Exceptions off | `/EHs-c-`, `_HAS_EXCEPTIONS=0` | `-fno-exceptions` |
| RTTI off | `/GR-` | `-fno-rtti` |
| Warnings as errors | `/W4 /WX` | `-Wall -Wextra -Wpedantic -Werror` |
| Extra warnings | conversion and shadowing warnings enabled | `-Wconversion -Wshadow -Wglobal-constructors -Wexit-time-destructors` (where available) |
| Symbol visibility | exports only through `NX_<MODULE>_API` | `-fvisibility=hidden` plus the same macro |
| Strict floating point, on every module | See the architecture document, section 7.3 | Same |

The authoritative flag set is `cmake/NexylCompiler.cmake`.

| ID | Rule | Level |
|---|---|---|
| CFG-1 | Code compiles without warnings on every supported toolchain. Warnings are never disabled locally except through the deviation process. | CI |
| CFG-2 | Strict aliasing stays enabled. Type punning goes through `BitCast` or `Memory::Copy`. | Review |
| CFG-3 | Because exceptions are disabled, `noexcept` is written only where type traits need it: move constructors, move assignment and swap. | Review |
| CFG-4 | Sanitizer builds (address, undefined behaviour, thread) exist for the desktop Clang toolchain and run in CI. | CI |
| CFG-5 | A module manifest (`CMakeLists.txt`) contains exactly one `nexyl_add_module()` call and no other command. | CI |

---

## 4. Standard library policy

The runtime does not use the C++ standard library or the C runtime library, with one fixed exception list.

### 4.1 Whitelisted headers

| Header | Why it cannot be replaced | Exposed to the rest of the engine as |
|---|---|---|
| `<cstddef>` | Fundamental size and pointer types, `offsetof` | `usize`, `isize` |
| `<cstdint>` | Fixed-width integer types | `u8` … `u64`, `i8` … `i64` |
| `<new>` | Placement new, `launder` | `Memory` construction helpers |
| `<initializer_list>` | Brace initialization is tied to this type by the language | `InitializerList<T>` |
| `<compare>` | Result types of three-way comparison | Nexyl ordering aliases |
| `<type_traits>` | Many traits are compiler intrinsics | Nexyl trait aliases and concepts |
| `<concepts>` | Core language-support concepts | Nexyl concepts |
| `<coroutine>` | Coroutine handle and traits are compiler-coupled | Nexyl task types |
| `<source_location>` | Call-site capture is a compiler intrinsic | `SourceLocation` |
| `<atomic>` | Portable atomics | `Atomic<T>` |
| `<bit>` | Bit casts and bit counting intrinsics | `BitCast`, bit utilities |
| `<limits>` | Numeric limits | Nexyl numeric constants |

### 4.2 Rules

| ID | Rule | Level |
|---|---|---|
| STL-1 | Whitelisted headers are included only from files under `Base/Public/Nexyl/Base/Std/`. Every other file includes Nexyl headers. | CI |
| STL-2 | The token `std::` does not appear outside `Base`. Other modules use the Nexyl names in the table above. | CI |
| STL-3 | No other standard or C runtime header is included: no containers, strings, streams, algorithms, smart pointers, threads, `<cmath>`, `<cstring>`, `<cstdio>`, `<cstdlib>`. | CI |
| STL-4 | Memory operations use `Memory::Copy`, `Memory::Move`, `Memory::Set`, `Memory::Compare`. Math uses `Nexyl::Math`. Formatting uses the Nexyl formatter. | CI |
| STL-5 | `Adapter` and `Tool` modules MAY use the standard library and third-party libraries inside `Private/`. Such types never appear in a `Public/` header and never cross a module boundary. | CI |
| STL-6 | Operating-system headers are included only in `Private/` directories of `Platform`, RHI backends, audio device backends and adapters. | CI |
| STL-7 | Structured bindings are used with aggregates. The tuple protocol, which needs additional standard headers, is not used. | Review |

---

## 5. Error handling

### 5.1 Three kinds of failure

| Kind | Example | Mechanism |
|---|---|---|
| Programmer error | Index out of range, broken precondition, misuse of an API | Assertion |
| Recoverable runtime failure | File missing, malformed asset, device lost, optional memory unavailable | `Result<T, Error>` |
| Unrecoverable failure | Small allocation fails, internal invariant destroyed | `NX_FATAL` |

### 5.2 Result and Error

- `Result<T, E = Error>` holds either a value or an error. The type is `[[nodiscard]]`; a result cannot be dropped silently. It converts implicitly from `T` and from `E`, so a function returns either directly. `Result<void, E>` is default-constructed as success: `return {};`.
- `Error` is a small trivially copyable value: an `ErrorCode` (module ID in the high 16 bits, module-specific code in the low 16 bits) and a 32-bit detail value such as an OS error number. When `NX_ERROR_LOCATIONS` is 1 (the default without `NDEBUG`), it also records the file and line where it was created; this changes its layout, so the setting is part of the Tier 1 build fingerprint. Creating an error never allocates. Create one explicitly: `return Error(MemoryError::OutOfMemory);`.
- Each module declares its codes as `enum class <Module>Error : u16` in `<Module>Errors.h`, starting at 1, because 0 means success. Next to the enumeration it defines `constexpr ModuleId GetErrorModule(<Module>Error)`, which the `ErrorEnum` concept requires. A module's `ModuleId` is added to `Base/Error.h` when the module is created; an assigned ID never changes and is never reused.
- `Option<T>` expresses "absent is a normal outcome". It is not an error channel. Its empty state is `kNone`.

### 5.3 Propagation

Two macros propagate failures in standard C++ (no compiler extensions):

| Macro | Use |
|---|---|
| `NX_TRY(expression)` | Evaluates a `Result`; returns the error from the enclosing function on failure and discards any value. |
| `NX_TRY_ASSIGN(declaration, expression)` | Evaluates a `Result<T, Error>`; on success declares `declaration` and direct-initialises it with the value, otherwise returns the error. At most one per source line. |

```cpp
Result<Mesh, Error> LoadMesh(StringView path, IAllocator& allocator) {
    NX_TRY_ASSIGN(File file, File::Open(path));
    NX_TRY_ASSIGN(MeshHeader header, ReadHeader(file));
    NX_TRY(ValidateHeader(header));
    return Mesh::Create(header, file, allocator);
}
```

### 5.4 Rules

| ID | Rule | Level |
|---|---|---|
| ERR-1 | Exceptions are not used. Third-party exceptions are caught inside the adapter that calls the library. | CI |
| ERR-2 | A function that can fail at run time returns `Result`. It does not return `bool`, a sentinel value, or use an error out-parameter. | Review |
| ERR-3 | Every `Result` is consumed: propagated, handled, or explicitly discarded with `NX_IGNORE_RESULT` and a comment explaining why. | CI |
| ERR-4 | Constructors do not fail. A type whose initialisation can fail has a private constructor and a `static Result<T, Error> Create(...)` factory. | Review |
| ERR-5 | Destructors do not fail and do not report errors. Fallible teardown has an explicit function (`Close`, `Flush`) that returns `Result`. | Review |
| ERR-6 | An error is logged once, where it is handled. Intermediate layers propagate without logging. | Review |
| ERR-7 | Input from files, networks and plugins is validated and produces `Result` errors. Assertions are never used to validate external input. | Review |
| ERR-8 | At the C ABI boundary, `Result` converts to `NxResult` with the same numeric code. | CI |

### 5.5 Assertions

| Macro | Evaluated in shipping builds | Use |
|---|---|---|
| `NX_ASSERT(condition, message)` | No | Preconditions and invariants. |
| `NX_VERIFY(condition)` | Expression yes, check no | When the expression has a needed side effect. |
| `NX_UNREACHABLE()` | Becomes an optimiser hint | Code paths that cannot execute. |
| `NX_ASSUME(condition)` | Optimiser hint | Only with measured benefit. |
| `NX_FATAL(message)` | Yes | Unrecoverable failure; reports and terminates. |

"Shipping builds" means builds with `NDEBUG`; `NX_ASSERTS_ENABLED` can override the default. A failed check calls the handler installed with `SetAssertHandler`, which decides whether to break into the debugger, continue or abort. Without a handler the process terminates at once, without calling the operating system or the C runtime.

| ID | Rule | Level |
|---|---|---|
| ERR-9 | The condition of `NX_ASSERT` has no side effects. | CI |
| ERR-10 | Public functions assert their documented preconditions. | Review |

---

## 6. Ownership and lifetime

### 6.1 Vocabulary

| Type | Meaning |
|---|---|
| `T&` | Non-owning, never null. The caller guarantees the lifetime. |
| `T*` | Non-owning observer that may be null. **Never** an owner. |
| `Span<T>`, `StringView` | Non-owning views. Valid for the call unless the function documents otherwise. |
| `UniquePtr<T>` | Sole owner. Remembers its allocator. |
| `Ref<T>`, `WeakRef<T>` | Shared ownership through an intrusive count; `T` derives from `RefCounted`. |
| `Handle<T>` | Slot index plus generation into a pool owned by a system. A stale handle is detected. |
| `AssetRef<T>` | Reference to an asset by stable ID. |

### 6.2 Rules

| ID | Rule | Level |
|---|---|---|
| OWN-1 | Global `new`, `delete`, `malloc` and `free` are not used. Objects are created with `MakeUnique`, `MakeRef`, system factories, or `NX_NEW` / `NX_DELETE` inside low-level code. | CI |
| OWN-2 | Every allocation names an allocator chosen by lifetime class (architecture document, section 5.5). Persistent heap allocations carry a tag. | CI |
| OWN-3 | Order of preference: value, then `Handle`, then `UniquePtr`, then `Ref`. Shared ownership needs a reason. | Review |
| OWN-4 | References between systems are handles, not pointers. | Review |
| OWN-5 | Every resource-owning type is RAII. Prefer the rule of zero; otherwise define or delete all five special members. | Review |
| OWN-6 | A moved-from object is valid and empty. | Review |
| OWN-7 | Owning containers are movable but not implicitly copyable. Copies are explicit through `Clone`. | CI |
| OWN-8 | Interfaces have a protected, non-virtual destructor. Objects behind interfaces are released by their owning system, by handle, or by reference count. | Review |
| OWN-9 | Views and frame, scratch or tick memory are never stored beyond their lifetime class. | Review |
| OWN-10 | Scope-exit actions use `NX_DEFER` or a dedicated RAII type, not manual cleanup on each return path. | Review |

---

## 7. Naming

| Element | Convention | Example |
|---|---|---|
| Namespace | PascalCase; root `Nexyl`; one level per module. `Base` is the exception: it is the shared vocabulary and declares directly in `Nexyl` | `Nexyl::Physics`, `Nexyl::Result` |
| Internal namespace | `Detail` (also excluded from reflection) | `Nexyl::Physics::Detail` |
| Class, struct, enum, alias, concept | PascalCase | `RigidBody`, `BodyType`, `TriviallyRelocatable` |
| Interface (pure abstract class) | `I` + PascalCase | `IPhysicsSystem` |
| Template parameter | `T`, or `T` + PascalCase | `T`, `TKey` |
| Function, method | PascalCase, verb first | `ApplyImpulse` |
| Accessor | `GetX`, `SetX` | `GetMass` |
| Boolean query | `IsX`, `HasX`, `CanX`, `ShouldX` | `IsSleeping` |
| Fallible factory | `Create` | `Mesh::Create` |
| Local variable, parameter | camelCase | `contactCount` |
| Output parameter | `out` + PascalCase | `outHits` |
| Public data member of a plain struct | camelCase, no prefix | `speed` |
| Private or protected data member | `m_` + camelCase | `m_linearVelocity` |
| Static data member | `s_` + camelCase | `s_instanceCount` |
| Global variable | `g_` + camelCase | `g_assertHandler` |
| Constant | `k` + PascalCase | `kMaxContacts` |
| Enumerator | PascalCase | `BodyType::Dynamic` |
| Macro | `NX_` + UPPER_SNAKE_CASE | `NX_ASSERT` |
| Export macro | `NX_<MODULE>_API` | `NX_PHYSICS_API` |
| Primitive aliases | lower-case | `u8 u16 u32 u64 i8 i16 i32 i64 f32 f64 usize isize` |
| Range-for protocol | lower-case, because the language requires it | `begin()`, `end()` |
| Cast-like function template | `nx_` + snake_case (the only such exception) | `nx_cast<T>()` |
| Source file | PascalCase, named after its primary type | `RigidBody.h`, `RigidBody.cpp`, `RigidBody.inl` |
| Generated file | `.gen.` infix; never edited | `Physics.reflect.gen.cpp` |
| C ABI type | `Nx` + PascalCase | `NxPluginDescriptor` |
| C ABI function | `nx_` + snake_case | `nx_plugin_get_descriptor` |
| C ABI constant | `NX_` + UPPER_SNAKE_CASE | `NX_ABI_VERSION_MAJOR` |
| C ABI struct field | camelCase (shared with C++) | `structSize` |

| ID | Rule | Level |
|---|---|---|
| NAME-1 | Module-name acronyms keep their capitals (`RHI`, `ECS`, `VFS`, `UI`). Every other acronym is written as a word: `TypeId`, `Gpu`, `Aabb`, `Json`. | Review |
| NAME-2 | Quantities are SI and carry no unit suffix. A non-SI quantity at a boundary names its unit: `timeoutMs`. | Review |
| NAME-3 | `struct` is for passive data with public members and no invariant. Everything else is a `class`. | Review |
| NAME-4 | Names are English, descriptive and unabbreviated except for widely known terms. | Review |

Reference example:

```cpp
namespace Nexyl::Physics {

class RigidBody final : public IPhysicsBody {
public:
    Result<void, Error> ApplyImpulse(const Vec3& impulse);
    [[nodiscard]] f32 GetMass() const;
    [[nodiscard]] bool IsSleeping() const;

private:
    f32 m_mass = 1.0f;
    Vec3 m_linearVelocity;
    bool m_isSleeping = false;
    static u32 s_instanceCount;
};

inline constexpr u32 kMaxContacts = 8;

enum class BodyType : u8 { Static, Kinematic, Dynamic };

} // namespace Nexyl::Physics
```

---

## 8. Files and headers

| ID | Rule | Level |
|---|---|---|
| HDR-1 | Headers use `#pragma once`. | CI |
| HDR-2 | Every public header compiles on its own. | CI |
| HDR-3 | Public headers are included as `<Nexyl/Module/File.h>`. Private headers are included with quotes and a path relative to `Private/`. | CI |
| HDR-4 | A file includes only headers of modules declared as dependencies in its manifest, and never another module's `Private/` headers. | CI |
| HDR-5 | Include order: the file's own header; same-module headers; headers of other Nexyl modules; then, only where STL-5 or STL-6 permits, third-party and system headers. | CI |
| HDR-6 | Include what you use; forward-declare where a declaration suffices. | Review |
| HDR-7 | A public header exposes the minimum. Implementation types stay in `Private/`. | Review |
| HDR-8 | Non-template, non-inline functions are defined in `.cpp` files. Long inline and template definitions go in a `.inl` file included at the end of the header. | Review |
| HDR-9 | One primary type per header. | Review |
| HDR-10 | Generated files are never edited and never included by hand-written headers. | CI |
| HDR-11 | A module directory contains only `CMakeLists.txt`, `Public/`, `Private/` and `Tests/`. `Public/` contains only `Nexyl/`. Platform-specific sources live in `Private/<Platform>/`. | CI |

---

## 9. Types and functions

| ID | Rule | Level |
|---|---|---|
| CLS-1 | Classes are `final` unless designed for inheritance. | Review |
| CLS-2 | At most one implementation base class. Additional bases are interfaces. | CI |
| CLS-3 | An interface has only pure virtual functions, no data, and a protected non-virtual destructor. | Review |
| CLS-4 | Overrides are marked `override`. | CI |
| CLS-5 | Virtual calls do not appear inside per-element hot loops. Interfaces are batch-oriented. | Review |
| CLS-6 | Single-argument constructors and conversion operators are `explicit`. The only exceptions are the converting constructors of the vocabulary wrappers `Result`, `Option` and `Span` from their payload, which are implicit by design and marked `explicit(false)` to show it. | CI |
| CLS-7 | Every data member has an initial value. | CI |
| CLS-8 | Functions returning a value that must be used are `[[nodiscard]]`. | Review |
| CLS-9 | Narrowing conversions are explicit. | CI |
| CLS-10 | Enumerations are `enum class` with an explicit underlying type. Bit flags use `Flags<TEnum>`. | CI |
| CLS-11 | Parameters: small trivially copyable types by value; others by `const&`; sinks by value or `&&`; sequences by `Span`. Results are returned, not written to out-parameters, except caller-provided output spans. | Review |
| CLS-12 | Macros are a last resort: assertions, error propagation, export, platform detection, scope exit. A macro never replaces a function, constant or template. | Review |
| CLS-13 | Code is `const`-correct. | Review |
| CLS-14 | Public versus private data is a deliberate choice, because reflection gives it meaning (section 15). | Review |

---

## 10. Initialization and globals

| ID | Rule | Level |
|---|---|---|
| INIT-1 | No object with static or thread storage duration has a non-trivial constructor or destructor. Globals are `constexpr` or `constinit`. | CI |
| INIT-2 | No function-local static objects with non-trivial construction. | CI |
| INIT-3 | There are no singletons. Subsystems are created explicitly and reached through a context passed by the caller or through the interface registry. | Review |
| INIT-4 | Startup and shutdown follow the explicit order in the architecture document, section 5.9. | Review |
| INIT-5 | Nothing runs at dynamic-library load or unload. | CI |

---

## 11. Templates and compile time

| ID | Rule | Level |
|---|---|---|
| TPL-1 | Templates are constrained with concepts. | Review |
| TPL-2 | Where possible a template is a thin typed wrapper over a non-template core, to limit code size and build time. | Review |
| TPL-3 | Prefer `constexpr` functions to template metaprogramming. | Review |
| TPL-4 | Class templates with non-type parameters that are reflected provide the `TypeId` composition trait described in the architecture document, section 8.8. | CI |
| TPL-5 | Build time is tracked in CI. A change that regresses it markedly needs justification. | Review |

---

## 12. Concurrency

| ID | Rule | Level |
|---|---|---|
| CONC-1 | Threads are created only by `Platform`, `Jobs`, audio backends and connector adapters. All other parallel work is submitted as jobs. | CI |
| CONC-2 | A job does not block: no blocking I/O, no sleeping, no waiting on a lock that another job may hold across a wait. | Review |
| CONC-3 | Shared mutable state has a documented protection mechanism. Prefer partitioning data by ownership over locking. | Review |
| CONC-4 | Atomic operations state their memory order explicitly. | CI |
| CONC-5 | Locks are not recursive, are never held across a job wait or a callback, and follow a documented order. | Review |
| CONC-6 | Every public type and function documents its thread-safety class (section 16). | CI |
| CONC-7 | Frequently written atomics are cache-line aligned with `NX_CACHE_LINE`. | Review |
| CONC-8 | Code on the audio mix thread does not allocate, lock or call into other engine systems. | Review |

---

## 13. Simulation-safe code

These rules apply to modules marked `SIM_SAFE ON` and to any system registered in a simulation stage. They implement the determinism specification in the architecture document, section 7.

| ID | Rule | Level |
|---|---|---|
| DET-1 | Floating-point primitives are `+ − × ÷` and square root. Every other function comes from `Nexyl::Math`. Platform math is not reachable. | CI |
| DET-2 | Floating-point behaviour is never altered locally: no pragmas, no per-file flags, no changes to the floating-point environment. | CI |
| DET-3 | SIMD is used only through `Nexyl::Math` vector types. Raw intrinsics, and in particular reciprocal estimate instructions, are not used. | CI |
| DET-4 | `Min`, `Max`, `Clamp` and float-to-integer conversions use the `Nexyl::Math` functions. | CI |
| DET-5 | A full expression contains at most one call with side effects that matter to the result. Do not rely on argument evaluation order. | CI |
| DET-6 | Code does not observe wall-clock time, thread identity, pointer values, or anything else that differs between runs. | CI |
| DET-7 | Iteration is over ordered containers only: `Array` and the insertion-ordered `HashMap`. Sorting uses a total order. | Review |
| DET-8 | Parallel loops use `ParallelFor` with a constant grain size and write to per-batch outputs. No shared accumulators. | Review |
| DET-9 | Entities and components are created, destroyed, added or removed only through command buffers. | CI |
| DET-10 | Random numbers come only from the counter-based generator with an explicit stream key. | CI |
| DET-11 | Every simulation-state field is initialised. A `@transient` or `@presentation` field never influences simulation state. | Review |
| DET-12 | Simulation state contains no `char`, `long`, `wchar_t`, `long double`, raw pointer or bit-field. | CI |
| DET-13 | Presentation-phase code never writes simulation state. | CI |
| DET-14 | A new simulation feature adds a scenario to the determinism suite. | Review |

---

## 14. Platform-specific code

| ID | Rule | Level |
|---|---|---|
| PLAT-1 | Platform, compiler and architecture are detected only through `NX_PLATFORM_*`, `NX_COMPILER_*` and `NX_ARCH_*`, defined in `Base`. | CI |
| PLAT-2 | Conditional compilation on those macros is allowed only in `Base`, `Platform`, RHI backends, audio device backends and adapters. All other code queries capabilities at run time. | CI |
| PLAT-3 | Per-platform implementations are separate files under `Private/<Platform>/`, selected by the build system, rather than `#if` blocks inside a shared file. | Review |
| PLAT-4 | Console-specific code exists only in the private overlay repositories. | CI |

---

## 15. Reflection

Reflection is automatic (architecture document, section 8). Types are plain C++.

| ID | Rule | Level |
|---|---|---|
| REFL-1 | Reflection registration is never written by hand, and no marker macro is added to a type. | CI |
| REFL-2 | A type in reflection scope has no bit-field, no reference member and no anonymous struct or union. | CI |
| REFL-3 | A reflected class has at most one reflected base class and is default-constructible (the constructor may be private). | CI |
| REFL-4 | A raw pointer member of a reflected type is `@transient`. Persistent references use `Handle`, `AssetRef` or an entity reference. | CI |
| REFL-5 | Anything that must not be reflected lives in a `Detail` namespace or is excluded in the module manifest. | Review |
| REFL-6 | Renaming a serialized type, field or enumerator adds `@renamed-from` in the same change. | CI |
| REFL-7 | Types that need dynamic identity derive from `Object` and are created only through factories. | Review |
| REFL-8 | Public data members are part of the editor and script surface. Types with invariants keep data private and expose accessors. | Review |

Metadata tags are written in the `///` comment of the declaration, one tag per line, after the description. The tag vocabulary and its effects are defined in the architecture document, section 8.5. An unknown tag is a build error.

```cpp
/// Maximum belt speed.
/// @units m/s
/// @range 0..5
f32 maxSpeed = 2.0f;
```

---

## 16. API documentation

Documentation is a product feature of a commercial engine and is enforced like code.

| ID | Rule | Level |
|---|---|---|
| DOC-1 | Every declaration in a `Public/` header has a `///` comment. An undocumented public symbol fails the documentation build. | CI |
| DOC-2 | The first sentence is a one-line summary. It also becomes the editor tooltip for reflected members. | CI |
| DOC-3 | Functions document every parameter (`@param`), the return value (`@return`), and each error they can return (`@error <Code> <condition>`). | CI |
| DOC-4 | Functions and types state their thread-safety class with `@threadsafety`. | CI |
| DOC-5 | Declarations in simulation-safe modules state `@determinism deterministic` or `@determinism presentation`. | CI |
| DOC-6 | Public API states the version that introduced it with `@since`. | CI |
| DOC-7 | Preconditions are stated with `@pre`. Ownership and lifetime of pointer and view parameters are stated explicitly. | Review |
| DOC-8 | Each major public type has a usage example. | Review |
| DOC-9 | Comments describe behaviour and intent, in English, in the present tense. They do not restate the code. | Review |
| DOC-10 | No commented-out code. Pending work is marked `// TODO(<issue-id>): <text>`. | CI |

Thread-safety classes:

| Value | Meaning |
|---|---|
| `safe` | May be called from any thread, concurrently. |
| `compatible` | Concurrent `const` use is safe; mutation needs external synchronisation. |
| `main` | Main thread only. |
| `stage` | Only from the owning simulation stage or system. |
| `realtime` | Callable from the audio mix thread; never blocks or allocates. |

Example:

```cpp
/// Casts rays against the world and reports the closest hit of each.
///
/// @param queries  Rays to cast. Borrowed for the duration of the call.
/// @param outHits  Receives one result per query, in query order. Must have the same size as `queries`.
/// @pre No simulation step is in progress.
/// @threadsafety compatible
/// @determinism deterministic
/// @since 0.1
virtual void RayCast(Span<const RayCastQuery> queries, Span<RayCastHit> outHits) const = 0;
```

---

## 17. API stability

| ID | Rule | Level |
|---|---|---|
| API-1 | The engine uses semantic versioning. Public API breaks only in a major version. | Review |
| API-2 | API marked `@experimental` is exempt from API-1 and is excluded from stability guarantees. | Review |
| API-3 | Removal is preceded by deprecation with `NX_DEPRECATED("since <version>: <replacement>")` for at least one minor release. | Review |
| API-4 | A change to a serialized schema ships with an upgrade path and a test that loads data from the previous version. | CI |
| API-5 | Changes to anything under `Abi/` or to a `*.Api` contract follow [PLUGIN_ABI.md](PLUGIN_ABI.md), section 10. | CI |

---

## 18. Formatting

The repository's `.clang-format` and `.editorconfig` are authoritative and enforced in CI. In summary:

| Topic | Rule |
|---|---|
| Indentation | 4 spaces, no tabs. Namespace contents are not indented. |
| Line length | 120 columns. |
| Braces | Attached (same line) for functions, types and control flow. Always present, even for one statement. |
| Pointers and references | Attached to the type: `const Vec3& value`, `T* pointer`. |
| Declarations | One per line. Consecutive declarations are not column-aligned. |
| Files | UTF-8, LF line endings, final newline, no trailing whitespace. |

| ID | Rule | Level |
|---|---|---|
| FMT-1 | C++ sources are formatted by the repository `.clang-format`. To reformat: `python Scripts/ci/check_format.py --repo . --fix`. | CI |
| FMT-2 | Files are UTF-8 with LF line endings and a final newline. | CI |

The output of clang-format and the findings of clang-tidy differ between versions. The two gates therefore accept exactly one major version, pinned as `REQUIRED_LLVM_MAJOR` in `Scripts/ci/nexyl_ci.py`; the CI workflow installs the same one, and a self-test fails if the two disagree. With any other version installed, the gate is skipped locally and fails where the tools are required.

---

## 19. Testing

| ID | Rule | Level |
|---|---|---|
| TEST-1 | Tests use the engine's own `Test` module. Third-party test frameworks are not used. | CI |
| TEST-2 | Each module has unit tests under `Tests/`. New public API comes with tests. | Review |
| TEST-3 | A bug fix includes a test that fails without the fix. | Review |
| TEST-4 | Tests are deterministic: fixed seeds, no dependence on execution order, timing or the network. | Review |
| TEST-5 | Parsers and archive readers have fuzz tests. | Review |
| TEST-6 | Containers and allocators are tested under the guard allocator and the sanitizers. | CI |
| TEST-7 | Performance-critical code has a benchmark tracked in CI. | Review |

Test categories: unit, determinism, ABI conformance, fuzz, soak, performance.

### 19.1 Writing unit tests

Unit tests live in a module's `Tests/` directory and use `<Nexyl/Test/Test.h>`:

```cpp
NX_TEST(Span, SlicesWithinBounds) {
    const i32 values[] = {1, 2, 3};
    const Span<const i32> all(values);
    NX_REQUIRE(all.Size() == 3);
    NX_CHECK(all.First(2).Size() == 2);
}
```

| Macro | Effect on failure |
|---|---|
| `NX_TEST(Suite, Name)` | Defines a test. Suite and name are PascalCase; the pair is unique within the module. |
| `NX_CHECK(condition)` | Reports the condition with its location, marks the test failed, continues. |
| `NX_REQUIRE(condition)` | Like `NX_CHECK`, then returns from the current function. Only in functions returning `void`. |

There is no registration code and no `main`. The build scans the module's test sources for `NX_TEST` and generates the test table and `main` (`Scripts/build/generate_test_registry.py`), because static registration is forbidden (rule INIT-1). Tests run in source order: files sorted by path, then by line.

| ID | Rule | Level |
|---|---|---|
| TEST-8 | `NX_TEST` never appears inside conditional compilation. Configuration-dependent checks go inside the test body. | CI |
| TEST-9 | Checks are made only from the thread that runs the test. (This is lifted once `Base` has an atomic wrapper.) | Review |

A test executable accepts `--filter <text>` (substring of `Suite.Name`), `--list` and `--help`. While tests run, a failed `NX_ASSERT` in the code under test is reported with its location and ends the run.

---

## 20. CI gates

Gates are CTest tests. One command runs the build, the tests and every gate, locally and in CI:

```
python Scripts/ci/run_workflow.py <preset>
```

`ctest --preset <preset> -L gate` runs the gates alone after a build. A gate whose tool is not installed is reported as skipped locally and fails in CI, where `NEXYL_REQUIRE_GATE_TOOLS` is set. Every active gate blocks a merge.

| Gate | Covers | Status |
|---|---|---|
| Build on every preset, warnings as errors | CFG-1; INIT-1 and INIT-2 on Clang | Active |
| Compiler-conformance probes (configure step) | LANG-2, LANG-3 | Active |
| Module graph validation (configure step) | Architecture rules R1–R7 | Active |
| `gate.module_table` | The module table in the architecture document matches the manifests and obeys R1, R2, R4–R6 | Active |
| `gate.manifests` | CFG-5, HDR-11 | Active |
| `gate.sources` | HDR-1, HDR-3, HDR-4, HDR-10, STL-1 to STL-3, OWN-1, PLAT-1, PLAT-2, DOC-10, FMT-2 (encoding), DEV-1 | Active |
| `gate.headers` | HDR-2 | Active |
| `gate.format` | FMT-1 | Active; needs clang-format |
| `gate.tidy` | Static-analysis profile (section 22), naming (section 7) | Active; needs clang-tidy |
| `selftest.module_rules`, `selftest.ci_scripts` | The graph rules and the gate scripts reject deliberately invalid input | Active |
| Unit tests (`unit.<Module>`) | TEST-2 | Active |
| Address and undefined-behaviour sanitizer run | CFG-4 | Active as preset `linux-x64-asan`; thread sanitizer arrives with `Jobs` |
| Determinism lint and matrix | DET-1 to DET-14 | Planned: lint with `Math` (phase 1), matrix with physics (phase 4) |
| Reflection constraints and schema diff | REFL-2 to REFL-6, API-4 | Planned: phase 2, with the header tool |
| Documentation completeness | DOC-1 to DOC-6 | Planned: phase 2, with the header tool |
| ABI layout baseline and conformance tests | API-5 | Planned: phase 2 |
| Fuzz, soak and performance suites | TEST-5, TEST-7 | Planned; scheduled runs, regressions open blocking issues |

A rule marked CI in this document whose gate is still planned is enforced by review until that gate exists.

---

## 21. Third-party code

| ID | Rule | Level |
|---|---|---|
| TP-1 | Third-party code may be linked only by `Adapter`, `Tool` and importer modules (architecture rule R7). | CI |
| TP-2 | Adding a third-party dependency requires prior approval and a manifest entry: name, version, licence, purpose, owning module. | Review |
| TP-3 | Only permissive licences may be linked into shipped binaries. | Review |
| TP-4 | The dependency is hidden behind a Nexyl interface. No third-party type appears in a public header. | CI |
| TP-5 | Where the library allows it, its allocations are routed to a Nexyl allocator under the `ThirdParty` tag. | Review |

### 21.1 Register

Every approved third-party dependency is listed here (rule TP-2). The version is pinned when the dependency is first introduced.

| Name | Version | Licence | Purpose | Owner | Used by | Status |
|---|---|---|---|---|---|---|
| mpmath | Pinned on introduction | BSD-3-Clause | Computes the high-precision reference values for the `Math` tests (D-27) | `Math` | An offline generator script; its output is committed. Not needed by the runtime, the build or CI. | Approved 2026-11-14, not yet introduced |

---

## 22. Rule basis and deviations

The rule set is informed by MISRA C++:2023 where its guidance is compatible with engine development. This is a design basis, not a claim of certified compliance. The static-analysis profile enables the corresponding checks, and can be tightened if a customer requires formal compliance.

A deviation from a MUST rule requires all of the following:

1. A marker at the site: `// NX-DEVIATION(<rule-id>): <justification>`.
2. Approval by a code owner of the affected module.
3. The narrowest possible scope: one expression or declaration, not a file.

| ID | Rule | Level |
|---|---|---|
| DEV-1 | A deviation marker has exactly the form `// NX-DEVIATION(<rule-id>): <justification>`. It applies to its own line and the line below. | CI |

A well-formed marker suppresses only the named rule at that location. A marker without a rule ID and justification fails the build.
