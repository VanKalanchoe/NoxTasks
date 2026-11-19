#pragma once

#include <cstddef>
#include <taskflow/taskflow.hpp>

// Internal to NoxTasks: only src/*.cpp include this header (Taskflow stays behind the module).
namespace NoxTasks
{
    // Taskflow observer hook (tf::ObserverInterface): every task any worker runs (graph tasks, parallel-for chunks,
    // async and subflow tasks) invokes the registered TasksProfiler callbacks.
    class ProfilerTaskObserver final : public tf::ObserverInterface
    {
    public:
        void set_up(size_t workerCount) override;
        void on_entry(tf::WorkerView worker, tf::TaskView task) override;
        void on_exit(tf::WorkerView worker, tf::TaskView task) override;
    };
}
