#include "TaskObserver.h"

#include <string_view>

#include "NoxTasks/TasksProfiler.h"

namespace NoxTasks
{
    namespace
    {
        constexpr std::string_view UnnamedTask = "Unnamed Task";

        std::string_view NormalizeTaskName(const std::string& name)
        {
            return name.empty() ? UnnamedTask : std::string_view(name);
        }
    }

    void ProfilerTaskObserver::set_up(size_t)
    {
    }

    void ProfilerTaskObserver::on_entry(tf::WorkerView, tf::TaskView task)
    {
        NotifyTaskBegin(NormalizeTaskName(task.name()));
    }

    void ProfilerTaskObserver::on_exit(tf::WorkerView, tf::TaskView task)
    {
        NotifyTaskEnd(NormalizeTaskName(task.name()));
    }
}
