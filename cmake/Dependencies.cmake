include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# All dependencies are pinned to exact tags/commits for reproducible builds.
FetchContent_Declare(glm   GIT_REPOSITORY https://github.com/g-truc/glm.git   GIT_TAG 1.0.1 GIT_SHALLOW ON)
FetchContent_Declare(entt  GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.15.0 GIT_SHALLOW ON)
FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.5.3 GIT_SHALLOW ON)

FetchContent_Declare(lua   GIT_REPOSITORY https://github.com/lua/lua.git   GIT_TAG v5.4.8 GIT_SHALLOW ON SOURCE_SUBDIR _none)
FetchContent_Declare(sol2  GIT_REPOSITORY https://github.com/ThePhD/sol2.git GIT_TAG v3.5.0 GIT_SHALLOW ON SOURCE_SUBDIR _none)

FetchContent_MakeAvailable(glm entt lua sol2)

# Lua ships no CMake build; compile the interpreter core as a static C library (no lua.c / luac.c).
# No platform defines on purpose: dlopen-based native module loading stays disabled.
file(GLOB LUA_SOURCES ${lua_SOURCE_DIR}/*.c)
list(FILTER LUA_SOURCES EXCLUDE REGEX ".*/(lua|luac|onelua|ltests)\\.c$")
add_library(LuaLib STATIC ${LUA_SOURCES})
target_include_directories(LuaLib SYSTEM PUBLIC ${lua_SOURCE_DIR})
set_target_properties(LuaLib PROPERTIES C_STANDARD 99 POSITION_INDEPENDENT_CODE ON)

# sol2 is header-only; sol2's own CMake is skipped (it can fetch its own Lua).
add_library(Sol2 INTERFACE)
target_include_directories(Sol2 SYSTEM INTERFACE ${sol2_SOURCE_DIR}/include)
target_link_libraries(Sol2 INTERFACE LuaLib)
target_compile_definitions(Sol2 INTERFACE SOL_ALL_SAFETIES_ON=1)
if(LUMEN_BUILD_TESTS)
    FetchContent_MakeAvailable(doctest)
endif()
