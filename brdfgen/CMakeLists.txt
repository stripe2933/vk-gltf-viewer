# --------------------
# External Dependencies.
# --------------------

find_package(Ktx CONFIG REQUIRED)
find_package(vku CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

# --------------------
# CMake Targets.
# --------------------

add_executable(brdfgen main.cpp impl.cpp)
target_link_libraries(brdfgen PRIVATE KTX::ktx vku::vku)

# --------------------
# Shaders.
# --------------------

if (${Vulkan_glslc_FOUND})
    set(GLSL_SPIRV_COMPILER Vulkan::glslc)
elseif (${Vulkan_glslangValidator_FOUND})
    set(GLSL_SPIRV_COMPILER Vulkan::glslangValidator)
else()
    message(FATAL_ERROR "No GLSL -> SPIR-V compiler found.")
endif ()

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/brdfmap.comp.spv
    COMMAND ${GLSL_SPIRV_COMPILER} ${CMAKE_CURRENT_SOURCE_DIR}/shader/brdfmap.comp -o ${CMAKE_CURRENT_BINARY_DIR}/shader/brdfmap.comp.spv
    DEPENDS shader/brdfmap.comp
)
target_sources(brdfgen PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shader/brdfmap.comp.spv)
target_compile_definitions(brdfgen PRIVATE COMPILED_SHADER_DIR="${CMAKE_CURRENT_BINARY_DIR}/shader")