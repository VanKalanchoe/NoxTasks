# NoxTasks

[![CI](https://github.com/VanKalanchoe/NoxTasks/actions/workflows/ci.yml/badge.svg)](https://github.com/VanKalanchoe/NoxTasks/actions/workflows/ci.yml)

A standalone C++20 priority-isolated task scheduler, per-worker scratch memory allocator (`FrameArena` & `WorkerLocal<T>`), semaphore-guarded exclusive task runner (`RunTasks`), and automatic Read/Write DAG system scheduler (`SystemGraph`) built on top of **[Taskflow](https://github.com/taskflow/taskflow)**.

`NoxTasks` has **zero** engine, EnTT, SDL3, spdlog, or Tracy dependencies. It exposes lightweight callbacks for logging (`SetLogCallback`) and profiling (`SetProfilerCallbacks`), and drops directly into `NoxRHI::RenderGraph` (`nri::RGTaskDispatcher`), `NoxUI`, `NoxAudio`, and `NoxNet`.

---

## Key Features

1. **Priority-Isolated Thread Pools (`JobSystem`):**
   - **Frame Workers (`JobPriority::High`):** Execute frame-critical `SystemGraph` nodes, `ParallelFor` / `Dispatch` loops, `RunTasks`, and high-priority `Async` jobs without ever stealing long background cooks.
   - **Low-OS-Priority Background Workers (`JobPriority::Normal` & `JobPriority::Background`):** Run asset streaming and offline cooking/compression on separate lower-priority threads with admission control so at least 25% of background workers remain free for streaming (`Normal`) while `Background` jobs run.
   - **Dedicated I/O Workers (`AsyncIO`):** Priority-queued blocking disk I/O workers so file reads never starve CPU workers.
2. **Lock-Free Per-Worker Scratch Memory (`FrameArena` & `WorkerLocal<T>`):**
   - `WorkerLocal<T>` provides a 64-byte cache-line-aligned slot per thread (`GetThreadSlotCount()`) for zero-contention accumulation and deterministic main-thread merging.
   - `FrameArena` provides a 1 MB per-thread linear bump allocator (`Allocate`, `New<T>`, `NewArray<T>`) reset in $O(1)$ at frame boundaries via `ResetFrameArenas()`.
3. **Semaphore-Guarded Exclusive Task Dispatch (`RunTasks`):**
   - Matches `NoxRHI::RenderGraph`'s `nri::RGTaskDispatcher` signature (`GetWorkerCount()` + `RunTasks(name, exclusiveMasks, task)`). Tasks sharing any bit in their 32-bit `exclusiveMasks` acquire non-blocking `tf::Semaphore` locks so non-thread-safe resources never overlap.
4. **Automatic Read/Write Conflict DAG (`SystemGraph`):**
   - Systems declare `.Read<TypeA>().Write<TypeB>()` using compile-time 64-bit FNV-1a type hashing (`NoxTasks::TypeId<T>()`, zero RTTI / zero EnTT). `SystemGraph` automatically builds dependency edges between conflicting systems, runs non-conflicting systems in parallel, and exports GraphViz `.dot` files via `DumpDot()`.

---

## Quick Integration Example

```cpp
#include <NoxTasks/NoxTasks.h>

struct Transform {};
struct Velocity {};
struct Bounds {};

int main()
{
    NoxTasks::JobSystem jobs;

    // 1. Parallel loop + lock-free WorkerLocal reduction
    NoxTasks::WorkerLocal<uint64_t> localTotals;
    jobs.Dispatch("Accumulate", 10000, 64, [&](uint32_t index)
    {
        localTotals.Local() += index;
    });

    uint64_t total = 0;
    localTotals.ForEach([&](uint64_t& slot) { total += slot; });

    // 2. Automatic Read/Write SystemGraph
    NoxTasks::SystemGraph graph("FrameGraph");
    graph.AddSystem("Integrate",
        NoxTasks::ComponentAccess{}.Read<Velocity>().Write<Transform>(),
        []() { /* update transforms */ });

    graph.AddSystem("ComputeBounds",
        NoxTasks::ComponentAccess{}.Read<Transform>().Write<Bounds>(),
        []() { /* automatically runs after Integrate */ });

    graph.Run();
    jobs.ResetFrameArenas();
    return 0;
}
```

### Hooking into `NoxRHI::RenderGraph`

```cpp
struct NoxTasksRGDispatcher final : nri::RGTaskDispatcher
{
    uint32_t GetWorkerCount() const override { return NoxTasks::JobSystem::Get().GetWorkerCount(); }
    void RunTasks(std::string_view name, std::span<const uint32_t> exclusiveMasks,
                  const std::function<void(uint32_t index)>& task) override
    {
        NoxTasks::JobSystem::Get().RunTasks(name, exclusiveMasks, task);
    }
};
```

---

## Quick Start (Fresh Clone)

### 1. Install Dependencies into `vendors/`
```bat
scripts\install_dependencies.bat
```
Clones `taskflow` (header-only) into `vendors\taskflow`.

### 2. Generate the Solution (`.slnx`) for Rider / Visual Studio
```bat
:: Configure with Visual Studio 2026 + ClangCL (default), generating build\NoxTasks.slnx:
scripts\setup_build.bat

:: Or configure with default MSVC:
scripts\setup_build.bat --msvc
```

---

## CMake Options

| Option | Default | Description |
|---|---|---|
| `NOXTASKS_BUILD_EXAMPLES` | `ON` | Build standalone `TasksDemo` executable |
| `NOXTASKS_BUILD_TESTS` | `ON` | Build standalone `NoxTasksTest` test suite |
| `TASKFLOW_INCLUDE_DIR` | `vendors/taskflow` | Path to the `taskflow` header-only repository |
