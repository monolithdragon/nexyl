#include <Nexyl/Base/Assert.h>
#include <Nexyl/Base/Atomic.h>
#include <Nexyl/Base/Error.h>
#include <Nexyl/Base/Result.h>
#include <Nexyl/Base/Types.h>
#include <Nexyl/Base/Utility.h>
#include <Nexyl/Jobs/Job.h>
#include <Nexyl/Jobs/JobSystem.h>
#include <Nexyl/Memory/GuardAllocator.h>
#include <Nexyl/Memory/UniquePtr.h>
#include <Nexyl/Platform/Thread.h>
#include <Nexyl/Test/Test.h>

using namespace Nexyl;
using namespace Nexyl::Jobs;

namespace {

[[nodiscard]] Memory::UniquePtr<JobSystem> CreateJobs(Memory::IAllocator& allocator, const JobSystemDesc& desc) {
    Result<Memory::UniquePtr<JobSystem>, Error> created = JobSystem::Create(allocator, desc);
    NX_ASSERT(created.IsOk(), "Job system creation failed in a test.");
    return Move(created).TakeValue();
}

// A mixing function with integer arithmetic only, so that its results are the same everywhere.
[[nodiscard]] u64 Mix(u64 value) {
    value ^= value >> 33U;
    value *= 0xFF51'AFD7'ED55'8CCDULL;
    value ^= value >> 33U;
    return value;
}

constexpr u32 kItems = 10'000;
constexpr u32 kGrain = 97;
constexpr u32 kBatches = ((kItems - 1) / kGrain) + 1;

struct BatchOutput {
    u32 begin = 0;
    u32 end = 0;
    u64 hash = 0;
};

// Runs one ParallelFor with per-batch outputs and folds them in batch order.
[[nodiscard]] u64 RunPartitionedWork(JobSystem& jobs, BatchOutput (&outputs)[kBatches]) {
    jobs.ParallelFor(kItems, kGrain, [&outputs](u32 batch, u32 begin, u32 end) {
        u64 hash = batch;
        for (u32 item = begin; item < end; ++item) {
            hash = Mix(hash + item);
        }
        outputs[batch] = BatchOutput{begin, end, hash};
    });
    u64 folded = 0;
    for (const BatchOutput& output : outputs) {
        folded = Mix(folded ^ output.hash);
    }
    return folded;
}

struct Counter {
    Atomic<u32> value{0};
};

struct ForeignContext {
    JobSystem* jobs = nullptr;
    Counter counter;
};

void ScheduleFromForeignThread(void* argument) {
    auto& context = *static_cast<ForeignContext*>(argument);
    Counter* const counter = &context.counter;
    const JobHandle job =
        context.jobs->Run([counter](const JobContext&) { counter->value.FetchAdd(1, MemoryOrder::Relaxed); });
    context.jobs->Wait(job);
}

} // namespace

NX_TEST(JobSystem, RunsAJobAndWaits) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 2});
    Counter counter;
    Counter* const pointer = &counter;
    const JobHandle job = jobs->Run([pointer](const JobContext&) { pointer->value.FetchAdd(1, MemoryOrder::Relaxed); });
    jobs->Wait(job);
    NX_CHECK(jobs->IsDone(job));
    NX_CHECK(counter.value.Load(MemoryOrder::Relaxed) == 1);
    NX_CHECK(jobs->IsDone(JobHandle{}));
}

NX_TEST(JobSystem, AParentFinishesAfterItsChildren) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 3});
    Counter counter;
    Counter* const pointer = &counter;
    const JobHandle parent = jobs->Run([pointer](const JobContext& context) {
        for (u32 child = 0; child < 32; ++child) {
            static_cast<void>(
                context.system->Run([pointer](const JobContext&) { pointer->value.FetchAdd(1, MemoryOrder::Relaxed); },
                                    JobPriority::Normal, context.job));
        }
    });
    jobs->Wait(parent);
    NX_CHECK(counter.value.Load(MemoryOrder::Relaxed) == 32);
}

NX_TEST(JobSystem, ParallelForCoversEveryItemOnce) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 4});
    static constexpr u32 kCount = 5'000;
    u8 hits[kCount] = {};
    jobs->ParallelFor(kCount, 13, [&hits](u32 /*batch*/, u32 begin, u32 end) {
        for (u32 item = begin; item < end; ++item) {
            ++hits[item];
        }
    });
    bool allOnce = true;
    for (const u8 hit : hits) {
        allOnce = allOnce && hit == 1;
    }
    NX_CHECK(allOnce);

    u32 calls = 0;
    jobs->ParallelFor(0, 8, [&calls](u32, u32, u32) { ++calls; });
    NX_CHECK(calls == 0);
    // A single batch runs on the calling thread.
    u32 range[3] = {9, 9, 9};
    jobs->ParallelFor(5, 8, [&calls, &range](u32 batch, u32 begin, u32 end) {
        range[0] = batch;
        range[1] = begin;
        range[2] = end;
        ++calls;
    });
    NX_CHECK(calls == 1);
    NX_CHECK(range[0] == 0 && range[1] == 0 && range[2] == 5);
}

