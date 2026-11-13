# Compiler-conformance probes (docs/CODING_STANDARDS.md, rule LANG-3).
#
# Each probe is a tiny translation unit that exercises one language feature.
#   Allowed / Restricted  the feature is part of the language baseline: a failing probe rejects the toolchain.
#   Pending               a C++23 feature not yet admitted: the result is only reported.
#
# A Pending feature may move to Allowed once this probe passes on every supported toolchain,
# including the console SDK compilers. Results are cached, so only the first configure pays for them.

include_guard(GLOBAL)

function(_nexyl_conformance_probe name status code)
    set(variable "NEXYL_CONFORMANCE_${name}")
    if(NOT DEFINED CACHE{${variable}})
        set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
        try_compile(passed
            SOURCE_FROM_CONTENT "Probe${name}.cpp" "${code}"
            CXX_STANDARD 23
            CXX_STANDARD_REQUIRED ON
            CXX_EXTENSIONS OFF
            OUTPUT_VARIABLE output
            NO_CACHE)
        set(${variable} "${passed}" CACHE INTERNAL "Compiler conformance probe: ${name}")
        if(NOT passed)
            file(WRITE "${CMAKE_BINARY_DIR}/nexyl_conformance/${name}.log" "${output}")
        endif()
    endif()

    set_property(GLOBAL APPEND PROPERTY NEXYL_CONFORMANCE_PROBES "${name}")
    set_property(GLOBAL PROPERTY "NEXYL_CONFORMANCE_STATUS_${name}" "${status}")
endfunction()

