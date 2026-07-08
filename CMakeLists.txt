cmake_minimum_required(VERSION 3.24)
project(tracearbiter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_library(tracearbiter_lib
    src/budget.cpp
    src/calibration.cpp
    src/collector.cpp
    src/json.cpp
    src/manifest.cpp
    src/plan.cpp
    src/planner.cpp
    src/preflight.cpp
    src/session.cpp
    src/smoke.cpp
    src/source.cpp
    src/stats.cpp
    src/tracefs.cpp
    src/yaml.cpp
)
target_include_directories(tracearbiter_lib PUBLIC include)
find_package(Threads REQUIRED)
target_link_libraries(tracearbiter_lib PUBLIC Threads::Threads)
target_compile_options(tracearbiter_lib PRIVATE -Wall -Wextra -Wpedantic)

add_executable(tracearbiter src/main.cpp)
target_link_libraries(tracearbiter PRIVATE tracearbiter_lib)
target_compile_options(tracearbiter PRIVATE -Wall -Wextra -Wpedantic)

include(CTest)
if(BUILD_TESTING)
    add_executable(test_budget tests/test_budget.cpp)
    target_link_libraries(test_budget PRIVATE tracearbiter_lib)
    add_test(NAME budget COMMAND test_budget)

    add_executable(test_planner tests/test_planner.cpp)
    target_link_libraries(test_planner PRIVATE tracearbiter_lib)
    add_test(NAME planner COMMAND test_planner)

    add_executable(test_tracefs_parser tests/test_tracefs_parser.cpp)
    target_link_libraries(test_tracefs_parser PRIVATE tracearbiter_lib)
    add_test(NAME tracefs_parser COMMAND test_tracefs_parser
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

    add_executable(test_tracefs_session tests/test_tracefs_session.cpp)
    target_link_libraries(test_tracefs_session PRIVATE tracearbiter_lib)
    add_test(NAME tracefs_session COMMAND test_tracefs_session)
endif()
