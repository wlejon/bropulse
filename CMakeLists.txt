cmake_minimum_required(VERSION 3.24)

project(bropulse VERSION 0.1.0 LANGUAGES C CXX)

# Dependencies (cmake/bro_deps.cmake): an outer project's target wins (under bro
# these are all bro's), else the ../<name> working tree beside the top-level
# project, else the pinned commit, fetched at configure.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/bro_deps.cmake)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(_bropulse_top_level OFF)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    set(_bropulse_top_level ON)
endif()

option(BROPULSE_BUILD_TESTS "Build bropulse tests" ${_bropulse_top_level})

# Memory-bounded parallelism for ninja
if(_bropulse_top_level AND CMAKE_GENERATOR MATCHES "Ninja")
    if(NOT DEFINED BRO_COMPILE_JOBS)
        set(BRO_COMPILE_JOBS 4)
    endif()
    if(NOT DEFINED BRO_LINK_JOBS)
        set(BRO_LINK_JOBS 2)
    endif()
    set_property(GLOBAL PROPERTY JOB_POOLS compile_jobs=${BRO_COMPILE_JOBS} link_jobs=${BRO_LINK_JOBS})
    set(CMAKE_JOB_POOL_COMPILE compile_jobs)
    set(CMAKE_JOB_POOL_LINK link_jobs)
endif()

function(bropulse_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8 /MP)
        target_compile_definitions(${target} PRIVATE _CRT_SECURE_NO_WARNINGS)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

if(MSVC)
    # The DLL CRT, so the tests share one heap with bronze_runtime_shared.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()

find_package(Threads REQUIRED)

set(BROPULSE_SOURCES
    src/stream_graph.cpp
    src/policy.cpp
    src/router.cpp
)

# The PipeWire and PulseAudio routers are Linux's audio servers. Elsewhere the
# stream graph and policy still build, and AudioRouter::create() finds no
# backend, so bro.pulse reports no sinks rather than failing to build.
set(BROPULSE_LINUX_BACKENDS OFF)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(PIPEWIRE REQUIRED IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(PULSE REQUIRED IMPORTED_TARGET libpulse)
    list(APPEND BROPULSE_SOURCES
        src/pipewire/pw_router.cpp
        src/pulse/pa_router.cpp
    )
    set(BROPULSE_LINUX_BACKENDS ON)
endif()

add_library(bropulse STATIC ${BROPULSE_SOURCES})
add_library(bropulse::bropulse ALIAS bropulse)
bropulse_warnings(bropulse)
target_compile_definitions(bropulse PRIVATE
    BROPULSE_LINUX_BACKENDS=$<BOOL:${BROPULSE_LINUX_BACKENDS}>)

target_include_directories(bropulse
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(bropulse PUBLIC Threads::Threads)
if(BROPULSE_LINUX_BACKENDS)
    target_link_libraries(bropulse PUBLIC PkgConfig::PIPEWIRE PkgConfig::PULSE)
endif()

# --- Standalone Bronze JavaScript API ---
option(BROPULSE_ENABLE_API "Build Bronze JavaScript API" ${PROJECT_IS_TOP_LEVEL})
if(BROPULSE_ENABLE_API)
    # bronze, the JavaScript runtime the api binds (it brings its code
    # generator, brass).
    bro_dependency(bronze GITHUB wlejon/bronze TARGET bronze_runtime_shared
                   OPTIONS BRONZE_BUILD_SHARED_RUNTIME=ON BRONZE_BUILD_TESTS=OFF)

    add_subdirectory(src/api)
endif()

if(BROPULSE_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

