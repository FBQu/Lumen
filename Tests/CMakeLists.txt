add_executable(LumenTests
    TestMain.cpp
    CoreTests.cpp
    SceneTests.cpp
    ScriptingTests.cpp
    PhysicsTests.cpp
    SerializerTests.cpp
    AgentTests.cpp
    Base64Tests.cpp
    EnvironmentTests.cpp
    GltfImporterTests.cpp
    HalfFloatTests.cpp
    AssetManagerTests.cpp
    ImageIOTests.cpp
    InputTests.cpp
    MeshTests.cpp
    MipChainTests.cpp
    RendererTests.cpp
    RenderDeviceTests.cpp
    RuntimeTests.cpp
    ShadowTests.cpp
)
target_link_libraries(LumenTests PRIVATE LumenCore doctest::doctest)
target_compile_definitions(LumenTests PRIVATE LUMEN_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(LumenTests)

# End-to-end: drive the real LumenAgent executable over stdin/stdout.
add_test(NAME LumenAgentStdio
    COMMAND ${CMAKE_COMMAND} -DAGENT=$<TARGET_FILE:LumenAgent> -DINPUT=${CMAKE_CURRENT_SOURCE_DIR}/agent_session.jsonl -DOUTPUT_PATH=${CMAKE_CURRENT_BINARY_DIR}/agent_session.out
            -P ${CMAKE_CURRENT_SOURCE_DIR}/RunAgentStdio.cmake)

# Window/swapchain tests: need a display. Run under Xvfb when one is available, otherwise they skip themselves.
add_executable(LumenPlatformTests PlatformTests.cpp)
target_link_libraries(LumenPlatformTests PRIVATE LumenPlatform doctest::doctest)
find_program(XVFB_RUN xvfb-run)
if(XVFB_RUN AND UNIX AND NOT APPLE)
    add_test(NAME LumenPlatformTests COMMAND ${XVFB_RUN} -a $<TARGET_FILE:LumenPlatformTests>)
else()
    add_test(NAME LumenPlatformTests COMMAND LumenPlatformTests)
endif()
