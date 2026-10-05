add_executable(LumenTests
    TestMain.cpp
    CoreTests.cpp
    SceneTests.cpp
    ScriptingTests.cpp
    PhysicsTests.cpp
    SerializerTests.cpp
)
target_link_libraries(LumenTests PRIVATE LumenCore doctest::doctest)

include(${doctest_SOURCE_DIR}/scripts/cmake/doctest.cmake)
doctest_discover_tests(LumenTests)
