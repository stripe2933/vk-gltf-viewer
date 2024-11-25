cmake_minimum_required (VERSION 3.30)

project(vk-gltf-viewer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_MODULE_STD 1)

# --------------------
# External dependencies.
# --------------------

find_package(boost_container CONFIG REQUIRED)
find_package(CGAL CONFIG REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(OpenEXR CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(vku CONFIG REQUIRED)

# --------------------
# Module configurations for the external dependencies.
# --------------------

add_library(fastgltf_module)
target_sources(fastgltf_module PUBLIC FILE_SET CXX_MODULES FILES extlibs/module-ports/fastgltf.ixx)
target_link_libraries(fastgltf_module PRIVATE fastgltf::fastgltf)
target_compile_definitions(fastgltf_module PRIVATE FASTGLTF_USE_STD_MODULE=1)
add_library(fastgltf::module ALIAS fastgltf_module)

add_library(glm_module)
target_sources(glm_module PUBLIC FILE_SET CXX_MODULES FILES extlibs/module-ports/glm.cppm)
target_link_libraries(glm_module PRIVATE glm::glm)
target_compile_definitions(glm_module PRIVATE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
    GLM_GTC_INLINE_NAMESPACE
    GLM_EXT_INLINE_NAMESPACE
    GLM_GTX_INLINE_NAMESPACE
)
add_library(glm::module ALIAS glm_module)

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer
    impl.cpp
    impl/control/AppWindow.cpp
    impl/control/ImGuiTaskCollector.cpp
    impl/AppState.cpp
    impl/gltf/AssetGpuBuffers.cpp
    impl/gltf/AssetSceneGpuBuffers.cpp
    impl/MainApp.cpp
    impl/mod.cpp
    impl/vulkan/buffer/IndirectDrawCommands.cpp
    impl/vulkan/Frame.cpp
    impl/vulkan/Gpu.cpp
    impl/vulkan/pipeline/BrdfmapComputer.cpp
    impl/vulkan/pipeline/JumpFloodComputer.cpp
    main.cpp
)
target_include_directories(vk-gltf-viewer PRIVATE
    ${Stb_INCLUDE_DIR}
)
target_sources(vk-gltf-viewer PRIVATE
    FILE_SET CXX_MODULES
    FILES
        extlibs/thread_pool.cppm
        extlibs/module-ports/imgui/imgui.cppm
        extlibs/module-ports/imgui/imgui.math.cppm
        extlibs/module-ports/imgui/imgui.glfw.cppm
        extlibs/module-ports/imgui/imgui.internal.cppm
        extlibs/module-ports/imgui/imgui.vulkan.cppm
        extlibs/module-ports/ImGuizmo.cppm
        interface/mod.cppm
        interface/AppState.cppm
        interface/control/AppWindow.cppm
        interface/control/Camera.cppm
        interface/control/ImGuiTaskCollector.cppm
        interface/control/Task.cppm
        interface/gltf/algorithm/bounding_box.cppm
        interface/gltf/algorithm/MikktSpaceInterface.cppm
        interface/gltf/algorithm/miniball.cppm
        interface/gltf/algorithm/traversal.cppm
        interface/gltf/AssetExternalBuffers.cppm
        interface/gltf/AssetGpuBuffers.cppm
        interface/gltf/AssetGpuFallbackTexture.cppm
        interface/gltf/AssetGpuTextures.cppm
        interface/gltf/AssetPrimitiveInfo.cppm
        interface/gltf/AssetSceneGpuBuffers.cppm
        interface/gltf/AssetSceneHierarchy.cppm
        interface/helpers/concepts.cppm
        interface/helpers/cstring_view.cppm
        interface/helpers/fastgltf.cppm
        interface/helpers/full_optional.cppm
        interface/helpers/functional.cppm
        interface/helpers/imgui/mod.cppm
        interface/helpers/imgui/table.cppm
        interface/helpers/optional.cppm
        interface/helpers/ranges/mod.cppm
        interface/helpers/ranges/concat.cppm
        interface/helpers/ranges/contains.cppm
        interface/helpers/TempStringBuffer.cppm
        interface/helpers/tristate.cppm
        interface/helpers/type_variant.cppm
        interface/MainApp.cppm
        interface/math/extended_arithmetic.cppm
        interface/math/Frustum.cppm
        interface/math/Plane.cppm
        interface/vulkan/attachment_group/DepthPrepass.cppm
        interface/vulkan/attachment_group/JumpFloodSeed.cppm
        interface/vulkan/attachment_group/SceneOpaque.cppm
        interface/vulkan/attachment_group/SceneWeightedBlended.cppm
        interface/vulkan/attachment_group/Swapchain.cppm
        interface/vulkan/buffer/CubeIndices.cppm
        interface/vulkan/buffer/IndirectDrawCommands.cppm
        interface/vulkan/descriptor_set_layout/Asset.cppm
        interface/vulkan/descriptor_set_layout/ImageBasedLighting.cppm
        interface/vulkan/descriptor_set_layout/Scene.cppm
        interface/vulkan/descriptor_set_layout/Skybox.cppm
        interface/vulkan/Frame.cppm
        interface/vulkan/generator/ImageBasedLightingResourceGenerator.cppm
        interface/vulkan/generator/MipmappedCubemapGenerator.cppm
        interface/vulkan/Gpu.cppm
        interface/vulkan/mipmap.cppm
        interface/vulkan/pipeline/BlendPrimitiveRenderer.cppm
        interface/vulkan/pipeline/BlendUnlitPrimitiveRenderer.cppm
        interface/vulkan/pipeline/BrdfmapComputer.cppm
        interface/vulkan/pipeline/CubemapComputer.cppm
        interface/vulkan/pipeline/CubemapToneMappingRenderer.cppm
        interface/vulkan/pipeline/DepthRenderer.cppm
        interface/vulkan/pipeline/JumpFloodComputer.cppm
        interface/vulkan/pipeline/JumpFloodSeedRenderer.cppm
        interface/vulkan/pipeline/MaskDepthRenderer.cppm
        interface/vulkan/pipeline/MaskJumpFloodSeedRenderer.cppm
        interface/vulkan/pipeline/MaskPrimitiveRenderer.cppm
        interface/vulkan/pipeline/MaskUnlitPrimitiveRenderer.cppm
        interface/vulkan/pipeline/MultiplyComputer.cppm
        interface/vulkan/pipeline/OutlineRenderer.cppm
        interface/vulkan/pipeline/PrefilteredmapComputer.cppm
        interface/vulkan/pipeline/PrimitiveRenderer.cppm
        interface/vulkan/pipeline/SphericalHarmonicCoefficientsSumComputer.cppm
        interface/vulkan/pipeline/SphericalHarmonicsComputer.cppm
        interface/vulkan/pipeline/SubgroupMipmapComputer.cppm
        interface/vulkan/pipeline/SkyboxRenderer.cppm
        interface/vulkan/pipeline/UnlitPrimitiveRenderer.cppm
        interface/vulkan/pipeline/WeightedBlendedCompositionRenderer.cppm
        interface/vulkan/pipeline_layout/Primitive.cppm
        interface/vulkan/pipeline_layout/PrimitiveNoShading.cppm
        interface/vulkan/render_pass/CubemapToneMapping.cppm
        interface/vulkan/render_pass/Scene.cppm
        interface/vulkan/sampler/BrdfLutSampler.cppm
        interface/vulkan/sampler/CubemapSampler.cppm
        interface/vulkan/sampler/SingleTexelSampler.cppm
        interface/vulkan/SharedData.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
    Boost::container
    CGAL::CGAL
    fastgltf::module
    glm::module
    imgui::imgui
    KTX::ktx
    imguizmo::imguizmo
    mikktspace::mikktspace
    nfd::nfd
    OpenEXR::OpenEXR
    vku::vku
)
target_compile_definitions(vk-gltf-viewer PRIVATE
    GLFW_INCLUDE_NONE
)

# --------------------
# Shader compilation.
# --------------------

include(cmake/CompileShader.cmake)

target_link_shaders(vk-gltf-viewer
    shaders/blend_faceted_primitive.frag
    shaders/blend_primitive.frag
    shaders/blend_unlit_primitive.frag
    shaders/brdfmap.comp
    shaders/cubemap_tone_mapping.frag
    shaders/cubemap.comp
    shaders/depth.frag
    shaders/depth.vert
    shaders/faceted_primitive.frag
    shaders/faceted_primitive.vert
    shaders/jump_flood_seed.frag
    shaders/jump_flood_seed.vert
    shaders/jump_flood.comp
    shaders/mask_depth.frag
    shaders/mask_depth.vert
    shaders/mask_faceted_primitive.frag
    shaders/mask_jump_flood_seed.frag
    shaders/mask_jump_flood_seed.vert
    shaders/mask_primitive.frag
    shaders/mask_unlit_primitive.frag
    shaders/multiply.comp
    shaders/outline.frag
    shaders/prefilteredmap.comp
    shaders/primitive.frag
    shaders/primitive.vert
    shaders/screen_quad.vert
    shaders/skybox.frag
    shaders/skybox.vert
    shaders/spherical_harmonic_coefficients_sum.comp
    shaders/spherical_harmonics.comp
    shaders/subgroup_mipmap_16.comp
    shaders/subgroup_mipmap_32.comp
    shaders/subgroup_mipmap_64.comp
    shaders/unlit_primitive.frag
    shaders/unlit_primitive.vert
    shaders/weighted_blended_composition.frag
)