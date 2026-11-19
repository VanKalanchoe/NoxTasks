#include "NoxTasks/TasksProfiler.h"

#include <mutex>

namespace NoxTasks
{
    namespace
    {
        std::mutex g_ProfilerMutex;
        ProfilerCallbacks g_ProfilerCallbacks;
    }

    void SetProfilerCallbacks(ProfilerCallbacks callbacks)
    {
        std::scoped_lock lock(g_ProfilerMutex);
        g_ProfilerCallbacks = std::move(callbacks);
    }

    void NotifyWorkerThreadStart(const char* threadName)
    {
        std::function<void(const char*)> cb;
        {
            std::scoped_lock lock(g_ProfilerMutex);
            cb = g_ProfilerCallbacks.OnWorkerThreadStart;
        }
        if (cb)
            cb(threadName);
    }

    void NotifyTaskBegin(std::string_view taskName)
    {
        std::function<void(std::string_view)> cb;
        {
            std::scoped_lock lock(g_ProfilerMutex);
            cb = g_ProfilerCallbacks.OnTaskBegin;
        }
        if (cb)
            cb(taskName);
    }

    void NotifyTaskEnd(std::string_view taskName)
    {
        std::function<void(std::string_view)> cb;
        {
            std::scoped_lock lock(g_ProfilerMutex);
            cb = g_ProfilerCallbacks.OnTaskEnd;
        }
        if (cb)
            cb(taskName);
    }
}
