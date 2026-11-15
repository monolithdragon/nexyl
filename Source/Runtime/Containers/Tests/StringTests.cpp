#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Containers/Hash.h>
#include <Nexyl/Containers/String.h>
#include <Nexyl/Containers/StringView.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Containers;

// StringView works in constant expressions.
static_assert(StringView("hello").Size() == 5);
static_assert(StringView("hello").StartsWith("he") && StringView("hello").EndsWith("llo"));
static_assert(StringView("hello").Find('l').GetValue() == 2);
static_assert(StringView("hello world").Find("world").GetValue() == 6);
static_assert(!StringView("hello").Find("xyz").HasValue());
static_assert(StringView("abc").Compare("abd") == -1 && StringView("abd").Compare("abc") == 1);
static_assert(StringView("ab").Compare("abc") == -1 && StringView("abc").Compare("abc") == 0);
static_assert(StringView("hello").Substring(1, 3) == StringView("ell"));
static_assert(StringView::FromCString(nullptr).IsEmpty());

NX_TEST(StringView, ViewsLiteralsAndCStrings) {
    const char buffer[16] = "abc";
    // An array literal stops at its first NUL, not at its size.
    NX_CHECK(StringView(buffer).Size() == 3);
    NX_CHECK(StringView::FromCString("xyz") == StringView("xyz"));
    NX_CHECK(StringView("é").Size() == 2); // UTF-8 bytes, not characters.

    usize count = 0;
    for (const char character : StringView("abcd")) {
        count += character != '\0' ? 1 : 0;
    }
    NX_CHECK(count == 4);
    NX_CHECK(StringView("a\xFF").Compare("a\x01") == 1); // Bytes compare as unsigned.
}

NX_TEST(String, BuildsAndEditsText) {
    Memory::GuardAllocator allocator;
    {
        String text(allocator, "Hello");
        NX_CHECK(text.Size() == 5 && text == StringView("Hello"));
        NX_CHECK(text.CStr()[5] == '\0');

        text.Append(", ");
        text.Append("world");
        text.Append('!');
        NX_CHECK(text == StringView("Hello, world!"));

        text.Insert(5, " there");
        NX_CHECK(text == StringView("Hello there, world!"));
        text.Remove(5, 6);
        NX_CHECK(text == StringView("Hello, world!"));

        text[0] = 'J';
        NX_CHECK(text.View().StartsWith("Jello"));

        text.Resize(3);
        NX_CHECK(text == StringView("Jel") && text.CStr()[3] == '\0');
        text.Resize(5, '-');
        NX_CHECK(text == StringView("Jel--"));

        text.Clear();
        NX_CHECK(text.IsEmpty() && text.CStr()[0] == '\0' && text.Capacity() != 0);
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(String, EditsWithViewsOfItself) {
    Memory::GuardAllocator allocator;
    {
        String text(allocator, "abc");
        // Appending its own text grows the string, which moves the bytes the view points to.
        for (u32 round = 0; round < 6; ++round) {
            text.Append(text.View());
        }
        NX_CHECK(text.Size() == usize{3} * 64);
        NX_CHECK(text.View().Substring(189) == StringView("abc"));

        String other(allocator, "0123456789");
        other.Insert(2, other.View().Substring(5, 3)); // "567" inserted before "2".
        NX_CHECK(other == StringView("0156723456789"));
        other.Insert(1, other.View().First(3)); // The source "015" is split by the gap.
        NX_CHECK(other == StringView("0015156723456789"));

        other.Assign(other.View().Substring(3, 4));
        NX_CHECK(other == StringView("5156"));
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}

NX_TEST(String, MovesAndClonesExplicitly) {
    Memory::GuardAllocator allocator;
    {
        String first(allocator, "moved text");
        String second(Nexyl::Move(first));
        NX_CHECK(first.IsEmpty() && second == StringView("moved text"));
        first.Clear(); // The moved-from string is valid; it kept no allocator.
        NX_CHECK(first.GetAllocator() == nullptr);

        const String copy = second.Clone();
        NX_CHECK(copy == second && copy.CStr() != second.CStr());

        first = Nexyl::Move(second);
        NX_CHECK(first == StringView("moved text") && second.IsEmpty());

        const String none;
        NX_CHECK(none.CStr()[0] == '\0' && none.Clone().GetAllocator() == nullptr);
        NX_CHECK(HashValue(copy) == HashValue(StringView("moved text")));
    }
    NX_CHECK(allocator.GetStatistics().liveAllocationCount == 0);
}
