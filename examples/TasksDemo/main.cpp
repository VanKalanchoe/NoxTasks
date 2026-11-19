#include <NoxTasks/NoxTasks.h>

#include <atomic>
#include <cstdio>
#include <numeric>
#include <string>
#include <vector>

namespace
{
    struct PositionComponent {};
    struct VelocityComponent {};
    struct BoundsComponent {};
    struct DrawListResource {};
}

int main()
{
    using namespace NoxTasks;

    SetLogCallback([](LogLevel level, std::string_view msg)
    {
        const char* tag = level == LogLevel::Error ? "ERROR" : level == LogLevel::Warn ? "WARN" : "INFO";
        std::printf("[TasksDemo][%s] %.*s\n", tag, static_cast<int>(msg.size()), msg.data());
    });

    std::atomic<uint32_t> observedTasks{ 0 };
    SetProfilerCallbacks({
        .OnWorkerThreadStart = [](const char*) {},
        .OnTaskBegin = [&](std::string_view) { observedTasks.fetch_add(1, std::memory_order_relaxed); },
        .OnTaskEnd = [](std::string_view) {}
    });

    JobSystem jobs;

    // 1. Parallel Dispatch + WorkerLocal<T>
    WorkerLocal<uint64_t> partialSums;
    jobs.Dispatch("ParallelAccumulate", 4096, 64, [&](uint32_t idx)
    {
        partialSums.Local() += idx;
    });

    uint64_t totalSum = 0;
    partialSums.ForEach([&](uint64_t& slot) { totalSum += slot; });
    std::printf("[TasksDemo] Parallel sum(0..4095) = %llu\n", static_cast<unsigned long long>(totalSum));

    // 2. Automatic Read/Write SystemGraph
    SystemGraph frameGraph("DemoFrameGraph");
    frameGraph.AddSystem("IntegrateVelocity",
        ComponentAccess{}.Read<VelocityComponent>().Write<PositionComponent>(),
        [&]()
        {
            FrameArena& arena = jobs.GetFrameArena();
            auto scratch = arena.NewArray<float>(64);
            scratch[0] = 1.0f;
        });

    frameGraph.AddSystem("UpdateBounds",
        ComponentAccess{}.Read<PositionComponent>().Write<BoundsComponent>(),
        []() {});

    frameGraph.AddSystem("CullAndBuildDrawList",
        ComponentAccess{}.Read<PositionComponent, BoundsComponent>().Write<DrawListResource>(),
        []() {});

    frameGraph.Run();
    jobs.ResetFrameArenas();

    std::printf("[TasksDemo] FrameGraph completed. Observed %u tasks via ProfilerCallbacks.\n", observedTasks.load());
    return totalSum == (4095ull * 4096ull / 2ull) ? 0 : 1;
}
