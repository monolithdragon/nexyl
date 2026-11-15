#include <Nexyl/Jobs/JobSystem.h>

#include <Nexyl/Base/Construct.h>
#include <Nexyl/Base/Format.h>
#include <Nexyl/Base/Profile.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Memory/MemoryOperations.h>
#include <Nexyl/Platform/SystemInfo.h>

namespace Nexyl::Jobs {

namespace Detail {

struct JobSlot {
    JobFunction function = nullptr;
    alignas(kJobPayloadAlignment) u8 payload[kJobPayloadSize] = {};
    // The job itself plus its unfinished children. The job finishes when this reaches zero.
    Atomic<u32> unfinished{0};
    // Advanced when the job finishes, so that handles to it become stale. Never zero.
    Atomic<u32> generation{1};
    u32 parent = 0;
    bool hasParent = false;
};

// A ring of job indices. Its capacity is the job capacity, so it never overflows.
struct IndexRing {
    u32* items = nullptr;
    u32 capacity = 0;
    u32 head = 0;
    u32 count = 0;

    void PushBack(u32 index) {
        NX_ASSERT(count < capacity, "Job queue overflow.");
        items[(head + count) % capacity] = index;
        ++count;
    }

    [[nodiscard]] bool PopBack(u32& outIndex) {
        if (count == 0) {
            return false;
        }
        --count;
        outIndex = items[(head + count) % capacity];
        return true;
    }

