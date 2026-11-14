# Nexyl Engine — guide for AI assistants

Nexyl is a modular C++23 real-time 2D/3D game and simulation engine with a self-owned runtime. The game engine ships first; industrial digital twins, training simulators and robotics build on the same runtime.

**Status:** roadmap phase 1 (foundation). The build system, the module system and the CI gates exist. `Base` provides the core vocabulary: platform detection (`Config.h`), `Result`, `Error`, `Option`, `Span`, `Move`/`Forward`, and the assert and log hooks. `Platform` so far has console output and a monotonic clock, for Windows and POSIX. No other module exists yet. The three documents below are the source of truth. If this file disagrees with them, they win, and this file must be corrected.

## Read before you write

| Before you… | Read |
|---|---|
| add or move a module, add a dependency, or touch memory, threading, simulation, reflection or RHI design | `docs/NEXYL_ARCHITECTURE.md` |
| write or review any C++ | `docs/CODING_STANDARDS.md` |
| touch plugin loading, a `*.Api` contract, or anything under `Abi/` | `docs/PLUGIN_ABI.md` |

Read the relevant sections, not just the headings. Rule IDs (for example `ERR-4`, `DET-5`, `R2`) refer to those documents.

## Hard rules

These are never broken, not even temporarily and not even in a prototype.

1. **Layering.** A module depends only on modules in strictly lower layers. No same-layer dependencies. `Impl` and `Adapter` modules are leaves that only an `App` names.
2. **No standard library.** No `std::` and no standard or C runtime headers outside the whitelist wrappers in `Base`. Use `Nexyl::Array`, `HashMap`, `String`, `Span`, `Memory::Copy`, `Nexyl::Math`.
3. **No exceptions, no RTTI.** Fallible functions return `Result<T, Error>`. Fallible construction uses a static `Create` factory.
4. **No global allocation.** No `new`, `delete`, `malloc`, `free`. Every allocation names an allocator chosen by lifetime class.
5. **No static initialization.** No non-trivial globals, function-local statics or singletons. Nothing runs at library load.
6. **No third-party code in the runtime.** Only `Adapter`, `Tool` and importer modules may link it, and never through a public header.
7. **Reflection is automatic.** Types are plain C++. Never add marker macros, generated-body macros or hand-written registration. Optional metadata goes in `///` doc-comment tags.
8. **Simulation is bit-exact.** Simulation-safe code uses only `Nexyl::Math`, ordered containers, command buffers and the counter-based random generator. It never observes wall-clock time, thread identity or pointer values.
9. **Fixed conventions.** Right-handed, Z-up, SI units. f64 world positions (`Vec3d`), f32 for everything else.
10. **No platform conditionals** outside `Base`, `Platform`, RHI backends, audio device backends and adapters.
11. **Public API is documented.** Every declaration in a `Public/` header has a `///` comment with the required tags.
12. **Generated files are never edited.** Change the generator's input instead.

## Naming at a glance

| Element | Style | Example |
|---|---|---|
| Types, functions, methods | PascalCase | `RigidBody`, `ApplyImpulse` |
| Interfaces | `I` prefix | `IPhysicsSystem` |
| Locals, parameters, public struct fields | camelCase | `contactCount`, `speed` |
| Private members / statics / globals | `m_` / `s_` / `g_` + camelCase | `m_mass` |
| Constants | `k` + PascalCase | `kMaxContacts` |
| Macros | `NX_UPPER_SNAKE` | `NX_ASSERT` |
| Primitives | `u8`…`u64`, `i8`…`i64`, `f32`, `f64`, `usize` | |
| C ABI | `NxType`, `nx_function`, `NX_CONSTANT`, camelCase fields | `NxPluginDescriptor` |

Full table: `docs/CODING_STANDARDS.md`, section 7.

## Creating a new module

1. Decide its **kind** (Foundation, Service, Contract, Feature, Domain, Impl, Adapter, ToolLib, Editor, Tool, App) and its **layer**: one above its highest dependency.
2. Check every dependency against rules R1–R7 in `docs/NEXYL_ARCHITECTURE.md`, section 4.1. If a needed dependency is not strictly lower, stop: the design is wrong, not the rule. Typical fixes are a hook in `Base`, a contract module, or moving the bridging code up into `Scene` or `Engine`.
3. **Ask the user before adding the module or any new dependency edge.**
4. Create the directory `Source/<Group>/<ModuleName>/` with `CMakeLists.txt`, `Public/Nexyl/<Module>/`, `Private/` and `Tests/`, and nothing else. The group follows from the kind: `Runtime` (Foundation, Service, Contract, Feature, Domain, TestLib), `Plugins` (Impl, Adapter), `Editor`, `Tools` (ToolLib, Tool), `Apps`. The build finds the module by this layout; there is no list to edit. `Source/Runtime/Base/` is the reference.
5. Write the manifest: one `nexyl_add_module()` call with `NAME`, `KIND`, `LAYER`, `DEPENDS`, and `REFLECT` / `SIM_SAFE` where applicable. Nothing else may appear in a manifest: no flags, no definitions, no `target_*` commands. The build system supplies the `NX_<MODULE>_API` export macro, include paths and the test target.
6. Add the module's row to the table in `docs/NEXYL_ARCHITECTURE.md`, section 4.2, in the same change. `gate.module_table` fails if the row and the manifest differ.
7. Keep public headers minimal. Mark exported symbols with `NX_<MODULE>_API`.
8. Declare error codes as `enum class <Module>Error : u16` in `<Module>Errors.h`.
9. Choose allocators by lifetime class and define the module's memory tag.
10. Add unit tests under `Tests/`. Until the `Test` module exists (phase 1), a test is a plain `main` that returns non-zero on failure.
11. Document every public declaration.
12. Run the workflow for your host preset and make every gate pass.

