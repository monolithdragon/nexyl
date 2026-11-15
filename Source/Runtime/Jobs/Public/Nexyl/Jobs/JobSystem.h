#pragma once

#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Jobs/Job.h>
#include <Nexyl/Memory/IAllocator.h>
#include <Nexyl/Memory/MemoryTag.h>
#include <Nexyl/Memory/TrackingAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/Synchronization.h>
#include <Nexyl/Platform/Thread.h>

namespace Nexyl::Jobs {

namespace Detail {

/// The job queues of one worker, or of the threads that are not workers. Defined in the implementation.
/// @since 0.1
struct JobQueues;
/// What a worker thread receives at start. Defined in the implementation. @since 0.1
struct WorkerStart;

/// Calls the callable stored in a job's payload. Internal. @since 0.1
using JobFunction = void (*)(const JobContext& context, const void* payload);

} // namespace Detail

/// The memory tag of everything the Jobs module allocates.
/// @since 0.1
inline constexpr Memory::MemoryTag kJobsMemoryTag = Memory::MemoryTag::Make(ModuleId::Jobs);

/// How a JobSystem is set up.
/// @threadsafety safe
/// @since 0.1
struct JobSystemDesc {
    /// Worker threads. Zero chooses the logical processor count minus two (the main and the RHI submit threads),
    /// at least one. Fixed for the system's lifetime; no code may partition work by it (section 7.6).
    u32 workerCount = 0;
    /// Jobs that can be scheduled and not yet finished at the same time. When the pool is full, Run runs the job
    /// at once on the calling thread.
    u32 jobCapacity = 4096;
};

/// Counters of a JobSystem since its creation.
/// @threadsafety safe
/// @since 0.1
struct JobStatistics {
    /// Jobs taken from a queue and run.
    u64 executed = 0;
    /// Jobs run at once on the calling thread because the job pool was full.
    u64 ranInline = 0;
    /// Jobs a thread took from another worker's queue.
    u64 stolen = 0;
};

/// The engine's job scheduler: a fixed set of worker threads with work-stealing queues (docs/NEXYL_ARCHITECTURE.md,
/// section 6.2).
///
/// A job is a small callable stored inline; scheduling allocates nothing. Each worker has a queue per priority and
/// takes its own newest job first, then the oldest job of the threads that are not workers, then the oldest job of
/// another worker. A thread that waits for a job runs other jobs meanwhile, so waiting inside a job is allowed;
/// there are no fibers (D-17). Jobs must not block: no blocking I/O, no sleeping, no lock held across a wait.
///
/// A job finishes when it has returned and all its children have finished. ParallelFor splits a range into
/// batches that depend only on the item count and the grain size, so its batches are the same on every machine.
///
/// ```cpp
/// NX_TRY_ASSIGN(UniquePtr<JobSystem> jobs, JobSystem::Create(jobMemory));
/// jobs->ParallelFor(bodyCount, 64, [&](u32 batch, u32 begin, u32 end) {
///     IntegrateBodies(bodies, begin, end, batchOutputs[batch]);
/// });
/// const JobHandle job = jobs->Run([scene](const JobContext&) { RebuildBounds(*scene); });
/// jobs->Wait(job);
/// ```
/// @threadsafety safe
/// @since 0.1
class JobSystem final {
    struct CreateKey {
        explicit CreateKey() = default;
    };

public:
    /// Creates the job system and starts its workers.
    /// @param allocator Supplies the job pool, the queues and the thread table. It must outlive the system.
    /// @param desc      How the system is set up.
    /// @return The job system.
    /// @error OutOfMemory   The pool or the queues cannot be allocated.
    /// @error ResourceLimit A worker thread cannot be started (Platform::Thread::Create).
    /// @pre `desc.jobCapacity` is at least 1.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_JOBS_API static Result<Memory::UniquePtr<JobSystem>, Error> Create(Memory::IAllocator& allocator,
                                                                                        const JobSystemDesc& desc = {});

    /// Constructs a system without workers. Only Create can call it.
    /// @param key       Restricts construction to Create.
    /// @param allocator Supplies the pool and the queues.
    /// @param desc      How the system is set up.
    /// @since 0.1
    NX_JOBS_API JobSystem(CreateKey key, Memory::IAllocator& allocator, const JobSystemDesc& desc);

    /// Stops and joins the workers. @pre Every job has finished. @since 0.1
    NX_JOBS_API ~JobSystem();

    JobSystem(const JobSystem&) = delete;
    JobSystem& operator=(const JobSystem&) = delete;
    JobSystem(JobSystem&&) = delete;
    JobSystem& operator=(JobSystem&&) = delete;

