#include <Nexyl/Base/TypeName.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Test/Test.h>

namespace Nexyl::TypeNameTest {

struct Plain {};
class Klass {};
enum class Scoped : u8 { Value };
enum Unscoped : u8 { UnscopedValue };

struct Outer {
    struct Inner {};
};

template <typename T>
struct Box {};

} // namespace Nexyl::TypeNameTest

namespace {

using namespace Nexyl;

consteval bool NameIs(TypeName name, const char* expected) {
    usize length = 0;
    while (expected[length] != '\0') {
        ++length;
    }
    if (length != name.size) {
        return false;
    }
    for (usize index = 0; index < length; ++index) {
        if (name.data[index] != expected[index]) {
            return false;
        }
    }
    return true;
}

// The prefix up to the template argument list, which every compiler spells the same way.
consteval bool TemplateNameIs(TypeName name, const char* expected) {
    usize length = 0;
    while (length < name.size && name.data[length] != '<') {
        ++length;
    }
    return NameIs(TypeName{name.data, length}, expected);
}

} // namespace

static_assert(NameIs(TypeNameOf<TypeNameTest::Plain>(), "Nexyl::TypeNameTest::Plain"));
static_assert(NameIs(TypeNameOf<TypeNameTest::Klass>(), "Nexyl::TypeNameTest::Klass"));
static_assert(NameIs(TypeNameOf<TypeNameTest::Scoped>(), "Nexyl::TypeNameTest::Scoped"));
static_assert(NameIs(TypeNameOf<TypeNameTest::Unscoped>(), "Nexyl::TypeNameTest::Unscoped"));
static_assert(NameIs(TypeNameOf<TypeNameTest::Outer::Inner>(), "Nexyl::TypeNameTest::Outer::Inner"));
static_assert(TemplateNameIs(TypeNameOf<TypeNameTest::Box<TypeNameTest::Klass>>(), "Nexyl::TypeNameTest::Box"));

NX_TEST(TypeName, IsAvailableAtRunTime) {
    constexpr TypeName kName = TypeNameOf<TypeNameTest::Plain>();
    NX_CHECK(kName.size == 26);
    NX_CHECK(kName.data[0] == 'N');
    NX_CHECK(kName.data[kName.size - 1] == 'n');
}