## Adding or changing public API

1. Prefer handles and spans to pointers; make operations batch-first.
2. Return `Result` for anything that can fail at run time; assert preconditions.
3. Write the `///` comment: summary, `@param`, `@return`, `@error`, `@threadsafety`, `@since`, and `@determinism` in simulation-safe modules.
4. Remember that public data members are visible to the editor and to scripts.
5. Renaming a serialized type, field or enumerator requires `@renamed-from`.
6. For a `*.Api` contract or anything under `Abi/`, follow the checklist in `docs/PLUGIN_ABI.md`, section 13.

## Simulation-safe code checklist

- Only `+ − × ÷`, square root and `Nexyl::Math` functions. No platform math, no raw SIMD intrinsics.
- No expression that depends on argument evaluation order.
- Iterate only `Array` and the insertion-ordered `HashMap`. Sort with a total order.
- Parallelise only with `ParallelFor`, a constant grain size and per-batch outputs.
- Structural entity changes only through command buffers.
- Random numbers only from the counter-based generator with an explicit stream key.
- No `char`, `long`, `wchar_t`, raw pointers or bit-fields in simulation state.
- Add a scenario to the determinism suite for every new simulation feature.

## Stop and ask the user before

- adding a module, or a dependency edge between modules;
- adding any third-party library or tool;
- changing anything under `Abi/`, any `*.Api` contract, or a serialized schema;
- adding a header to the standard-library whitelist, or a C++ feature to the allowed list;
- changing compiler flags, especially floating-point flags;
- deviating from any MUST rule (deviations need the process in `docs/CODING_STANDARDS.md`, section 22);
- making an architectural decision that the documents do not already settle.

When a rule blocks the obvious implementation, say so and propose a compliant design. Do not work around a rule silently.

## Never

- Suppress a warning, a lint or a CI gate to make something pass.
- Add a "temporary" `std::` include, global, singleton or cross-layer include.
- Put platform-specific or console-specific code in a public module.
- Write reflection registration, C ABI tables or bridge code by hand; they are generated.
- Store views, or frame, scratch or tick memory, beyond their lifetime.
- Invent build or test commands. If scaffolding is missing, say so.

## Build and test

Requirements: CMake 3.28 or newer, Ninja, Python 3.10 or newer, and a supported compiler. Two gates need clang-format and clang-tidy at the pinned major version (`REQUIRED_LLVM_MAJOR` in `Scripts/ci/nexyl_ci.py`); with another version they are skipped locally.

One command configures, builds, runs the tests and runs every gate:

```
python Scripts/ci/run_workflow.py <preset>
```

Use this entry point on Windows: it loads the Visual Studio environment that the MSVC presets need. Add `--fresh` to discard a stale configuration.

| Preset | Toolchain | Configuration |
|---|---|---|
| `win-x64-dev` | MSVC | Debug, dynamic modules |
| `win-x64-release` | MSVC | Release, static modules |
| `win-x64-clang-dev` | clang-cl | Debug, dynamic modules |
| `linux-x64-dev` / `linux-x64-release` | Clang (`clang++`) | Debug dynamic / Release static |
| `linux-x64-gcc-dev` | GCC (`g++`) | Debug, dynamic modules |
| `linux-x64-asan` | Clang | Address and undefined-behaviour sanitizers |
| `macos-arm64-dev` / `macos-arm64-release` | Apple Clang | Debug dynamic / Release static |

The individual steps also work: `cmake --preset <preset>`, `cmake --build --preset <preset>`, `ctest --preset <preset>`. For the MSVC presets they need a Visual Studio developer prompt. Useful subsets after a build:

```
ctest --preset <preset> -L gate        # gates only
ctest --preset <preset> -L unit        # unit tests only
python Scripts/ci/check_format.py --repo . --fix    # reformat sources
```

After changing a manifest or adding a file, configure again before trusting a gate: the gates read the module graph written at configure time.

Verified locally on Windows (MSVC 19.51, clang-cl 23) and Linux (Clang 19, GCC 14.2). The CI workflow in `.github/workflows/ci.yml` runs all eight non-clang-cl presets on GitHub Actions, including macOS (Apple Clang 17); the formatting and static-analysis gates are required on the `linux-x64-dev` job. The `win-x64-clang-dev` preset runs only locally.

## Branches

| Branch | Purpose |
|---|---|
| `main` | Released state. Protected on GitHub: changes arrive only through a pull request from `development`, with all eight CI jobs green and the branch up to date. |
| `development` | Integration branch. Every feature branch is created from it and merged back into it. |
| `<area>/<short-name>` | One feature branch per change, for example `ci/development-branch` or `memory/tlsf-allocator`. |

Never commit directly to `main` or `development`. Merge a feature branch into `development` only when the definition of done below holds, using `git merge --no-ff` so the feature stays visible in the history. Merging `development` into `main` is the user's decision.

## Definition of done

A change is done when all of the following hold:

- It builds without warnings on every available preset.
- All CI gates listed in `docs/CODING_STANDARDS.md`, section 20, pass.
- New and changed public API is documented and tested.
- The module table and any affected section of the three documents are updated in the same change.
- Anything not verified (for example a platform that could not be built locally) is stated explicitly.

## Language

Code, comments, commit messages and repository documents are written in English.