    [[nodiscard]] bool PopFront(u32& outIndex) {
        if (count == 0) {
            return false;
        }
        outIndex = items[head];
        head = (head + 1) % capacity;
        --count;
        return true;
    }
};

struct JobQueues {
    Platform::Mutex mutex;
    // One ring per priority; protected by `mutex`.
    IndexRing rings[kJobPriorityCount];
};

struct WorkerStart {
    JobSystem* system = nullptr;
    u32 worker = 0;
};

} // namespace Detail

namespace {

// The worker the current thread is, for one job system at a time.
struct WorkerIdentity {
    const JobSystem* system = nullptr;
    u32 worker = 0;
};

thread_local constinit WorkerIdentity g_currentWorker{};

// Rounds an idle worker looks for work, yielding in between, before it sleeps.
constexpr u32 kSpinRounds = 64;

[[nodiscard]] u32 DefaultWorkerCount() {
    const u32 processors = Platform::GetSystemInfo().logicalProcessorCount;
    return processors > 3 ? processors - 2 : 1;
}

} // namespace

Result<Memory::UniquePtr<JobSystem>, Error> JobSystem::Create(Memory::IAllocator& allocator,
                                                              const JobSystemDesc& desc) {
    NX_ASSERT(desc.jobCapacity >= 1, "A job system needs room for at least one job.");
    JobSystemDesc resolved = desc;
    if (resolved.workerCount == 0) {
        resolved.workerCount = DefaultWorkerCount();
    }
    Memory::UniquePtr<JobSystem> system = Memory::MakeUnique<JobSystem>(allocator, CreateKey{}, allocator, resolved);
    NX_TRY(system->Start());
    return system;
}

JobSystem::JobSystem(CreateKey /*key*/, Memory::IAllocator& allocator, const JobSystemDesc& desc)
    : m_memory(allocator, kJobsMemoryTag), m_desc(desc) {}

JobSystem::~JobSystem() {
    m_stopping.Store(true, MemoryOrder::SequentiallyConsistent);
    m_sleepMutex.Lock();
    m_wake.NotifyAll();
    m_sleepMutex.Unlock();
    for (u32 index = 0; index < m_startedThreads; ++index) {
        m_threads[index].Join();
    }

    const u32 queueCount = m_desc.workerCount + 1;
    const usize ringItems = usize{queueCount} * kJobPriorityCount * m_desc.jobCapacity;
    if (m_threads != nullptr) {
        for (u32 index = 0; index < m_desc.workerCount; ++index) {
            DestroyAt(&m_threads[index]);
        }
        m_memory.Deallocate(m_threads, sizeof(Platform::Thread) * m_desc.workerCount);
    }
    if (m_workerStarts != nullptr) {
        m_memory.Deallocate(m_workerStarts, sizeof(Detail::WorkerStart) * m_desc.workerCount);
    }
    if (m_queues != nullptr) {
        for (u32 index = 0; index < queueCount; ++index) {
            DestroyAt(&m_queues[index]);
        }
        m_memory.Deallocate(m_queues, sizeof(Detail::JobQueues) * queueCount);
    }
    if (m_ringItems != nullptr) {
        m_memory.Deallocate(m_ringItems, sizeof(u32) * ringItems);
    }
    if (m_freeList != nullptr) {
        NX_ASSERT(m_freeCount == m_desc.jobCapacity, "A job system was destroyed with unfinished jobs.");
        m_memory.Deallocate(m_freeList, sizeof(u32) * m_desc.jobCapacity);
    }
    if (m_slots != nullptr) {
        for (u32 index = 0; index < m_desc.jobCapacity; ++index) {
            DestroyAt(&m_slots[index]);
        }
        m_memory.Deallocate(m_slots, sizeof(Detail::JobSlot) * m_desc.jobCapacity);
    }
}

Result<void, Error> JobSystem::Start() {
    const u32 capacity = m_desc.jobCapacity;
    const u32 workers = m_desc.workerCount;
    const u32 queueCount = workers + 1;

    NX_TRY_ASSIGN(void* const slots,
                  m_memory.TryAllocate(sizeof(Detail::JobSlot) * capacity, alignof(Detail::JobSlot)));
    m_slots = static_cast<Detail::JobSlot*>(slots);
    for (u32 index = 0; index < capacity; ++index) {
        ConstructAt<Detail::JobSlot>(&m_slots[index]);
    }

    NX_TRY_ASSIGN(void* const freeList, m_memory.TryAllocate(sizeof(u32) * capacity, alignof(u32)));
    m_freeList = static_cast<u32*>(freeList);
    // Lowest index on top, so that a lightly loaded system reuses the same few slots.
    for (u32 index = 0; index < capacity; ++index) {
        m_freeList[index] = capacity - 1 - index;
    }
    m_freeCount = capacity;

    const usize ringItems = usize{queueCount} * kJobPriorityCount * capacity;
    NX_TRY_ASSIGN(void* const items, m_memory.TryAllocate(sizeof(u32) * ringItems, alignof(u32)));
    m_ringItems = static_cast<u32*>(items);
    NX_TRY_ASSIGN(void* const queues,
                  m_memory.TryAllocate(sizeof(Detail::JobQueues) * queueCount, alignof(Detail::JobQueues)));
    m_queues = static_cast<Detail::JobQueues*>(queues);
    u32* next = m_ringItems;
    for (u32 queue = 0; queue < queueCount; ++queue) {
        ConstructAt<Detail::JobQueues>(&m_queues[queue]);
        for (Detail::IndexRing& ring : m_queues[queue].rings) {
            ring.items = next;
            ring.capacity = capacity;
            next += capacity;
        }
    }

    NX_TRY_ASSIGN(void* const starts,
                  m_memory.TryAllocate(sizeof(Detail::WorkerStart) * workers, alignof(Detail::WorkerStart)));
    m_workerStarts = static_cast<Detail::WorkerStart*>(starts);
    NX_TRY_ASSIGN(void* const threads,
                  m_memory.TryAllocate(sizeof(Platform::Thread) * workers, alignof(Platform::Thread)));
    m_threads = static_cast<Platform::Thread*>(threads);
    for (u32 worker = 0; worker < workers; ++worker) {
        ConstructAt<Platform::Thread>(&m_threads[worker]);
    }
    for (u32 worker = 0; worker < workers; ++worker) {
        m_workerStarts[worker] = Detail::WorkerStart{this, worker};
        NX_TRY_ASSIGN(Platform::Thread thread,
                      Platform::Thread::Create(Platform::ThreadDesc{&WorkerMain, &m_workerStarts[worker], "Job"}));
        m_threads[worker] = Move(thread);
        ++m_startedThreads;
    }
    return {};
}

JobHandle JobSystem::Schedule(Detail::JobFunction function, const void* payload, usize size, JobPriority priority,
                              JobHandle parent) {
    NX_ASSERT(function != nullptr && size <= kJobPayloadSize, "Invalid job.");
    if (!parent.IsNull()) {
        NX_ASSERT(!IsDone(parent), "The parent job has already finished.");
        m_slots[parent.index].unfinished.FetchAdd(1, MemoryOrder::Relaxed);
    }
    u32 index = 0;
    if (!TryAllocateSlot(index)) {
        RunInline(function, payload, parent);
        return {};
    }
    Detail::JobSlot& slot = m_slots[index];
    slot.function = function;
    Memory::Copy(slot.payload, payload, size);
    slot.hasParent = !parent.IsNull();
    slot.parent = parent.index;
    slot.unfinished.Store(1, MemoryOrder::Relaxed);
    const JobHandle handle{index, slot.generation.Load(MemoryOrder::Relaxed)};
    Enqueue(index, priority);
    return handle;
}

void JobSystem::RunInline(Detail::JobFunction function, const void* payload, JobHandle parent) {
    m_ranInline.FetchAdd(1, MemoryOrder::Relaxed);
    function(JobContext{this, JobHandle{}}, payload);
    if (!parent.IsNull()) {
        Finish(parent.index);
    }
}

JobHandle JobSystem::BeginGroup() {
    u32 index = 0;
    if (!TryAllocateSlot(index)) {
        return {};
    }
    // A group has no function: it finishes when EndGroup has released it and its children have finished.
    Detail::JobSlot& slot = m_slots[index];
    slot.function = nullptr;
    slot.hasParent = false;
    slot.unfinished.Store(1, MemoryOrder::Relaxed);
    return JobHandle{index, slot.generation.Load(MemoryOrder::Relaxed)};
}

void JobSystem::EndGroup(JobHandle group) {
    Finish(group.index);
}

void JobSystem::Wait(JobHandle job) {
    const u32 worker = CurrentWorker();
    while (!IsDone(job)) {
        u32 index = 0;
        if (TryTake(worker, index)) {
            Execute(index);
        } else {
            Platform::Thread::YieldTimeSlice();
        }
    }
}

bool JobSystem::IsDone(JobHandle job) const {
    if (job.IsNull()) {
        return true;
    }
    NX_ASSERT(job.index < m_desc.jobCapacity, "Job handle out of range.");
    return m_slots[job.index].generation.Load(MemoryOrder::Acquire) != job.generation;
}

JobStatistics JobSystem::GetStatistics() const {
    JobStatistics statistics;
    statistics.executed = m_executed.Load(MemoryOrder::Relaxed);
    statistics.ranInline = m_ranInline.Load(MemoryOrder::Relaxed);
    statistics.stolen = m_stolen.Load(MemoryOrder::Relaxed);
    return statistics;
}

void JobSystem::WorkerMain(void* argument) {
    const auto& start = *static_cast<const Detail::WorkerStart*>(argument);
    start.system->RunWorker(start.worker);
}

void JobSystem::RunWorker(u32 worker) {
    g_currentWorker = WorkerIdentity{this, worker};
    char name[32] = {};
    // The last byte stays zero: the name is NUL-terminated.
    static_cast<void>(Format(Span<char>(name, sizeof(name) - 1), "Job {}", worker));
    ProfileThreadName(name);
    u32 idleRounds = 0;
    for (;;) {
        u32 index = 0;
        if (TryTake(worker, index)) {
            Execute(index);
            idleRounds = 0;
            continue;
        }
        if (m_stopping.Load(MemoryOrder::Acquire)) {
            break;
        }
        if (++idleRounds < kSpinRounds) {
            Platform::Thread::YieldTimeSlice();
            continue;
        }
        idleRounds = 0;
        // Sequentially consistent on both sides: either the scheduler sees this sleeper and wakes it, or this
        // worker sees the scheduler's pending job and does not sleep.
        const Platform::MutexLock lock(m_sleepMutex);
        m_sleepers.FetchAdd(1, MemoryOrder::SequentiallyConsistent);
        while (m_pending.Load(MemoryOrder::SequentiallyConsistent) <= 0 &&
               !m_stopping.Load(MemoryOrder::SequentiallyConsistent)) {
            m_wake.Wait(m_sleepMutex);
        }
        m_sleepers.FetchSub(1, MemoryOrder::SequentiallyConsistent);
    }
    g_currentWorker = WorkerIdentity{};
}

u32 JobSystem::CurrentWorker() const {
    // Threads that are not workers of this system share the last queue.
    return g_currentWorker.system == this ? g_currentWorker.worker : m_desc.workerCount;
}

bool JobSystem::TryAllocateSlot(u32& index) {
    const Platform::MutexLock lock(m_freeMutex);
    if (m_freeCount == 0) {
        return false;
    }
    index = m_freeList[--m_freeCount];
    return true;
}

void JobSystem::FreeSlot(u32 index) {
    const Platform::MutexLock lock(m_freeMutex);
    m_freeList[m_freeCount++] = index;
}

void JobSystem::Enqueue(u32 index, JobPriority priority) {
    // Counted before it is visible, so that a worker never sees the queue ahead of the count.
    m_pending.FetchAdd(1, MemoryOrder::SequentiallyConsistent);
    Detail::JobQueues& queues = m_queues[CurrentWorker()];
    {
        const Platform::MutexLock lock(queues.mutex);
        queues.rings[static_cast<usize>(priority)].PushBack(index);
    }
    if (m_sleepers.Load(MemoryOrder::SequentiallyConsistent) > 0) {
        const Platform::MutexLock lock(m_sleepMutex);
        m_wake.NotifyOne();
    }
}

bool JobSystem::TryTake(u32 worker, u32& index) {
    const u32 workers = m_desc.workerCount;
    const u32 external = workers;
    for (usize priority = 0; priority < kJobPriorityCount; ++priority) {
        // Own newest job first: it is likely still in the cache.
        if (worker != external) {
            Detail::JobQueues& own = m_queues[worker];
            const Platform::MutexLock lock(own.mutex);
            if (own.rings[priority].PopBack(index)) {
                m_pending.FetchSub(1, MemoryOrder::SequentiallyConsistent);
                return true;
            }
        }
        // Then the oldest job of the threads that are not workers.
        {
            Detail::JobQueues& shared = m_queues[external];
            const Platform::MutexLock lock(shared.mutex);
            if (shared.rings[priority].PopFront(index)) {
                m_pending.FetchSub(1, MemoryOrder::SequentiallyConsistent);
                return true;
            }
        }
        // Then steal the oldest job of another worker, starting after this one.
        for (u32 offset = 1; offset <= workers; ++offset) {
            const u32 victim = (worker + offset) % (workers + 1);
            if (victim == worker || victim == external) {
                continue;
            }
            Detail::JobQueues& other = m_queues[victim];
            const Platform::MutexLock lock(other.mutex);
            if (other.rings[priority].PopFront(index)) {
                m_pending.FetchSub(1, MemoryOrder::SequentiallyConsistent);
                m_stolen.FetchAdd(1, MemoryOrder::Relaxed);
                return true;
            }
        }
    }
    return false;
}

void JobSystem::Execute(u32 index) {
    Detail::JobSlot& slot = m_slots[index];
    const JobContext context{this, JobHandle{index, slot.generation.Load(MemoryOrder::Relaxed)}};
    {
        const ProfileScope scope("Job");
        slot.function(context, slot.payload);
    }
    m_executed.FetchAdd(1, MemoryOrder::Relaxed);
    Finish(index);
}

void JobSystem::Finish(u32 index) {
    for (;;) {
        Detail::JobSlot& slot = m_slots[index];
        if (slot.unfinished.FetchSub(1, MemoryOrder::AcquireRelease) != 1) {
            return;
        }
        const bool hasParent = slot.hasParent;
        const u32 parent = slot.parent;
        u32 generation = slot.generation.Load(MemoryOrder::Relaxed) + 1;
        if (generation == 0) {
            generation = 1;
        }
        // Release: a thread that sees the new generation also sees everything the job and its children wrote.
        slot.generation.Store(generation, MemoryOrder::Release);
        FreeSlot(index);
        if (!hasParent) {
            return;
        }
        index = parent;
    }
}

} // namespace Nexyl::Jobs
