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
find_package(Ktx CONFIG REQUIRED)
find_package(unofficial-imguizmo CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(vku CONFIG REQUIRED)

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer
    impl.cpp
    impl/control/AppWindow.cpp
    impl/control/ImGuiTaskCollector.cpp
    impl/AppState.cpp
    impl/gltf/AssetResources.cpp
    impl/gltf/SceneResources.cpp
    impl/MainApp.cpp
    impl/mod.cpp
    impl/vulkan/Frame.cpp
    impl/vulkan/Gpu.cpp
    impl/vulkan/pipeline/BrdfmapComputer.cpp
    impl/vulkan/pipeline/DepthRenderer.cpp
    impl/vulkan/pipeline/JumpFloodComputer.cpp
    impl/vulkan/pipeline/MaskDepthRenderer.cpp
    main.cpp
)
target_include_directories(vk-gltf-viewer PRIVATE
    ${Stb_INCLUDE_DIR}
)
target_sources(vk-gltf-viewer PRIVATE
    FILE_SET CXX_MODULES
    FILES
        extlibs/thread_pool.cppm
        extlibs/module-ports/glm.cppm
        interface/mod.cppm
        interface/AppState.cppm
        interface/control/AppWindow.cppm
        interface/control/Camera.cppm
        interface/control/ImGuiTaskCollector.cppm
        interface/control/Task.cppm
        interface/gltf/algorithm/MikktSpaceInterface.cppm
        interface/gltf/AssetExternalBuffers.cppm
        interface/gltf/AssetResources.cppm
        interface/gltf/AssetTextures.cppm
        interface/gltf/SceneResources.cppm
        interface/helpers/cstring_view.cppm
        interface/helpers/extended_arithmetic.cppm
        interface/helpers/fastgltf.cppm
        interface/helpers/formatters/joiner.cppm
        interface/helpers/full_optional.cppm
        interface/helpers/functional.cppm
        interface/helpers/imgui/mod.cppm
        interface/helpers/imgui/table.cppm
        interface/helpers/ranges/mod.cppm
        interface/helpers/ranges/concat.cppm
        interface/helpers/ranges/contains.cppm
        interface/helpers/tristate.cppm
        interface/helpers/type_variant.cppm
        interface/io/StbDecoder.cppm
        interface/MainApp.cppm
        interface/vulkan/attachment_group/DepthPrepass.cppm
        interface/vulkan/attachment_group/JumpFloodSeed.cppm
        interface/vulkan/attachment_group/SceneOpaque.cppm
        interface/vulkan/attachment_group/SceneWeightedBlended.cppm
        interface/vulkan/attachment_group/Swapchain.cppm
        interface/vulkan/buffer/CubeIndices.cppm
        interface/vulkan/descriptor_set_layout/Asset.cppm
        interface/vulkan/descriptor_set_layout/ImageBasedLighting.cppm
        interface/vulkan/descriptor_set_layout/Scene.cppm
        interface/vulkan/descriptor_set_layout/Skybox.cppm
        interface/vulkan/Frame.cppm
        interface/vulkan/generator/ImageBasedLightingResourceGenerator.cppm
        interface/vulkan/generator/MipmappedCubemapGenerator.cppm
        interface/vulkan/Gpu.cppm
        interface/vulkan/mipmap.cppm
        interface/vulkan/pipeline/BlendFacetedPrimitiveRenderer.cppm
        interface/vulkan/pipeline/BlendPrimitiveRenderer.cppm
        interface/vulkan/pipeline/BrdfmapComputer.cppm
        interface/vulkan/pipeline/CubemapComputer.cppm
        interface/vulkan/pipeline/CubemapToneMappingRenderer.cppm
        interface/vulkan/pipeline/DepthRenderer.cppm
        interface/vulkan/pipeline/FacetedPrimitiveRenderer.cppm
        interface/vulkan/pipeline/JumpFloodComputer.cppm
        interface/vulkan/pipeline/JumpFloodSeedRenderer.cppm
        interface/vulkan/pipeline/MaskDepthRenderer.cppm
        interface/vulkan/pipeline/MaskFacetedPrimitiveRenderer.cppm
        interface/vulkan/pipeline/MaskJumpFloodSeedRenderer.cppm
        interface/vulkan/pipeline/MaskPrimitiveRenderer.cppm
        interface/vulkan/pipeline/MultiplyComputer.cppm
        interface/vulkan/pipeline/OutlineRenderer.cppm
        interface/vulkan/pipeline/PrefilteredmapComputer.cppm
        interface/vulkan/pipeline/PrimitiveRenderer.cppm
        interface/vulkan/pipeline/SphericalHarmonicCoefficientsSumComputer.cppm
        interface/vulkan/pipeline/SphericalHarmonicsComputer.cppm
        interface/vulkan/pipeline/SubgroupMipmapComputer.cppm
        interface/vulkan/pipeline/SkyboxRenderer.cppm
        interface/vulkan/pipeline/WeightedBlendedCompositionRenderer.cppm
        interface/vulkan/pipeline_layout/SceneRendering.cppm
        interface/vulkan/render_pass/CubemapToneMapping.cppm
        interface/vulkan/render_pass/Scene.cppm
        interface/vulkan/sampler/BrdfLutSampler.cppm
        interface/vulkan/sampler/CubemapSampler.cppm
        interface/vulkan/sampler/SingleTexelSampler.cppm
        interface/vulkan/shader/BlendPrimitiveFragment.cppm
        interface/vulkan/shader/FacetedPrimitiveTessellation.cppm
        interface/vulkan/shader/FacetedPrimitiveVertex.cppm
        interface/vulkan/shader/MaskPrimitiveFragment.cppm
        interface/vulkan/shader/PrimitiveFragment.cppm
        interface/vulkan/shader/PrimitiveVertex.cppm
        interface/vulkan/shader/ScreenQuadVertex.cppm
        interface/vulkan/SharedData.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
    Boost::container
    CGAL::CGAL
    fastgltf::fastgltf
    imgui::imgui
    KTX::ktx
    unofficial::imguizmo::imguizmo
    mikktspace::mikktspace
    nfd::nfd
    vku::vku
)
target_compile_definitions(vk-gltf-viewer PRIVATE
    COMPILED_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
    GLFW_INCLUDE_NONE
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_XYZW_ONLY
    GLM_ENABLE_EXPERIMENTAL
    GLM_GTC_INLINE_NAMESPACE
    GLM_EXT_INLINE_NAMESPACE
    GLM_GTX_INLINE_NAMESPACE
)