    /// Schedules a job.
    ///
    /// The callable is copied into the job. If the job pool is full, it runs at once on the calling thread and
    /// the returned handle is null (done).
    /// @param function The job, called once on some thread with the job's context.
    /// @param priority How urgently it runs.
    /// @param parent   A job that does not finish before this one; null for none. Usually the running job
    ///                 (JobContext::job).
    /// @return The job's handle.
    /// @pre `parent` is null or a job that has not finished.
    /// @threadsafety safe
    /// @since 0.1
    template <JobCallable F>
    JobHandle Run(const F& function, JobPriority priority = JobPriority::Normal, JobHandle parent = {});

    /// Calls `body(batchIndex, begin, end)` for consecutive batches of `[0, count)` in parallel and returns when
    /// all have finished.
    ///
    /// Batch `b` covers `[b * grain, min((b + 1) * grain, count))`. The batches depend only on `count` and
    /// `grain`, never on the worker count or on timing, so per-batch outputs merged in batch order give the same
    /// result on every machine (section 7.6). The calling thread runs batches too.
    /// @param count    Number of items.
    /// @param grain    Items per batch, chosen by the call site. A constant, never derived from the machine.
    /// @param body     Called once per batch, concurrently. Borrowed until the function returns.
    /// @param priority Priority of the batch jobs.
    /// @pre `grain` is at least 1.
    /// @threadsafety safe
    /// @determinism deterministic
    /// @since 0.1
    template <ParallelForBody F>
    void ParallelFor(u32 count, u32 grain, const F& body, JobPriority priority = JobPriority::Normal);

    /// Returns when the job has finished, running other jobs meanwhile.
    /// @param job The job; null or finished returns at once.
    /// @threadsafety safe
    /// @since 0.1
    NX_JOBS_API void Wait(JobHandle job);

    /// Tells whether a job has finished.
    /// @param job The job.
    /// @return True if the job and all its children have finished, or the handle is null.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_JOBS_API bool IsDone(JobHandle job) const;

    /// Returns the system's counters.
    /// @return A snapshot; the counters are read one by one.
    /// @threadsafety safe
    /// @since 0.1
    [[nodiscard]] NX_JOBS_API JobStatistics GetStatistics() const;

    /// Schedules a job whose callable is already type-erased. Internal; Run and ParallelFor call it.
    /// @param function Calls the payload.
    /// @param payload  The bytes to store, at most kJobPayloadSize. Borrowed for the duration of the call.
    /// @param size     Number of bytes in `payload`.
    /// @param priority How urgently the job runs.
    /// @param parent   The parent job, or null.
    /// @return The job's handle, or null if it ran inline.
    /// @threadsafety safe
    /// @since 0.1
    NX_JOBS_API JobHandle Schedule(Detail::JobFunction function, const void* payload, usize size, JobPriority priority,
                                   JobHandle parent);

private:
    static void WorkerMain(void* argument);
    Result<void, Error> Start();
    void RunWorker(u32 worker);
    [[nodiscard]] u32 CurrentWorker() const;
    [[nodiscard]] bool TryAllocateSlot(u32& index);
    void FreeSlot(u32 index);
    void Enqueue(u32 index, JobPriority priority);
    [[nodiscard]] bool TryTake(u32 worker, u32& index);
    void Execute(u32 index);
    void Finish(u32 index);
    // Called from the ParallelFor template, so exported.
    [[nodiscard]] NX_JOBS_API JobHandle BeginGroup();
    NX_JOBS_API void EndGroup(JobHandle group);
    void RunInline(Detail::JobFunction function, const void* payload, JobHandle parent);

    template <typename F>
    static void InvokeJob(const JobContext& context, const void* payload);
    template <typename F>
    static void InvokeBatch(const JobContext& context, const void* payload);

    Memory::TrackingAllocator m_memory;
    JobSystemDesc m_desc;

    Detail::JobSlot* m_slots = nullptr;
    // Indices of free slots, used as a stack; protected by m_freeMutex.
    Platform::Mutex m_freeMutex;
    u32* m_freeList = nullptr;
    u32 m_freeCount = 0;

    // m_desc.workerCount worker queues, then the queues of the threads that are not workers.
    Detail::JobQueues* m_queues = nullptr;
    // The ring storage of every queue, in one block.
    u32* m_ringItems = nullptr;
    Detail::WorkerStart* m_workerStarts = nullptr;
    Platform::Thread* m_threads = nullptr;
    u32 m_startedThreads = 0;

    // Idle workers sleep on m_wake. m_pending counts queued jobs; a scheduler that sees sleepers wakes one.
    Platform::Mutex m_sleepMutex;
    Platform::ConditionVariable m_wake;
    Atomic<i64> m_pending{0};
    Atomic<u32> m_sleepers{0};
    Atomic<bool> m_stopping{false};

    Atomic<u64> m_executed{0};
    Atomic<u64> m_ranInline{0};
    Atomic<u64> m_stolen{0};
};

} // namespace Nexyl::Jobs

#include <Nexyl/Jobs/JobSystem.inl>
