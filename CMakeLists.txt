cmake_minimum_required(VERSION 3.25)

project(NoxTasks VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_VS_GLOBALS "UseMultiToolTask=true" "EnforceProcessCountAcrossBuilds=true")
endif()
if(MSVC AND NOT CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    add_compile_options($<$<COMPILE_LANGUAGE:CXX>:/MP>)
endif()

option(NOXTASKS_BUILD_EXAMPLES "Build standalone NoxTasks examples (TasksDemo)" ON)
option(NOXTASKS_BUILD_TESTS    "Build standalone NoxTasks test suite (NoxTasksTest)" ON)

# -----------------------------------------------------------------------------
# Third-Party Dependencies (Taskflow header-only library)
# -----------------------------------------------------------------------------
set(TASKFLOW_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendors/taskflow" CACHE PATH "Path to Taskflow root directory")

if(NOT EXISTS "${TASKFLOW_INCLUDE_DIR}/taskflow/taskflow.hpp")
    message(FATAL_ERROR "taskflow not found at '${TASKFLOW_INCLUDE_DIR}'. Run scripts/install_dependencies.bat first.")
endif()

# -----------------------------------------------------------------------------
# Core NoxTasks Static Library
# -----------------------------------------------------------------------------
file(GLOB_RECURSE NOXTASKS_HEADERS CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/include/NoxTasks/*.h"
)

file(GLOB_RECURSE NOXTASKS_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
)

add_library(NoxTasks STATIC ${NOXTASKS_HEADERS} ${NOXTASKS_SOURCES})
add_library(NoxTasks::Core ALIAS NoxTasks)
add_library(NoxTasks::NoxTasks ALIAS NoxTasks)

target_include_directories(NoxTasks
    PUBLIC
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_include_directories(NoxTasks SYSTEM PRIVATE
    "${TASKFLOW_INCLUDE_DIR}"
)
target_compile_features(NoxTasks PUBLIC cxx_std_20)
target_compile_definitions(NoxTasks
    PRIVATE
        $<$<BOOL:${WIN32}>:NOMINMAX>
        $<$<BOOL:${WIN32}>:WIN32_LEAN_AND_MEAN>
        _CRT_SECURE_NO_WARNINGS
)

# -----------------------------------------------------------------------------
# Standalone Examples & Tests
# -----------------------------------------------------------------------------
if(NOXTASKS_BUILD_EXAMPLES)
    add_executable(TasksDemo examples/TasksDemo/main.cpp)
    target_link_libraries(TasksDemo PRIVATE NoxTasks::Core)
    target_compile_features(TasksDemo PRIVATE cxx_std_20)
endif()

if(NOXTASKS_BUILD_TESTS)
    enable_testing()
    add_executable(NoxTasksTest tests/NoxTasksTest/main.cpp)
    target_link_libraries(NoxTasksTest PRIVATE NoxTasks::Core)
    target_compile_features(NoxTasksTest PRIVATE cxx_std_20)
    add_test(NAME NoxTasksTest COMMAND NoxTasksTest)
endif()
