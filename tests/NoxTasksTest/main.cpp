#include <NoxTasks/NoxTasks.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace
{
    int g_TestsPassed = 0;
    int g_TestsFailed = 0;

    void Check(bool condition, const char* expr, const char* file, int line)
    {
        if (condition)
        {
            ++g_TestsPassed;
        }
        else
        {
            ++g_TestsFailed;
            std::fprintf(stderr, "[FAIL] %s (%s:%d)\n", expr, file, line);
        }
    }

#define TEST_CHECK(expr) Check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)

    // Tag types for SystemGraph read/write conflict testing
    struct TransformData {};
    struct VelocityData {};
    struct PhysicsManifold {};
    struct RenderPacket {};
}

int main()
{
    using namespace NoxTasks;

    std::printf("=== NoxTasks Standalone Test Suite ===\n");

    // -------------------------------------------------------------------------
    // 1. Custom Log & Profiler Callbacks
    // -------------------------------------------------------------------------
    std::atomic<uint32_t> logCount{ 0 };
    std::atomic<uint32_t> workerStartCount{ 0 };
    std::atomic<uint32_t> taskBeginCount{ 0 };
    std::atomic<uint32_t> taskEndCount{ 0 };

    SetLogCallback([&](LogLevel level, std::string_view message)
    {
        logCount.fetch_add(1, std::memory_order_relaxed);
        const char* tag = level == LogLevel::Error ? "ERROR" : level == LogLevel::Warn ? "WARN" : "INFO";
        std::printf("[CustomLog][%s] %.*s\n", tag, static_cast<int>(message.size()), message.data());
    });

    SetProfilerCallbacks({
        .OnWorkerThreadStart = [&](const char* threadName)
        {
            if (threadName && threadName[0] != '\0')
                workerStartCount.fetch_add(1, std::memory_order_relaxed);
        },
        .OnTaskBegin = [&](std::string_view taskName)
        {
            if (!taskName.empty())
                taskBeginCount.fetch_add(1, std::memory_order_relaxed);
        },
        .OnTaskEnd = [&](std::string_view taskName)
        {
            if (!taskName.empty())
                taskEndCount.fetch_add(1, std::memory_order_relaxed);
        }
    });

    // -------------------------------------------------------------------------
    // 2. JobSystem Initialization & Worker Counts
    // -------------------------------------------------------------------------
    TEST_CHECK(!JobSystem::IsInitialized());
    {
        JobSystem jobs;
        TEST_CHECK(JobSystem::IsInitialized());
        TEST_CHECK(&JobSystem::Get() == &jobs);
        TEST_CHECK(jobs.GetWorkerCount() >= 1);
        TEST_CHECK(jobs.GetBackgroundWorkerCount() == jobs.GetWorkerCount());
        TEST_CHECK(jobs.GetBackgroundJobLimit() >= 1);
        TEST_CHECK(jobs.GetBackgroundJobLimit() <= jobs.GetBackgroundWorkerCount());

        const uint32_t expectedSlots = jobs.GetWorkerCount() + jobs.GetBackgroundWorkerCount() + JobSystem::IoWorkerCount + 1;
        TEST_CHECK(jobs.GetThreadSlotCount() == expectedSlots);
        TEST_CHECK(jobs.GetCurrentThreadSlot() == expectedSlots - 1);
        TEST_CHECK(jobs.GetChunkCount(0, 1) == 0);
        TEST_CHECK(jobs.GetChunkCount(100, 10) >= 1);
        TEST_CHECK(logCount.load() >= 1);

        // ---------------------------------------------------------------------
        // 3. ParallelFor & Dispatch Determinism and Multi-threaded Execution
        // ---------------------------------------------------------------------
        constexpr uint32_t itemCount = 10000;
        std::vector<uint64_t> squares(itemCount, 0);

        jobs.ParallelFor("ComputeSquares", itemCount, 64, [&](uint32_t chunk, uint32_t begin, uint32_t end)
        {
            (void)chunk;
            TEST_CHECK(begin < end);
            TEST_CHECK(end <= itemCount);
            for (uint32_t i = begin; i < end; ++i)
                squares[i] = static_cast<uint64_t>(i) * static_cast<uint64_t>(i);
        });

        bool squaresOk = true;
        for (uint32_t i = 0; i < itemCount; ++i)
        {
            if (squares[i] != static_cast<uint64_t>(i) * static_cast<uint64_t>(i))
            {
                squaresOk = false;
                break;
            }
        }
        TEST_CHECK(squaresOk);

        std::vector<uint32_t> dispatched(itemCount, 0);
        jobs.Dispatch("DispatchIncrement", itemCount, 32, [&](uint32_t index)
        {
            dispatched[index] = index + 1;
        });

        bool dispatchOk = true;
        for (uint32_t i = 0; i < itemCount; ++i)
        {
            if (dispatched[i] != i + 1)
            {
                dispatchOk = false;
                break;
            }
        }
        TEST_CHECK(dispatchOk);

        // Nested ParallelFor from within worker tasks
        std::atomic<uint32_t> nestedSum{ 0 };
        jobs.ParallelFor("OuterParallel", 16, 1, [&](uint32_t, uint32_t begin, uint32_t end)
        {
            for (uint32_t i = begin; i < end; ++i)
            {
                jobs.Dispatch("InnerDispatch", 32, 4, [&](uint32_t j)
                {
                    (void)j;
                    nestedSum.fetch_add(1, std::memory_order_relaxed);
                });
            }
        });
        TEST_CHECK(nestedSum.load() == 16 * 32);

        // ---------------------------------------------------------------------
        // 4. WorkerLocal<T> Lock-Free Accumulation & Deterministic Merge
        // ---------------------------------------------------------------------
        WorkerLocal<uint64_t> localSums;
        jobs.Dispatch("WorkerLocalSum", itemCount, 16, [&](uint32_t index)
        {
            localSums.Local() += (index + 1);
        });

        uint64_t mergedSum = 0;
        localSums.ForEach([&](uint64_t& slotSum)
        {
            mergedSum += slotSum;
            slotSum = 0;
        });
        const uint64_t expectedSum = static_cast<uint64_t>(itemCount) * (itemCount + 1) / 2;
        TEST_CHECK(mergedSum == expectedSum);

        // ---------------------------------------------------------------------
        // 5. FrameArena Per-Worker Bump Allocation & ResetFrameArenas
        // ---------------------------------------------------------------------
        std::atomic<uint32_t> arenaChecksOk{ 0 };
        jobs.ParallelFor("FrameArenaAlloc", 128, 4, [&](uint32_t chunk, uint32_t begin, uint32_t end)
        {
            FrameArena& arena = jobs.GetFrameArena();
            const uint32_t count = end - begin;
            std::span<uint32_t> scratch = arena.NewArray<uint32_t>(count);
            for (uint32_t i = 0; i < count; ++i)
                scratch[i] = begin + i;

            uint32_t* single = arena.New<uint32_t>(chunk + 12345u);
            if (*single == chunk + 12345u && scratch.size() == count && scratch[0] == begin)
                arenaChecksOk.fetch_add(count, std::memory_order_relaxed);
        });
        TEST_CHECK(arenaChecksOk.load() == 128);

        // Main thread allocation + ResetFrameArenas
        FrameArena& mainArena = jobs.GetFrameArena();
        void* firstPtr = mainArena.Allocate(256, 16);
        TEST_CHECK(firstPtr != nullptr);
         TEST_CHECK((reinterpret_cast<uintptr_t>(firstPtr) & 15) == 0);
        jobs.ResetFrameArenas();
        void* afterResetPtr = mainArena.Allocate(256, 16);
        TEST_CHECK(afterResetPtr == firstPtr); // Reuses first block from offset 0

        // ---------------------------------------------------------------------
        // 6. RunTasks with exclusiveMasks (Mutual Exclusion Verification)
        // ---------------------------------------------------------------------
        // Mask bits 0, 1, 2 represent 3 shared non-thread-safe resources.
        std::atomic<int> activeOnKey[3] = { 0, 0, 0 };
        std::atomic<int> exclusionViolations{ 0 };
        std::atomic<uint32_t> tasksCompleted{ 0 };

        const std::vector<uint32_t> masks = {
            0b001, 0b001, 0b010, 0b010,
            0b011, 0b100, 0b101, 0b110,
            0b111, 0b000, 0b001, 0b010
        };

        jobs.RunTasks("ExclusiveTasks", masks, [&](uint32_t index)
        {
            const uint32_t mask = masks[index];
            for (uint32_t bit = 0; bit < 3; ++bit)
            {
                if (mask & (1u << bit))
                {
                    if (activeOnKey[bit].fetch_add(1, std::memory_order_acq_rel) != 0)
                        exclusionViolations.fetch_add(1, std::memory_order_relaxed);
                }
            }

            std::this_thread::sleep_for(std::chrono::milliseconds(2));

            for (uint32_t bit = 0; bit < 3; ++bit)
            {
                if (mask & (1u << bit))
                {
                    if (activeOnKey[bit].fetch_sub(1, std::memory_order_acq_rel) != 1)
                        exclusionViolations.fetch_add(1, std::memory_order_relaxed);
                }
            }
            tasksCompleted.fetch_add(1, std::memory_order_relaxed);
        });

        TEST_CHECK(tasksCompleted.load() == masks.size());
        TEST_CHECK(exclusionViolations.load() == 0);

        // ---------------------------------------------------------------------
        // 7. Async (High, Normal, Background) & AsyncIO + CancellationToken
        // ---------------------------------------------------------------------
        auto highFuture = jobs.Async("HighPriorityJob", JobPriority::High, [](const CancellationToken& token) -> int
        {
            return token.IsCancelled() ? -1 : 42;
        });
        TEST_CHECK(highFuture.IsValid());
        highFuture.Wait();
        TEST_CHECK(highFuture.IsReady());
        TEST_CHECK(highFuture.Get() == 42);

        // Normal priority job using nested ParallelFor on background workers
        auto normalFuture = jobs.Async("NormalStreamingJob", JobPriority::Normal, [&](const CancellationToken& token) -> uint32_t
        {
            if (token.IsCancelled())
                return 0;
            std::atomic<uint32_t> acc{ 0 };
            jobs.ParallelFor("BgNestedParallel", 64, 4, [&](uint32_t, uint32_t b, uint32_t e)
            {
                acc.fetch_add(e - b, std::memory_order_relaxed);
            });
            return acc.load();
        });

        // Background priority job testing cooperative CancellationToken
        std::atomic<bool> bgStarted{ false };
        auto bgCancelFuture = jobs.Async("CancellableCookJob", JobPriority::Background, [&](const CancellationToken& token) -> bool
        {
            bgStarted.store(true, std::memory_order_release);
            while (!token.IsCancelled())
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            return token.IsCancelled();
        });

        while (!bgStarted.load(std::memory_order_acquire))
            std::this_thread::yield();
        bgCancelFuture.Cancel();
        TEST_CHECK(bgCancelFuture.GetToken().IsCancelled());
        TEST_CHECK(bgCancelFuture.Get() == true);
        TEST_CHECK(normalFuture.Get() == 64);

        // AsyncIO High & Normal priority jobs
        auto ioNormalFuture = jobs.AsyncIO("ReadArchiveChunk", JobPriority::Normal, [](const CancellationToken&) -> std::string
        {
            return "ArchivePayload";
        });
        auto ioHighFuture = jobs.AsyncIO("ReadStreamTexture", JobPriority::High, [](const CancellationToken&) -> uint32_t
        {
            return 2048u;
        });
        TEST_CHECK(ioHighFuture.Get() == 2048u);
        TEST_CHECK(ioNormalFuture.Get() == "ArchivePayload");

        // Exception propagation through TaskFuture<T>
        auto throwingFuture = jobs.Async("ThrowingJob", JobPriority::High, [](const CancellationToken&) -> int
        {
            throw std::runtime_error("Expected test error");
        });
        bool caughtException = false;
        try
        {
            (void)throwingFuture.Get();
        }
        catch (const std::runtime_error&)
        {
            caughtException = true;
        }
        TEST_CHECK(caughtException);

        // ---------------------------------------------------------------------
        // 8. SystemGraph Automatic Read/Write Conflict DAG & DumpDot
        // ---------------------------------------------------------------------
        static_assert(TypeId<TransformData>() != TypeId<VelocityData>());
        static_assert(TypeId<TransformData>() == TypeId<const TransformData&>());

        ComponentAccess readA;
        readA.Read<TransformData>();
        ComponentAccess readA2;
        readA2.Read<TransformData>();
        ComponentAccess writeA;
        writeA.Write<TransformData>();
        TEST_CHECK(!readA.ConflictsWith(readA2));
        TEST_CHECK(readA.ConflictsWith(writeA));
        TEST_CHECK(writeA.ConflictsWith(readA));
        TEST_CHECK(writeA.ConflictsWith(writeA));

        SystemGraph graph("FrameGraph");
        std::atomic<int> stepCounter{ 0 };
        int integrateOrder = -1;
        int collisionOrder = -1;
        int velocityAuxOrder = -1;
        int renderGatherOrder = -1;
        int postStepOrder = -1;

        // System 0: Writes TransformData, Reads VelocityData
        auto sysIntegrate = graph.AddSystem("IntegrateTransforms",
            ComponentAccess{}.Read<VelocityData>().Write<TransformData>(),
            [&]()
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                integrateOrder = stepCounter.fetch_add(1, std::memory_order_acq_rel);
            });

        // System 1: Also Reads VelocityData only (no conflict with System 0! Can run in parallel with System 0)
        auto sysVelocityAux = graph.AddSystem("InspectVelocity",
            ComponentAccess{}.Read<VelocityData>(),
            [&]()
            {
                velocityAuxOrder = stepCounter.fetch_add(1, std::memory_order_acq_rel);
            });
        (void)sysVelocityAux;

        // System 2: Reads TransformData, Writes PhysicsManifold (conflicts with System 0's Write<TransformData>)
        auto sysCollision = graph.AddSystem("DetectCollisions",
            ComponentAccess{}.Read<TransformData>().Write<PhysicsManifold>(),
            [&]()
            {
                collisionOrder = stepCounter.fetch_add(1, std::memory_order_acq_rel);
            });

        // System 3: Reads TransformData, Writes RenderPacket (conflicts with System 0, but NOT with System 2!)
        auto sysRenderGather = graph.AddSystem("GatherRenderPackets",
            ComponentAccess{}.Read<TransformData>().Write<RenderPacket>(),
            [&]()
            {
                renderGatherOrder = stepCounter.fetch_add(1, std::memory_order_acq_rel);
            });

        // System 4: Explicit RunAfter(sysCollision) and RunAfter(sysRenderGather)
        auto sysPostStep = graph.AddSystem("FrameComplete",
            ComponentAccess{},
            [&]()
            {
                postStepOrder = stepCounter.fetch_add(1, std::memory_order_acq_rel);
            });
        graph.RunAfter(sysPostStep, sysCollision);
        graph.RunAfter(sysPostStep, sysRenderGather);
        (void)sysIntegrate;

        graph.Run();

        TEST_CHECK(integrateOrder >= 0);
        TEST_CHECK(velocityAuxOrder >= 0);
        TEST_CHECK(collisionOrder > integrateOrder);
        TEST_CHECK(renderGatherOrder > integrateOrder);
        TEST_CHECK(postStepOrder > collisionOrder);
        TEST_CHECK(postStepOrder > renderGatherOrder);

        const std::filesystem::path dotPath = std::filesystem::temp_directory_path() / "noxtasks_test_graph.dot";
        TEST_CHECK(graph.DumpDot(dotPath));
        TEST_CHECK(std::filesystem::exists(dotPath));
        {
            std::ifstream dotFile(dotPath);
            std::string dotContents((std::istreambuf_iterator<char>(dotFile)), std::istreambuf_iterator<char>());
            TEST_CHECK(dotContents.find("IntegrateTransforms") != std::string::npos);
            TEST_CHECK(dotContents.find("DetectCollisions") != std::string::npos);
            TEST_CHECK(dotContents.find("GatherRenderPackets") != std::string::npos);
        }
        std::error_code ec;
        std::filesystem::remove(dotPath, ec);
    }
    TEST_CHECK(!JobSystem::IsInitialized());

    // Profiler observer hooks must have been invoked across workers and tasks
    TEST_CHECK(workerStartCount.load() >= 1);
    TEST_CHECK(taskBeginCount.load() >= 10);
    TEST_CHECK(taskEndCount.load() == taskBeginCount.load());

    std::printf("=== NoxTasks Results: %d passed, %d failed ===\n", g_TestsPassed, g_TestsFailed);
    return g_TestsFailed == 0 ? 0 : 1;
}
