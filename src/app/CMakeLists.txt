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
if(NOT MAGENTA_RT_DIR)
    message(FATAL_ERROR
        "MAGENTA_RT_DIR is not set.\n"
        "Point it at a checkout of https://github.com/magenta/magenta-realtime, e.g.\n"
        "  cmake -B build -DGHOSTBAND_BUILD_APP=ON -DMAGENTA_RT_DIR=/path/to/magenta-realtime")
endif()
if(NOT EXISTS "${MAGENTA_RT_DIR}/core/CMakeLists.txt")
    message(FATAL_ERROR
        "MAGENTA_RT_DIR='${MAGENTA_RT_DIR}' does not look like a magenta-realtime checkout "
        "(no core/CMakeLists.txt).")
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
