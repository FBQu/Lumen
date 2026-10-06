include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# All dependencies are pinned to exact tags/commits for reproducible builds.
FetchContent_Declare(glm   GIT_REPOSITORY https://github.com/g-truc/glm.git   GIT_TAG 1.0.1 GIT_SHALLOW ON)
FetchContent_Declare(entt  GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.15.0 GIT_SHALLOW ON)
FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.5.3 GIT_SHALLOW ON)

FetchContent_Declare(lua   GIT_REPOSITORY https://github.com/lua/lua.git   GIT_TAG v5.4.8 GIT_SHALLOW ON SOURCE_SUBDIR _none)
FetchContent_Declare(sol2  GIT_REPOSITORY https://github.com/ThePhD/sol2.git GIT_TAG v3.5.0 GIT_SHALLOW ON SOURCE_SUBDIR _none)

FetchContent_Declare(JoltPhysics GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git GIT_TAG v5.5.0 GIT_SHALLOW ON SOURCE_SUBDIR Build)

set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
set(INTERPROCEDURAL_OPTIMIZATION OFF CACHE BOOL "" FORCE)
set(OVERRIDE_CXX_FLAGS OFF CACHE BOOL "" FORCE)
set(CPP_RTTI_ENABLED ON CACHE BOOL "" FORCE)
set(CPP_EXCEPTIONS_ENABLED ON CACHE BOOL "" FORCE)
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

FetchContent_Declare(json  GIT_REPOSITORY https://github.com/nlohmann/json.git GIT_TAG v3.12.0 GIT_SHALLOW ON)
set(JSON_BuildTests OFF CACHE BOOL "" FORCE)
set(JSON_Install OFF CACHE BOOL "" FORCE)

FetchContent_Declare(nvrhi GIT_REPOSITORY https://github.com/NVIDIA-RTX/NVRHI.git GIT_TAG 6b96fb03e07539f08327aea76c56d55f1de9d906)
FetchContent_Declare(volk  GIT_REPOSITORY https://github.com/zeux/volk.git GIT_TAG vulkan-sdk-1.4.363.0 GIT_SHALLOW ON)

set(NVRHI_VULKAN_HEADERS_GIT_TAG vulkan-sdk-1.4.363.0 CACHE STRING "" FORCE)
set(NVRHI_INSTALL OFF CACHE BOOL "" FORCE)
set(NVRHI_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(NVRHI_WITH_VULKAN ON CACHE BOOL "" FORCE)
set(NVRHI_WITH_VALIDATION ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(glm entt lua sol2 JoltPhysics json nvrhi)

# volk loads the Vulkan loader at runtime so Lumen needs no link-time dependency on the Vulkan SDK.
# Vulkan headers come from the Vulkan-Headers target that nvrhi fetches.
FetchContent_MakeAvailable(volk)

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
