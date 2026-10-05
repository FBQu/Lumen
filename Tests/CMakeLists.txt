add_executable(LumenTests
    TestMain.cpp
    CoreTests.cpp
    SceneTests.cpp
    ScriptingTests.cpp
    PhysicsTests.cpp
    SerializerTests.cpp
    AgentTests.cpp
)
target_link_libraries(LumenTests PRIVATE LumenCore doctest::doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(LumenTests)

# End-to-end: drive the real LumenAgent executable over stdin/stdout.
add_test(NAME LumenAgentStdio
    COMMAND ${CMAKE_COMMAND} -DAGENT=$<TARGET_FILE:LumenAgent> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/agent_session.jsonl -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/agent_session.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunAgentStdio.cmake)
