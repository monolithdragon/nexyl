#include <Nexyl/Base/Span.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Platform/File.h>
#include <Nexyl/Platform/FileSystem.h>
#include <Nexyl/Platform/PlatformErrors.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Platform;

// The tests work in a directory below the current one (the build directory when run through CTest).
// Each test creates and removes its own files, so the tests are independent of each other.

namespace {

constexpr const char* kDirectory = "NexylPlatformFileTests";

bool SameText(Span<const char> text, const char* expected) {
    usize index = 0;
    for (; index < text.Size(); ++index) {
        if (expected[index] == '\0' || expected[index] != text[index]) {
            return false;
        }
    }
    return expected[index] == '\0';
}

/// Creates the test directory on construction and removes it, with the listed files, on destruction.
class Scratch final {
public:
    Scratch() {
        NX_IGNORE_RESULT(MakeDirectory(kDirectory)); // It may exist after an earlier aborted run.
    }
    ~Scratch() {
        DirectoryIterator iterator;
        Result<DirectoryIterator, Error> opened = DirectoryIterator::Open(kDirectory);
        if (opened.IsOk()) {
            iterator = Move(opened).TakeValue();
        }
        // Remove whatever the test left behind, then the directory.
        DirectoryEntry entry;
        while (iterator.Next(entry).ValueOr(false)) {
            char path[DirectoryEntry::kMaxNameLength + 64] = {};
            usize length = 0;
            for (const char* part = kDirectory; *part != '\0'; ++part) {
                path[length++] = *part;
            }
            path[length++] = '/';
            for (const char character : entry.GetName()) {
                path[length++] = character;
            }
            NX_IGNORE_RESULT(entry.isDirectory ? RemoveEmptyDirectory(path) : RemoveFile(path)); // Best effort.
        }
        iterator = DirectoryIterator();
        NX_IGNORE_RESULT(RemoveEmptyDirectory(kDirectory)); // Best effort cleanup.
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;
};

Span<const u8> Bytes(const char* text, usize length) {
    return Span<const u8>(reinterpret_cast<const u8*>(text), length);
}

} // namespace

NX_TEST(File, WritesAndReadsBack) {
    const Scratch scratch;
    Result<File, Error> created =
        File::Open("NexylPlatformFileTests/data.bin", FileAccess::ReadWrite, FileOpenMode::CreateNew);
    NX_REQUIRE(created.IsOk());
    File file = Move(created).TakeValue();

    NX_REQUIRE(file.WriteAt(0, Bytes("hello world", 11)).IsOk());
    NX_REQUIRE(file.WriteAt(6, Bytes("nexyl", 5)).IsOk());
    NX_CHECK(file.GetSize().ValueOr(0) == 11);

    u8 buffer[32] = {};
    const Result<usize, Error> read = file.ReadAt(0, Span<u8>(buffer));
    NX_REQUIRE(read.IsOk());
    NX_CHECK(read.GetValue() == 11); // Fewer bytes than the buffer holds: the end of the file was reached.
    NX_CHECK(SameText(Span<const char>(reinterpret_cast<const char*>(buffer), 11), "hello nexyl"));

    NX_CHECK(file.ReadAt(100, Span<u8>(buffer)).ValueOr(1) == 0);
    NX_CHECK(file.Flush().IsOk());
    NX_CHECK(file.Close().IsOk());
    NX_CHECK(!file.IsOpen());
}

NX_TEST(File, SetSizeTruncatesAndExtendsWithZeros) {
    const Scratch scratch;
    Result<File, Error> created =
        File::Open("NexylPlatformFileTests/size.bin", FileAccess::ReadWrite, FileOpenMode::CreateOrTruncate);
    NX_REQUIRE(created.IsOk());
    const File file = Move(created).TakeValue();

    NX_REQUIRE(file.WriteAt(0, Bytes("abcdef", 6)).IsOk());
    NX_REQUIRE(file.SetSize(3).IsOk());
    NX_CHECK(file.GetSize().ValueOr(0) == 3);
    NX_REQUIRE(file.SetSize(8).IsOk());

    u8 buffer[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    NX_CHECK(file.ReadAt(0, Span<u8>(buffer)).ValueOr(0) == 8);
    NX_CHECK(buffer[2] == 'c');
    NX_CHECK(buffer[3] == 0 && buffer[7] == 0);
}

NX_TEST(File, OpenModesReportExistence) {
    const Scratch scratch;
    const char* const path = "NexylPlatformFileTests/modes.bin";

    const Result<File, Error> missing = File::Open(path, FileAccess::Read, FileOpenMode::OpenExisting);
    NX_REQUIRE(missing.IsError());
    NX_CHECK(missing.GetError().Is(PlatformError::NotFound));

    NX_CHECK(File::Open(path, FileAccess::Write, FileOpenMode::CreateNew).IsOk());
    const Result<File, Error> again = File::Open(path, FileAccess::Write, FileOpenMode::CreateNew);
    NX_REQUIRE(again.IsError());
    NX_CHECK(again.GetError().Is(PlatformError::AlreadyExists));

    NX_CHECK(File::Open(path, FileAccess::Read, FileOpenMode::OpenOrCreate).IsOk());
}

NX_TEST(File, OpeningADirectoryIsWrongFileType) {
    const Scratch scratch;
    const Result<File, Error> directory = File::Open(kDirectory, FileAccess::Read, FileOpenMode::OpenExisting);
    NX_REQUIRE(directory.IsError());
    NX_CHECK(directory.GetError().Is(PlatformError::WrongFileType));
}

NX_TEST(FileSystem, ReportsInfoRenamesAndRemoves) {
    const Scratch scratch;
    {
        Result<File, Error> created =
            File::Open("NexylPlatformFileTests/a.txt", FileAccess::Write, FileOpenMode::CreateNew);
        NX_REQUIRE(created.IsOk());
        NX_REQUIRE(created.GetValue().WriteAt(0, Bytes("12345", 5)).IsOk());
    }

    const Result<FileInfo, Error> info = GetFileInfo("NexylPlatformFileTests/a.txt");
    NX_REQUIRE(info.IsOk());
    NX_CHECK(info.GetValue().size == 5);
    NX_CHECK(!info.GetValue().isDirectory);
    // Modified after 2020-01-01, in nanoseconds since 1970.
    NX_CHECK(info.GetValue().modificationTime > u64{1'577'836'800} * 1'000'000'000);

    const Result<FileInfo, Error> directoryInfo = GetFileInfo(kDirectory);
    NX_REQUIRE(directoryInfo.IsOk());
    NX_CHECK(directoryInfo.GetValue().isDirectory);

    NX_REQUIRE(RenameFile("NexylPlatformFileTests/a.txt", "NexylPlatformFileTests/b.txt").IsOk());
    NX_CHECK(GetFileInfo("NexylPlatformFileTests/a.txt").IsError());
    NX_CHECK(GetFileInfo("NexylPlatformFileTests/b.txt").IsOk());

    NX_REQUIRE(RemoveFile("NexylPlatformFileTests/b.txt").IsOk());
    const Result<void, Error> removedAgain = RemoveFile("NexylPlatformFileTests/b.txt");
    NX_REQUIRE(removedAgain.IsError());
    NX_CHECK(removedAgain.GetError().Is(PlatformError::NotFound));

    const Result<void, Error> removeDirectory = RemoveFile(kDirectory);
    NX_REQUIRE(removeDirectory.IsError());
    NX_CHECK(removeDirectory.GetError().Is(PlatformError::WrongFileType));
}

NX_TEST(FileSystem, RenameReplacesAnExistingFile) {
    const Scratch scratch;
    {
        const Result<File, Error> old =
            File::Open("NexylPlatformFileTests/old.txt", FileAccess::Write, FileOpenMode::CreateNew);
        NX_REQUIRE(old.IsOk());
        NX_REQUIRE(old.GetValue().WriteAt(0, Bytes("old!", 4)).IsOk());
    }
    NX_REQUIRE(File::Open("NexylPlatformFileTests/new.txt", FileAccess::Write, FileOpenMode::CreateNew).IsOk());
    NX_REQUIRE(RenameFile("NexylPlatformFileTests/old.txt", "NexylPlatformFileTests/new.txt").IsOk());
    NX_CHECK(GetFileInfo("NexylPlatformFileTests/new.txt").GetValue().size == 4);
}

NX_TEST(FileSystem, DirectoriesAreCreatedListedAndRemoved) {
    const Scratch scratch;
    NX_REQUIRE(MakeDirectory("NexylPlatformFileTests/sub").IsOk());
    const Result<void, Error> twice = MakeDirectory("NexylPlatformFileTests/sub");
    NX_REQUIRE(twice.IsError());
    NX_CHECK(twice.GetError().Is(PlatformError::AlreadyExists));
    NX_REQUIRE(File::Open("NexylPlatformFileTests/file.txt", FileAccess::Write, FileOpenMode::CreateNew).IsOk());
    // A non-ASCII name proves the UTF-8 conversion in both directions on Windows.
    NX_REQUIRE(File::Open("NexylPlatformFileTests/Árvíztűrő.txt", FileAccess::Write, FileOpenMode::CreateNew).IsOk());

    Result<DirectoryIterator, Error> opened = DirectoryIterator::Open(kDirectory);
    NX_REQUIRE(opened.IsOk());
    DirectoryIterator iterator = Move(opened).TakeValue();

    bool foundSub = false;
    bool foundFile = false;
    bool foundUnicode = false;
    u32 count = 0;
    DirectoryEntry entry;
    while (iterator.Next(entry).ValueOr(false)) {
        ++count;
        foundSub = foundSub || (SameText(entry.GetName(), "sub") && entry.isDirectory);
        foundFile = foundFile || (SameText(entry.GetName(), "file.txt") && !entry.isDirectory);
        foundUnicode = foundUnicode || SameText(entry.GetName(), "Árvíztűrő.txt");
    }
    NX_CHECK(count == 3);
    NX_CHECK(foundSub);
    NX_CHECK(foundFile);
    NX_CHECK(foundUnicode);

    const Result<void, Error> notEmpty = RemoveEmptyDirectory(kDirectory);
    NX_REQUIRE(notEmpty.IsError());
    NX_CHECK(notEmpty.GetError().Is(PlatformError::DirectoryNotEmpty));
    NX_CHECK(RemoveEmptyDirectory("NexylPlatformFileTests/sub").IsOk());
}

NX_TEST(FileSystem, IteratingAFileIsAnError) {
    const Scratch scratch;
    NX_REQUIRE(File::Open("NexylPlatformFileTests/plain.txt", FileAccess::Write, FileOpenMode::CreateNew).IsOk());
    const Result<DirectoryIterator, Error> opened = DirectoryIterator::Open("NexylPlatformFileTests/plain.txt");
    NX_REQUIRE(opened.IsError());
    NX_CHECK(opened.GetError().Is(PlatformError::WrongFileType));

    const Result<DirectoryIterator, Error> missing = DirectoryIterator::Open("NexylPlatformFileTests/none");
    NX_REQUIRE(missing.IsError());
    NX_CHECK(missing.GetError().Is(PlatformError::NotFound));
}