NX_TEST(JobSystem, ParallelForBatchesDoNotDependOnTheWorkerCount) {
    // Section 7.6: the same call gives the same batches, and the same folded result, with any worker count.
    u64 reference = 0;
    BatchOutput referenceOutputs[kBatches] = {};
    const u32 workerCounts[] = {1, 2, 4, 8};
    for (const u32 workers : workerCounts) {
        Memory::GuardAllocator allocator;
        const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = workers});
        BatchOutput outputs[kBatches] = {};
        const u64 folded = RunPartitionedWork(*jobs, outputs);
        if (workers == 1) {
            reference = folded;
            for (u32 batch = 0; batch < kBatches; ++batch) {
                referenceOutputs[batch] = outputs[batch];
            }
            continue;
        }
        NX_CHECK(folded == reference);
        bool sameBatches = true;
        for (u32 batch = 0; batch < kBatches; ++batch) {
            sameBatches = sameBatches && outputs[batch].begin == referenceOutputs[batch].begin &&
                          outputs[batch].end == referenceOutputs[batch].end &&
                          outputs[batch].hash == referenceOutputs[batch].hash;
        }
        NX_CHECK(sameBatches);
    }
    NX_CHECK(referenceOutputs[0].begin == 0 && referenceOutputs[0].end == kGrain);
    NX_CHECK(referenceOutputs[kBatches - 1].end == kItems);
}

NX_TEST(JobSystem, JobsCanWaitForNestedWork) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 2});
    Counter counter;
    Counter* const pointer = &counter;
    // Each outer job runs a ParallelFor and waits for it; waiting runs other jobs, so this cannot deadlock even
    // with fewer workers than outer jobs.
    jobs->ParallelFor(8, 1, [pointer, &jobs](u32, u32, u32) {
        jobs->ParallelFor(100, 10, [pointer](u32, u32 begin, u32 end) {
            pointer->value.FetchAdd(end - begin, MemoryOrder::Relaxed);
        });
    });
    NX_CHECK(counter.value.Load(MemoryOrder::Relaxed) == 800);
}

NX_TEST(JobSystem, RunsInlineWhenThePoolIsFull) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 2, .jobCapacity = 2});
    static constexpr u32 kCount = 1'000;
    u8 hits[kCount] = {};
    jobs->ParallelFor(kCount, 10, [&hits](u32, u32 begin, u32 end) {
        for (u32 item = begin; item < end; ++item) {
            ++hits[item];
        }
    });
    bool allOnce = true;
    for (const u8 hit : hits) {
        allOnce = allOnce && hit == 1;
    }
    NX_CHECK(allOnce);
    NX_CHECK(jobs->GetStatistics().ranInline > 0);
}

NX_TEST(JobSystem, ThreadsThatAreNotWorkersCanScheduleAndWait) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 2});
    ForeignContext context;
    context.jobs = jobs.Get();
    Result<Platform::Thread, Error> created =
        Platform::Thread::Create(Platform::ThreadDesc{&ScheduleFromForeignThread, &context, "Foreign"});
    NX_REQUIRE(created.IsOk());
    Platform::Thread thread = Move(created).TakeValue();
    thread.Join();
    NX_CHECK(context.counter.value.Load(MemoryOrder::Relaxed) == 1);
}

NX_TEST(JobSystem, ManyJobsFromManyProducers) {
    Memory::GuardAllocator allocator;
    const Memory::UniquePtr<JobSystem> jobs = CreateJobs(allocator, JobSystemDesc{.workerCount = 4});
    Counter counter;
    Counter* const pointer = &counter;
    // Every outer job spawns children with itself as parent; the main thread waits for all outer jobs.
    jobs->ParallelFor(64, 1, [pointer, &jobs](u32, u32, u32) {
        const JobHandle group = jobs->Run([pointer](const JobContext& context) {
            for (u32 child = 0; child < 50; ++child) {
                static_cast<void>(context.system->Run(
                    [pointer](const JobContext&) { pointer->value.FetchAdd(1, MemoryOrder::Relaxed); },
                    JobPriority::High, context.job));
            }
        });
        jobs->Wait(group);
    });
    NX_CHECK(counter.value.Load(MemoryOrder::Relaxed) == 64U * 50U);
    const JobStatistics statistics = jobs->GetStatistics();
    NX_CHECK(statistics.executed + statistics.ranInline >= u64{64} * 50U);
}
