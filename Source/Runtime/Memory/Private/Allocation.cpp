#include <Nexyl/Memory/Allocation.h>

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Result.h>

namespace Nexyl::Memory {

namespace {

// Constant-initialized and trivially destructible: no static constructor or destructor runs (rule INIT-1).
constinit Atomic<OutOfMemoryHandler> g_outOfMemoryHandler{nullptr};

[[noreturn]] void ReportOutOfMemory(const OutOfMemoryInfo& info) {
    const OutOfMemoryHandler handler = g_outOfMemoryHandler.Load(MemoryOrder::Acquire);
    if (handler != nullptr) {
        handler(info);
    }
    NX_FATAL("Out of memory: an allocation the caller cannot do without has failed.");
}

} // namespace

OutOfMemoryHandler SetOutOfMemoryHandler(OutOfMemoryHandler handler) {
    return g_outOfMemoryHandler.Exchange(handler, MemoryOrder::AcquireRelease);
}

void* Allocate(IAllocator& allocator, usize size, usize alignment) {
    const Result<void*, Error> block = allocator.TryAllocate(size, alignment);
    if (block.IsError()) [[unlikely]] {
        ReportOutOfMemory(OutOfMemoryInfo{&allocator, size, alignment, block.GetError()});
    }
    return block.GetValue();
}

void* Reallocate(IAllocator& allocator, void* block, usize oldSize, usize newSize, usize alignment) {
    const Result<void*, Error> resized = allocator.TryReallocate(block, oldSize, newSize, alignment);
    if (resized.IsError()) [[unlikely]] {
        ReportOutOfMemory(OutOfMemoryInfo{&allocator, newSize, alignment, resized.GetError()});
    }
    return resized.GetValue();
}

} // namespace Nexyl::Memory
