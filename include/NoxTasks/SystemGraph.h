#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace tf
{
    class Taskflow;
}

namespace NoxTasks
{
    using TypeHash = uint64_t;

    namespace Detail
    {
        template <typename T>
        constexpr std::string_view TypeSignature() noexcept
        {
#if defined(_MSC_VER)
            return __FUNCSIG__;
#else
            return __PRETTY_FUNCTION__;
#endif
        }

        constexpr TypeHash Fnv1a64(std::string_view str) noexcept
        {
            TypeHash hash = 14695981039346656037ull;
            for (char c : str)
            {
                hash ^= static_cast<uint8_t>(c);
                hash *= 1099511628211ull;
            }
            return hash;
        }
    }

    // Compile-time 64-bit FNV-1a type identifier (zero RTTI / zero EnTT dependency).
    template <typename T>
    constexpr TypeHash TypeId() noexcept
    {
        return Detail::Fnv1a64(Detail::TypeSignature<std::remove_cvref_t<T>>());
    }

    // Declared read/write resource access of a system node. Any type can be named: data structs, components,
    // or empty tag types for non-component resources a system produces or consumes.
    class ComponentAccess
    {
    public:
        template <typename... T>
        ComponentAccess& Read()
        {
            ((m_Reads.push_back(TypeId<T>())), ...);
            return *this;
        }

        template <typename... T>
        ComponentAccess& Write()
        {
            ((m_Writes.push_back(TypeId<T>())), ...);
            return *this;
        }

        // True when either side writes something the other reads or writes.
        bool ConflictsWith(const ComponentAccess& other) const;

    private:
        std::vector<TypeHash> m_Reads;
        std::vector<TypeHash> m_Writes;
    };

    // Automatic Read/Write DAG scheduler. Edges come from declared access: a system runs after every earlier
    // registered system it conflicts with; systems without conflicts run in parallel on the JobSystem workers.
    // Built once, run every frame.
    class SystemGraph
    {
    public:
        using SystemId = uint32_t;

        explicit SystemGraph(std::string name);
        ~SystemGraph();

        SystemGraph(const SystemGraph&) = delete;
        SystemGraph& operator=(const SystemGraph&) = delete;

        SystemId AddSystem(std::string name, ComponentAccess access, std::function<void()> run);
        // Ordering that is not a data conflict (e.g. a system consuming another system's side effect).
        void RunAfter(SystemId system, SystemId dependency);

        // Runs every system once and returns when all finished (cooperatively when called from a task).
        void Run();

        // GraphViz DOT of the built graph (tf::Taskflow::dump).
        bool DumpDot(const std::filesystem::path& path);
        const std::string& GetName() const { return m_Name; }

    private:
        struct System
        {
            std::string Name;
            ComponentAccess Access;
            std::function<void()> Run;
            std::vector<SystemId> RunAfter;
        };

        void Build();

    private:
        std::string m_Name;
        std::vector<System> m_Systems;
        std::unique_ptr<tf::Taskflow> m_Taskflow;
        bool m_Dirty = true;
    };
}
