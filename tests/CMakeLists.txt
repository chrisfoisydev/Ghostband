# GhostBand — unit tests for the portable core.
# These run on any platform, which is the point: MRT2 cannot build off Apple Silicon,
# so the safety-critical logic lives where it can be tested everywhere.

function(ghostband_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE ghostband_core ghostband_warnings)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

ghostband_add_test(engine_state_tests EngineStateTests.cpp)
ghostband_add_test(audio_safety_tests AudioSafetyTests.cpp)
ghostband_add_test(backend_tests BackendTests.cpp)
ghostband_add_test(harmony_tests HarmonyTests.cpp)
ghostband_add_test(intensity_tests IntensityTests.cpp)
ghostband_add_test(latency_tests LatencyTests.cpp)
ghostband_add_test(song_tests SongTests.cpp)
