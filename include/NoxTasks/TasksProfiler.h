#pragma once

#include <functional>
#include <string_view>

namespace NoxTasks
{
    struct ProfilerCallbacks
    {
        std::function<void(const char* threadName)> OnWorkerThreadStart;
        std::function<void(std::string_view taskName)> OnTaskBegin;
        std::function<void(std::string_view taskName)> OnTaskEnd;
    };

    using ProfilerHooks = ProfilerCallbacks;

    void SetProfilerCallbacks(ProfilerCallbacks callbacks);
    inline void SetProfilerHooks(ProfilerHooks hooks) { SetProfilerCallbacks(std::move(hooks)); }

    void NotifyWorkerThreadStart(const char* threadName);
    void NotifyTaskBegin(std::string_view taskName);
    void NotifyTaskEnd(std::string_view taskName);
}
