# GhostBand — live AI accompaniment for singer-songwriters.
# Copyright 2026 GhostBand contributors. Licensed under Apache-2.0.

cmake_minimum_required(VERSION 3.24)

# C is required even though GhostBand itself is pure C++20: JUCE hard-errors without a C
# compiler ("A C compiler is required to build targets that depend on JUCE"), and several
# of MRT2's transitive dependencies (TFLite, ruy, flatbuffers) contain C sources.
project(GhostBand VERSION 0.1.0 LANGUAGES C CXX)

# On Apple, MRT2's inference loop needs an Objective-C autorelease pool per iteration and
# JUCE's platform layer is Objective-C++. Upstream magenta-realtime declares
# `LANGUAGES C CXX OBJCXX` for the same reason. Enabled here rather than in the app
# subdirectory because the toolchain must be probed before any target that needs it.
# No-op off Apple, so the portable core build is unaffected.
if(APPLE)
    enable_language(OBJC)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

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
# `ghostband_core` and its tests are deliberately free of both, so they build and run
# anywhere — which is what makes the safety logic testable in CI. See ARCHITECTURE.md §2.1.
option(GHOSTBAND_BUILD_APP "Build the JUCE standalone application (macOS only)" OFF)
option(GHOSTBAND_BUILD_TESTS "Build unit tests" ON)
set(MAGENTA_RT_DIR "" CACHE PATH "Path to a magenta/magenta-realtime checkout")

# ------------------------------------------------------------------------------
# Warnings — CLAUDE.md forbids silencing them, so they are on from the start.
# ------------------------------------------------------------------------------
add_library(ghostband_warnings INTERFACE)
if(MSVC)
    target_compile_options(ghostband_warnings INTERFACE /W4)
else()
    target_compile_options(ghostband_warnings INTERFACE
        -Wall -Wextra -Wpedantic
        -Wshadow -Wconversion -Wsign-conversion
        -Wnon-virtual-dtor -Woverloaded-virtual
        -Wnull-dereference -Wdouble-promotion)
endif()

# Optional sanitizers (CLAUDE.md: "use sanitizers where useful").
#
# `add_compile_options` here applies to every target added below, so this reaches the JUCE
# app as well as the portable core — which is what caught nothing for a long time only
# because nobody ran it against the app. A UI use-after-free (KNOWN_ISSUES.md §28) is
# exactly what it is for.
#
# The cost is that it also instruments JUCE, MLX and TFLite: a cold rebuild, and MRT2 will
# not be close to real time under it. Fine for hunting memory bugs, useless for timing.
#
# Core only:  cmake -B build-asan -DGHOSTBAND_SANITIZE=address,undefined
# With app:   cmake -B build-asan -DGHOSTBAND_BUILD_APP=ON -DGHOSTBAND_SANITIZE=address
set(GHOSTBAND_SANITIZE "" CACHE STRING "Comma-separated sanitizers, e.g. address,undefined")
if(GHOSTBAND_SANITIZE)
    add_compile_options(-fsanitize=${GHOSTBAND_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${GHOSTBAND_SANITIZE})
endif()

# ------------------------------------------------------------------------------
# ghostband_core — portable. C++20 standard library only. No JUCE, no MRT2, no platform.
# ------------------------------------------------------------------------------
add_library(ghostband_core STATIC
    src/core/EngineState.cpp
    src/core/SafetyLimiter.cpp
    src/core/SafetyMonitor.cpp
    src/core/AiOutputStage.cpp
    src/core/Logging.cpp
    src/core/MidiHarmonyState.cpp
    src/core/ChordNamer.cpp
    src/core/IntensityMacro.cpp
    src/core/ControlLatency.cpp
    src/core/Song.cpp
    src/core/SectionController.cpp
    src/core/PromptSlotAllocator.cpp
    src/core/PerformanceEngine.cpp
    src/core/MidiMapping.cpp
    src/core/Setlist.cpp
    src/core/Persistence.cpp
    src/core/SongEditor.cpp
    src/core/DeviceRecovery.cpp
    src/core/ModelCatalog.cpp
    src/core/SetlistEditor.cpp
)
add_library(ghostband::core ALIAS ghostband_core)

target_include_directories(ghostband_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(ghostband_core PRIVATE ghostband_warnings)
target_compile_features(ghostband_core PUBLIC cxx_std_20)

find_package(Threads REQUIRED)
target_link_libraries(ghostband_core PUBLIC Threads::Threads)

# ------------------------------------------------------------------------------
# Tests
# ------------------------------------------------------------------------------
if(GHOSTBAND_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

# ------------------------------------------------------------------------------
# Application (macOS only)
# ------------------------------------------------------------------------------
if(GHOSTBAND_BUILD_APP)
    if(NOT APPLE)
        message(FATAL_ERROR
            "GHOSTBAND_BUILD_APP requires macOS on Apple Silicon.\n"
            "Magenta RealTime 2's C++ engine is macOS-only by upstream design "
            "(MLX/Metal + Apple frameworks); there is no Linux or x86 real-time path.\n"
            "Configure without -DGHOSTBAND_BUILD_APP to build and test ghostband_core only.")
    endif()
    add_subdirectory(src/app)
else()
    message(STATUS "GhostBand: building portable core + tests only "
                   "(set -DGHOSTBAND_BUILD_APP=ON on macOS for the full app)")
endif()
