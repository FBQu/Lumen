add_library(LumenCore STATIC
    src/Lumen/Core/Log.cpp
    src/Lumen/Core/UUID.cpp
    src/Lumen/Scene/Scene.cpp
)
target_include_directories(LumenCore PUBLIC src)
target_link_libraries(LumenCore PUBLIC glm::glm EnTT::EnTT)
target_compile_features(LumenCore PUBLIC cxx_std_20)
target_compile_definitions(LumenCore PUBLIC GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE)
if(MSVC)
    target_compile_options(LumenCore PRIVATE /W4 /WX)
else()
    target_compile_options(LumenCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
