# Follow — live AI accompaniment for singer-songwriters.
# Copyright 2026 Follow contributors. Licensed under Apache-2.0.

cmake_minimum_required(VERSION 3.24)
project(Follow VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "RelWithDebInfo" CACHE STRING "" FORCE)
endif()

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------
# The JUCE host and the MRT2 backend are macOS/Apple-Silicon targets: MRT2's C++ engine
# hard-fails on non-Apple platforms by upstream design (MLX/Metal + Apple frameworks).
# `follow_core` and its tests are deliberately free of both, so they build and run
# anywhere — which is what makes the safety logic testable in CI. See ARCHITECTURE.md §2.1.
option(FOLLOW_BUILD_APP "Build the JUCE standalone application (macOS only)" OFF)
option(FOLLOW_BUILD_TESTS "Build unit tests" ON)
set(MAGENTA_RT_DIR "" CACHE PATH "Path to a magenta/magenta-realtime checkout")

# ------------------------------------------------------------------------------
# Warnings — CLAUDE.md forbids silencing them, so they are on from the start.
# ------------------------------------------------------------------------------
add_library(follow_warnings INTERFACE)
if(MSVC)
    target_compile_options(follow_warnings INTERFACE /W4)
else()
    target_compile_options(follow_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion)
endif()

# Optional sanitizers for the portable core (CLAUDE.md: "use sanitizers where useful").
# Usage: cmake -B build-asan -DFOLLOW_SANITIZE=address,undefined
set(FOLLOW_SANITIZE "" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined")
if(FOLLOW_SANITIZE)
    add_compile_options(-fsanitize=${FOLLOW_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${FOLLOW_SANITIZE})
endif()

# ------------------------------------------------------------------------------
# follow_core — portable. C++20 standard library only. No JUCE, no MRT2, no platform.
# ------------------------------------------------------------------------------
add_library(follow_core STATIC
    src/core/EngineState.cpp
    src/core/SafetyLimiter.cpp
    src/core/SafetyMonitor.cpp
    src/core/AiOutputStage.cpp
    src/core/Logging.cpp
)
add_library(follow::core ALIAS follow_core)

target_include_directories(follow_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(follow_core PRIVATE follow_warnings)
target_compile_features(follow_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(follow_core PUBLIC Threads::Threads)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(FOLLOW_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Application (macOS only)
# ------------------------------------------------------------------------------
if(FOLLOW_BUILD_APP)
    if(NOT APPLE)
        message(FATAL_ERROR
            "FOLLOW_BUILD_APP requires macOS on Apple Silicon.\n"
            "Magenta RealTime 2's C++ engine is macOS-only by upstream design "
            "(MLX/Metal + Apple frameworks); there is no Linux or x86 real-time path.\n"
            "Configure without -DFOLLOW_BUILD_APP to build and test follow_core only.")
    endif()
    add_subdirectory(src/app)
else()
    message(STATUS "Follow: building portable core + tests only "
                   "(set -DFOLLOW_BUILD_APP=ON on macOS for the full app)")
endif()
