# GhostBand — JUCE standalone host (macOS / Apple Silicon only).
#
# ⚠️ NEVER CONFIGURED OR COMPILED as of this commit — the development machine is Linux,
# where MRT2 refuses to configure by design. See KNOWN_ISSUES.md §1.

include(FetchContent)

# Pinned to the latest stable 8.x. JUCE 9.0.0 exists but GhostBand's code is written against
# the 8.x API and that has not been re-verified; bumping is a deliberate task, not a
# default. Licensing implications of JUCE are flagged in THIRD_PARTY_NOTICES.md §3.
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.15
    GIT_SHALLOW    ON
)
message(STATUS "GhostBand: fetching JUCE...")
FetchContent_MakeAvailable(JUCE)

# --- Magenta RealTime 2 -------------------------------------------------------
#
# Auto-detect before demanding. The documented command line carries a
# `/path/to/magenta-realtime` placeholder, and that placeholder has been pasted verbatim
# at least once — so the common case of a checkout sitting next to or inside the GhostBand
# directory should just work, and the failure message should say where it looked.
set(GHOSTBAND_MRT_CANDIDATES
    "${CMAKE_SOURCE_DIR}/magenta-realtime"
    "${CMAKE_SOURCE_DIR}/../magenta-realtime"
    "$ENV{HOME}/magenta-realtime"
    "$ENV{HOME}/Documents/magenta-realtime"
    "$ENV{HOME}/src/magenta-realtime")

# Detect when MAGENTA_RT_DIR is unset **or** points somewhere invalid. The invalid case
# matters because CMake caches the variable: one bad configure leaves the wrong value in
# build/CMakeCache.txt, and a value that is set-but-wrong would otherwise suppress
# detection on every subsequent run until the cache is cleared by hand.
set(ghostband_mrt_was_set "${MAGENTA_RT_DIR}")
if(NOT MAGENTA_RT_DIR OR NOT EXISTS "${MAGENTA_RT_DIR}/core/CMakeLists.txt")
    foreach(candidate IN LISTS GHOSTBAND_MRT_CANDIDATES)
        if(EXISTS "${candidate}/core/CMakeLists.txt")
            get_filename_component(MAGENTA_RT_DIR "${candidate}" ABSOLUTE)
            # Overwrite the cache entry too, or the stale value comes back next configure.
            set(MAGENTA_RT_DIR "${MAGENTA_RT_DIR}" CACHE PATH
                "Path to a magenta/magenta-realtime checkout" FORCE)

            if(ghostband_mrt_was_set)
                # Loud, not silent: quietly building against a different checkout than the
                # one that was asked for would be worse than the error it replaces.
                message(WARNING
                    "MAGENTA_RT_DIR was '${ghostband_mrt_was_set}', which has no "
                    "core/CMakeLists.txt. Using ${MAGENTA_RT_DIR} instead.")
            else()
                message(STATUS "GhostBand: found magenta-realtime at ${MAGENTA_RT_DIR}")
            endif()
            break()
        endif()
    endforeach()
endif()

if(NOT MAGENTA_RT_DIR OR NOT EXISTS "${MAGENTA_RT_DIR}/core/CMakeLists.txt")
    # One message for both cases. "Not set" and "set to the wrong place" have the same fix,
    # and listing the searched paths turns a guess into a check.
    if(MAGENTA_RT_DIR)
        set(why "MAGENTA_RT_DIR='${MAGENTA_RT_DIR}' has no core/CMakeLists.txt.")
    else()
        set(why "MAGENTA_RT_DIR is not set and no checkout was found automatically.")
    endif()

    string(REPLACE ";" "\n    " searched "${GHOSTBAND_MRT_CANDIDATES}")
    message(FATAL_ERROR
        "${why}\n"
        "Searched:\n    ${searched}\n\n"
        "Point it at your checkout of https://github.com/magenta/magenta-realtime.\n"
        "To find it:  find ~ -maxdepth 4 -type d -name magenta-realtime\n"
        "Then:        cmake -B build -DGHOSTBAND_BUILD_APP=ON "
        "-DMAGENTA_RT_DIR=<the path you just found>\n"
        "Note the placeholder above is not a real path - substitute your own.")
endif()

# Upstream's root CMakeLists pulls in MLX, TFLite and SentencePiece, then defines
# magentart::core. We add the whole project rather than just core/ because core/ depends
# on those FetchContent targets existing.
add_subdirectory("${MAGENTA_RT_DIR}" "${CMAKE_BINARY_DIR}/magenta-rt" EXCLUDE_FROM_ALL)

# --- Application --------------------------------------------------------------
juce_add_gui_app(GhostBand
    PRODUCT_NAME "GhostBand"
    COMPANY_NAME "GhostBand"
    BUNDLE_ID "app.ghostband.standalone"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT
        "GhostBand needs audio input only for the experimental Guitar Follow feature."
)

target_sources(GhostBand PRIVATE
    Main.cpp
    MainComponent.cpp
    PerformanceView.cpp
    FootControlPanel.cpp
    SongEditorView.cpp
    GhostBandAudioEngine.cpp
    ../backend/Mrt2Backend.cpp
)

juce_generate_juce_header(GhostBand)

target_compile_definitions(GhostBand PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:GhostBand,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:GhostBand,JUCE_VERSION>"
    # GhostBand is a live instrument: the audio callback must never be interrupted by an
    # assertion dialog. Device errors and MIDI disconnects are expected conditions.
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(GhostBand PRIVATE
    ghostband_core
    magentart::core
    juce::juce_audio_utils
    juce::juce_audio_devices
    juce::juce_gui_extra
    PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags
)

target_include_directories(GhostBand PRIVATE ${CMAKE_SOURCE_DIR}/src)