# Runs all probes, prints the result table, writes nexyl_conformance.json, and rejects the toolchain
# if a baseline feature is missing.
function(nexyl_check_conformance)
    set_property(GLOBAL PROPERTY NEXYL_CONFORMANCE_PROBES "")

    # ---- Allowed -----------------------------------------------------------------------------------------
    _nexyl_conformance_probe(Concepts Allowed [==[
template <typename T>
concept Small = sizeof(T) <= 8;
template <Small T>
constexpr T Twice(T value) requires requires(T v) { v + v; } { return value + value; }
static_assert(Twice(2) == 4);
]==])

    _nexyl_conformance_probe(ConstantEvaluation Allowed [==[
consteval int Square(int value) { return value * value; }
constinit int g_value = Square(3);
constexpr int kValue = Square(4);
static_assert(kValue == 16);
]==])

    _nexyl_conformance_probe(ThreeWayComparison Allowed [==[
#include <compare>
struct Pair {
    int first;
    int second;
    auto operator<=>(const Pair&) const = default;
};
static_assert(Pair{1, 2} < Pair{1, 3});
]==])

    _nexyl_conformance_probe(DesignatedInitializers Allowed [==[
struct Desc { int width = 0; int height = 0; };
constexpr Desc kDesc{.width = 4, .height = 2};
static_assert(kDesc.height == 2);
]==])

    _nexyl_conformance_probe(StructuredBindings Allowed [==[
struct Pair { int first; int second; };
constexpr int Sum() { Pair pair{1, 2}; auto [a, b] = pair; return a + b; }
static_assert(Sum() == 3);
]==])

    _nexyl_conformance_probe(Lambdas Allowed [==[
constexpr auto kAdd = [](auto a, auto b) constexpr { return a + b; };
static_assert(kAdd(1, 2) == 3);
]==])

    _nexyl_conformance_probe(Attributes Allowed [==[
[[nodiscard]] constexpr int Magnitude([[maybe_unused]] int unused, int value) {
    if (value > 0) [[likely]] { return value; } else [[unlikely]] { return -value; }
}
static_assert(Magnitude(0, -2) == 2);
]==])

    _nexyl_conformance_probe(UsingEnum Allowed [==[
enum class Color : unsigned char { Red, Green };
constexpr int Index(Color color) {
    switch (color) { using enum Color; case Red: return 0; case Green: return 1; }
    return -1;
}
static_assert(Index(Color::Green) == 1);
]==])

    _nexyl_conformance_probe(TemplateDeduction Allowed [==[
template <typename T> struct Box { T value; };
template <typename T> Box(T) -> Box<T>;
template <typename... T> constexpr auto Sum(T... values) { return (values + ...); }
constexpr Box kBox{Sum(1, 2, 3)};
static_assert(kBox.value == 6);
]==])

    # ---- Restricted --------------------------------------------------------------------------------------
    _nexyl_conformance_probe(Coroutines Restricted [==[
#include <coroutine>
struct Task {
    struct promise_type {
        Task get_return_object() noexcept { return {}; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() noexcept {}
    };
};
Task Run() { co_return; }
]==])

    # ---- Pending (C++23) ---------------------------------------------------------------------------------
    _nexyl_conformance_probe(DeducingThis Pending [==[
struct Counter {
    int value = 0;
    template <typename TSelf>
    constexpr auto&& Get(this TSelf&& self) { return static_cast<TSelf&&>(self).value; }
};
static_assert(Counter{3}.Get() == 3);
]==])

    _nexyl_conformance_probe(IfConsteval Pending [==[
constexpr int Mode() { if consteval { return 1; } else { return 2; } }
static_assert(Mode() == 1);
]==])

    _nexyl_conformance_probe(MultidimensionalSubscript Pending [==[
struct Grid { constexpr int operator[](int x, int y) const { return x * 10 + y; } };
static_assert(Grid{}[2, 3] == 23);
]==])

    _nexyl_conformance_probe(StaticCallOperator Pending [==[
struct Doubler { static constexpr int operator()(int value) { return value * 2; } };
static_assert(Doubler::operator()(2) == 4);
]==])

    _nexyl_conformance_probe(AssumeAttribute Pending [==[
#if !__has_cpp_attribute(assume)
#error "[[assume]] is not supported"
#endif
constexpr int Half(int value) { [[assume(value >= 0)]]; return value / 2; }
static_assert(Half(4) == 2);
]==])

    _nexyl_conformance_probe(DecayCopy Pending [==[
constexpr int Copy(const int& value) { return auto(value); }
static_assert(Copy(5) == 5);
]==])

    _nexyl_conformance_probe(SizeLiteral Pending [==[
constexpr auto kSize = 42uz;
static_assert(sizeof(kSize) == sizeof(sizeof(int)));
]==])

    _nexyl_conformance_probe(ElifdefDirective Pending [==[
#ifdef NX_PROBE_UNDEFINED_A
#error "unexpected"
#elifndef NX_PROBE_UNDEFINED_B
constexpr int kTaken = 1;
#else
#error "unexpected"
#endif
static_assert(kTaken == 1);
]==])

    # ---- Report ------------------------------------------------------------------------------------------
    get_property(probes GLOBAL PROPERTY NEXYL_CONFORMANCE_PROBES)
    set(json_entries "")
    set(missing_baseline "")
    message(STATUS "Nexyl compiler conformance (${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}):")
    foreach(name IN LISTS probes)
        get_property(status GLOBAL PROPERTY "NEXYL_CONFORMANCE_STATUS_${name}")
        if(NEXYL_CONFORMANCE_${name})
            set(result "pass")
            set(json_result "true")
        else()
            set(result "FAIL")
            set(json_result "false")
            if(NOT status STREQUAL "Pending")
                list(APPEND missing_baseline "${name}")
            endif()
        endif()
        message(STATUS "  ${status} / ${name}: ${result}")
        list(APPEND json_entries "    { \"feature\": \"${name}\", \"status\": \"${status}\", \"supported\": ${json_result} }")
    endforeach()

    string(REPLACE ";" ",\n" json_body "${json_entries}")
    file(WRITE "${CMAKE_BINARY_DIR}/nexyl_conformance.json"
        "{\n  \"compiler\": \"${CMAKE_CXX_COMPILER_ID}\",\n  \"version\": \"${CMAKE_CXX_COMPILER_VERSION}\",\n  \"probes\": [\n${json_body}\n  ]\n}\n")

    if(missing_baseline)
        message(FATAL_ERROR
            "Nexyl: this toolchain does not support baseline language features: ${missing_baseline}. "
            "Compiler output is in ${CMAKE_BINARY_DIR}/nexyl_conformance/.")
    endif()
endfunction()
