include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# All dependencies are pinned to exact tags/commits for reproducible builds.
FetchContent_Declare(glm   GIT_REPOSITORY https://github.com/g-truc/glm.git   GIT_TAG 1.0.1 GIT_SHALLOW ON)
FetchContent_Declare(entt  GIT_REPOSITORY https://github.com/skypjack/entt.git GIT_TAG v3.15.0 GIT_SHALLOW ON)
FetchContent_Declare(doctest GIT_REPOSITORY https://github.com/doctest/doctest.git GIT_TAG v2.5.3 GIT_SHALLOW ON)

FetchContent_MakeAvailable(glm entt)
if(LUMEN_BUILD_TESTS)
    FetchContent_MakeAvailable(doctest)
endif()
