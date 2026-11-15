#pragma once

// Template definitions of <Nexyl/Jobs/JobSystem.h> (rule HDR-8).

namespace Nexyl::Jobs {

namespace Detail {

/// The payload of one ParallelFor batch. Internal. @since 0.1
template <typename F>
struct BatchPayload {
    /// The caller's body; it outlives the batches because ParallelFor waits.
    const F* body = nullptr;
    /// The batch index.
    u32 batch = 0;
    /// First item.
    u32 begin = 0;
    /// One past the last item.
    u32 end = 0;
};

} // namespace Detail

template <typename F>
void JobSystem::InvokeJob(const JobContext& context, const void* payload) {
    (*static_cast<const F*>(payload))(context);
}

template <typename F>
void JobSystem::InvokeBatch(const JobContext& context, const void* payload) {
    static_cast<void>(context);
    const auto& batch = *static_cast<const Detail::BatchPayload<F>*>(payload);
    (*batch.body)(batch.batch, batch.begin, batch.end);
}

template <JobCallable F>
JobHandle JobSystem::Run(const F& function, JobPriority priority, JobHandle parent) {
    return Schedule(&InvokeJob<F>, &function, sizeof(F), priority, parent);
}

template <ParallelForBody F>
void JobSystem::ParallelFor(u32 count, u32 grain, const F& body, JobPriority priority) {
    NX_ASSERT(grain >= 1, "ParallelFor needs a grain size of at least one item.");
    if (count == 0) {
        return;
    }
    const u32 batchCount = ((count - 1U) / grain) + 1U;
    const auto batchRange = [count, grain](u32 batch, u32& begin, u32& end) {
        const u64 first = u64{batch} * grain;
        const u64 last = first + grain;
        begin = static_cast<u32>(first);
        end = last < count ? static_cast<u32>(last) : count;
    };
    const JobHandle group = batchCount > 1 ? BeginGroup() : JobHandle{};
    if (group.IsNull()) {
        // One batch, or no slot for the group: run every batch here, in order.
        for (u32 batch = 0; batch < batchCount; ++batch) {
            u32 begin = 0;
            u32 end = 0;
            batchRange(batch, begin, end);
            body(batch, begin, end);
        }
        return;
    }
    for (u32 batch = 0; batch < batchCount; ++batch) {
        Detail::BatchPayload<F> payload;
        payload.body = &body;
        payload.batch = batch;
        batchRange(batch, payload.begin, payload.end);
        static_cast<void>(Schedule(&InvokeBatch<F>, &payload, sizeof(payload), priority, group));
    }
    EndGroup(group);
    Wait(group);
}

} // namespace Nexyl::Jobs
