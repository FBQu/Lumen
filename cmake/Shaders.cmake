# lumen_embed_shaders(<out-sources-var> <shader-dir> <shader files...>)
# Shaders may #include "*.glsl" files from the shader directory (GL_GOOGLE_include_directive).
# Compiles GLSL (Vulkan 1.3 target) to SPIR-V with glslangValidator and embeds each shader as a C++ byte array named
# g_Shader_<file>_<stage> (e.g. mesh.vert -> g_Shader_mesh_vert). Requires glslangValidator (Vulkan SDK or glslang-tools).
find_program(GLSLANG_VALIDATOR glslangValidator HINTS $ENV{VULKAN_SDK}/bin $ENV{VULKAN_SDK}/Bin)
if(NOT GLSLANG_VALIDATOR)
    message(FATAL_ERROR "glslangValidator not found. Install the Vulkan SDK or the glslang tools "
                        "(Ubuntu: sudo apt install glslang-tools) and make sure it is on PATH or VULKAN_SDK is set.")
endif()

function(lumen_embed_shaders OUT_VAR SHADER_DIR)
    set(GENERATED_SOURCES)
    foreach(SHADER ${ARGN})
        string(REPLACE "." "_" SYMBOL_NAME ${SHADER})
        set(SYMBOL g_Shader_${SYMBOL_NAME})
        set(SPIRV ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.spv)
        set(CPP ${CMAKE_CURRENT_BINARY_DIR}/shaders/${SHADER}.cpp)
        add_custom_command(
            OUTPUT ${CPP}
            COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
            COMMAND ${GLSLANG_VALIDATOR} -V --target-env vulkan1.3 -I${SHADER_DIR} -o ${SPIRV} ${SHADER_DIR}/${SHADER}
            COMMAND ${CMAKE_COMMAND} -DINPUT=${SPIRV} -DOUTPUT=${CPP} -DSYMBOL=${SYMBOL} -P ${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake
            DEPENDS ${SHADER_DIR}/${SHADER} ${SHADER_INCLUDES} ${PROJECT_SOURCE_DIR}/cmake/EmbedBinary.cmake
            COMMENT "Compiling shader ${SHADER}"
            VERBATIM)
        list(APPEND GENERATED_SOURCES ${CPP})
    endforeach()
    set(${OUT_VAR} ${GENERATED_SOURCES} PARENT_SCOPE)
endfunction()
