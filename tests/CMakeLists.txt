# Follow — unit tests for the portable core.
# These run on any platform, which is the point: MRT2 cannot build off Apple Silicon,
# so the safety-critical logic lives where it can be tested everywhere.

function(follow_add_test name source)
    add_executable(${name} ${source})
    target_link_libraries(${name} PRIVATE follow_core follow_warnings)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES TIMEOUT 120)
endfunction()

follow_add_test(engine_state_tests EngineStateTests.cpp)
follow_add_test(audio_safety_tests AudioSafetyTests.cpp)
follow_add_test(backend_tests BackendTests.cpp)
