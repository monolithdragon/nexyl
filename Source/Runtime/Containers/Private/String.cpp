#include <Nexyl/Containers/String.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/Allocation.h>
#include <Nexyl/Memory/MemoryErrors.h>
#include <Nexyl/Memory/MemoryOperations.h>

namespace Nexyl::Containers {

namespace {

constexpr usize kSmallestCapacity = 15;

// Whether `text` points into the bytes of a string, so that growing the string would move it.
bool Overlaps(StringView text, const char* data, usize size) {
    return data != nullptr && text.Data() >= data && text.Data() < data + size;
}

} // namespace

String::String(Memory::IAllocator& allocator, StringView text) : m_allocator(&allocator) {
    Assign(text);
}

String::String(String&& other) noexcept
    : m_data(Exchange(other.m_data, nullptr)), m_size(Exchange(other.m_size, usize{0})),
      m_capacity(Exchange(other.m_capacity, usize{0})), m_allocator(Exchange(other.m_allocator, nullptr)) {}

String& String::operator=(String&& other) noexcept {
    if (this != &other) {
        Release();
        m_data = Exchange(other.m_data, nullptr);
        m_size = Exchange(other.m_size, usize{0});
        m_capacity = Exchange(other.m_capacity, usize{0});
        m_allocator = Exchange(other.m_allocator, nullptr);
    }
    return *this;
}

String::~String() {
    Release();
}

String String::Clone() const {
    if (m_allocator == nullptr) {
        return String();
    }
    return Clone(*m_allocator);
}

String String::Clone(Memory::IAllocator& allocator) const {
    return String(allocator, View());
}

void String::Reserve(usize capacity) {
    const Result<void, Error> reserved = TryReserve(capacity);
    if (reserved.IsError()) [[unlikely]] {
        Memory::ReportOutOfMemory(Memory::OutOfMemoryInfo{m_allocator, capacity + 1, 1, reserved.GetError()});
    }
}

Result<void, Error> String::TryReserve(usize capacity) {
    if (capacity <= m_capacity) {
        return {};
    }
    NX_ASSERT(m_allocator != nullptr, "A String needs an allocator before it can hold text.");
    if (capacity >= Memory::kMaxAllocationSize) {
        return Error(Memory::MemoryError::OutOfMemory);
    }
    if (m_data == nullptr) {
        NX_TRY_ASSIGN(void* const memory, m_allocator->TryAllocate(capacity + 1, 1));
        m_data = static_cast<char*>(memory);
        m_data[0] = '\0';
    } else {
        NX_TRY_ASSIGN(void* const memory, m_allocator->TryReallocate(m_data, m_capacity + 1, capacity + 1, 1));
        m_data = static_cast<char*>(memory);
    }
    m_capacity = capacity;
    return {};
}

void String::Assign(StringView text) {
    if (Overlaps(text, m_data, m_size)) {
        // A view of this string's own bytes: move them to the front.
        const usize offset = static_cast<usize>(text.Data() - m_data);
        Memory::Move(m_data, m_data + offset, text.Size());
        m_size = text.Size();
        m_data[m_size] = '\0';
        return;
    }
    if (text.IsEmpty()) {
        Clear();
        return;
    }
    GrowFor(text.Size());
    char* const data = m_data;
    NX_ASSERT(data != nullptr, "A string with capacity has a buffer.");
    Memory::Copy(data, text.Data(), text.Size());
    m_size = text.Size();
    data[m_size] = '\0';
}

void String::Append(StringView text) {
    Insert(m_size, text);
}

void String::Append(char character) {
    GrowFor(m_size + 1);
    m_data[m_size] = character;
    ++m_size;
    m_data[m_size] = '\0';
}

void String::Insert(usize index, StringView text) {
    NX_ASSERT(index <= m_size, "String::Insert index out of range.");
    if (text.IsEmpty()) {
        return;
    }
    // A view of this string's own bytes moves when the string grows; remember where it starts.
    const bool isOwn = Overlaps(text, m_data, m_size);
    const usize ownOffset = isOwn ? static_cast<usize>(text.Data() - m_data) : 0;
    GrowFor(m_size + text.Size());

    Memory::Move(m_data + index + text.Size(), m_data + index, m_size - index);
    if (isOwn) {
        // The source may now be split by the gap: bytes before `index` stayed, bytes after it moved right.
        for (usize copied = 0; copied < text.Size(); ++copied) {
            const usize source = ownOffset + copied;
            m_data[index + copied] = m_data[source < index ? source : source + text.Size()];
        }
    } else {
        Memory::Copy(m_data + index, text.Data(), text.Size());
    }
    m_size += text.Size();
    m_data[m_size] = '\0';
}

void String::Remove(usize index, usize count) {
    NX_ASSERT(index <= m_size && count <= m_size - index, "String::Remove range out of range.");
    if (count == 0) {
        return;
    }
    Memory::Move(m_data + index, m_data + index + count, m_size - index - count);
    m_size -= count;
    m_data[m_size] = '\0';
}

void String::Resize(usize size, char fill) {
    if (size > m_size) {
        GrowFor(size);
        Memory::Set(m_data + m_size, static_cast<u8>(fill), size - m_size);
    }
    if (m_data != nullptr) {
        m_size = size;
        m_data[m_size] = '\0';
    }
}

void String::Clear() {
    m_size = 0;
    if (m_data != nullptr) {
        m_data[0] = '\0';
    }
}

usize String::GrownCapacity(usize minimum) const {
    const usize doubled = m_capacity < kSmallestCapacity ? kSmallestCapacity : m_capacity * 2;
    return doubled < minimum ? minimum : doubled;
}

void String::GrowFor(usize minimum) {
    if (minimum > m_capacity) {
        Reserve(GrownCapacity(minimum));
    }
    NX_ASSERT(m_data != nullptr || minimum == 0, "A string with capacity has a buffer.");
}

void String::Release() {
    if (m_data != nullptr) {
        m_allocator->Deallocate(m_data, m_capacity + 1);
    }
    m_data = nullptr;
    m_size = 0;
    m_capacity = 0;
}

} // namespace Nexyl::Containers
