include(${PROJECT_SOURCE_DIR}/cmake/Shaders.cmake)
lumen_embed_shaders(LUMEN_SHADER_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/shaders
    mesh.vert pbr.frag fullscreen.vert tonemap.frag)

add_library(LumenCore STATIC
    src/Lumen/Agent/AgentSession.cpp
    src/Lumen/Assets/ImageIO.cpp
    src/Lumen/Core/Log.cpp
    src/Lumen/Core/UUID.cpp
    src/Lumen/Physics/PhysicsWorld.cpp
    src/Lumen/Renderer/Mesh.cpp
    src/Lumen/Renderer/RenderDevice.cpp
    src/Lumen/Renderer/Renderer.cpp
    src/Lumen/Scene/Scene.cpp
    src/Lumen/Scene/SceneSerializer.cpp
    src/Lumen/Scripting/ScriptEngine.cpp
    ${LUMEN_SHADER_SOURCES}
)
target_include_directories(LumenCore PUBLIC src)
target_link_libraries(LumenCore PUBLIC glm::glm EnTT::EnTT nlohmann_json::nlohmann_json nvrhi_vk nvrhi
    PRIVATE Sol2 Jolt volk Vulkan::Headers Stb)
target_compile_definitions(LumenCore PRIVATE VK_NO_PROTOTYPES VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1)
target_include_directories(LumenCore SYSTEM PRIVATE ${JoltPhysics_SOURCE_DIR}/..)
target_compile_features(LumenCore PUBLIC cxx_std_20)
target_compile_definitions(LumenCore PUBLIC GLM_ENABLE_EXPERIMENTAL GLM_FORCE_DEPTH_ZERO_TO_ONE)
if(MSVC)
    target_compile_options(LumenCore PRIVATE /W4 /WX)
else()
    target_compile_options(LumenCore PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
