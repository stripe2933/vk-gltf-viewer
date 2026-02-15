cmake_minimum_required (VERSION 3.30)

if (CMAKE_VERSION VERSION_GREATER_EQUAL "4.0.3")
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "d0edc3af-4c50-42ea-a356-e2862fe7a444")
elseif (CMAKE_VERSION VERSION_GREATER_EQUAL "4.0.1")
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "a9e1cf81-9932-4810-974b-6eccaf14e457")
else()
    set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")
endif()

project(vk-gltf-viewer
    VERSION 0.1.0
    DESCRIPTION "Vulkan glTF Viewer"
    HOMEPAGE_URL "https://github.com/stripe2933/vk-gltf-viewer"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_MODULE_STD 1)

# --------------------
# External dependencies.
# --------------------

find_package(boost_container CONFIG REQUIRED)
find_package(boost_container_hash CONFIG REQUIRED)
find_path(BSHOSHANY_THREAD_POOL_INCLUDE_DIRS "BS_thread_pool.hpp")
find_package(cstring_view CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(meshoptimizer CONFIG REQUIRED)
find_package(nfd CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(vku CONFIG REQUIRED)

# --------------------
# Optional external dependencies.
# --------------------

find_package(CGAL CONFIG)
find_package(Ktx CONFIG)
find_package(OpenEXR CONFIG)

# --------------------
# Module configurations for the external dependencies.
# --------------------

add_library(BS_thread_pool_module)
target_sources(BS_thread_pool_module PUBLIC FILE_SET CXX_MODULES FILES extlibs/module-ports/BS.thread_pool.cppm)
target_include_directories(BS_thread_pool_module PRIVATE ${BSHOSHANY_THREAD_POOL_INCLUDE_DIRS})
target_compile_definitions(BS_thread_pool_module PRIVATE BS_THREAD_POOL_IMPORT_STD)
add_library(BS_thread_pool::module ALIAS BS_thread_pool_module)

add_library(cstring_view_module)
target_sources(cstring_view_module PUBLIC FILE_SET CXX_MODULES FILES extlibs/module-ports/cstring_view.cppm)
target_link_libraries(cstring_view_module PRIVATE cpp_util::cstring_view)
add_library(cpp_util::cstring_view::module ALIAS cstring_view_module)

add_library(fmt_module)
target_sources(fmt_module PUBLIC FILE_SET CXX_MODULES FILES extlibs/module-ports/fmt.cc)
target_link_libraries(fmt_module PRIVATE fmt::fmt)
set_source_files_properties(extlibs/module-ports/fmt.cc PROPERTIES COMPILE_DEFINITIONS "FMT_IMPORT_STD;FMT_ATTACH_TO_GLOBAL_MODULE")
add_library(fmt::module ALIAS fmt_module)

get_target_property(glm_INCLUDE_DIR glm::glm-header-only INTERFACE_INCLUDE_DIRECTORIES)

add_library(glm_module)
target_sources(glm_module PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS ${glm_INCLUDE_DIR}/glm
    FILES ${glm_INCLUDE_DIR}/glm/glm.cppm
)
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

get_target_property(
    VulkanMemoryAllocator_Hpp_INCLUDE_DIR
    unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
    INTERFACE_INCLUDE_DIRECTORIES
)

add_library(vku_module)
target_sources(vku_module PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS
        ${VulkanMemoryAllocator_Hpp_INCLUDE_DIR}/vulkan-memory-allocator-hpp
        extlibs/module-ports
    FILES
        ${VulkanMemoryAllocator_Hpp_INCLUDE_DIR}/vulkan-memory-allocator-hpp/vk_mem_alloc.cppm
        extlibs/module-ports/vulkan.cppm
        extlibs/module-ports/vku.cppm
)
target_compile_definitions(vku_module
    PUBLIC
        VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
    PRIVATE
        VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1 VMA_ENABLE_VULKAN_HPP_MODULE
        VULKAN_HPP_NO_SMART_HANDLE
        VULKAN_HPP_NO_TO_STRING
        $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
        $<$<PLATFORM_ID:Darwin>:VK_USE_PLATFORM_METAL_EXT VK_ENABLE_BETA_EXTENSIONS> # For VK_KHR_portability_subset availability.
)
target_link_libraries(vku_module PUBLIC vku::vku)
add_library(vku::module ALIAS vku_module)

# --------------------
# Internal dependencies.
# --------------------

include(cmake/CompileShader.cmake)

add_subdirectory(bloom)
add_subdirectory(cubemap)
add_subdirectory(ibl)
set(VKGLTF_USE_MIKKTSPACE ON)
set(VKGLTF_USE_KTX ${Ktx_FOUND})
set(VKGLTF_USE_BINDLESS ON)
add_subdirectory(vkgltf)

if (APPLE)
    add_subdirectory(metal-cpp)
endif()

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer WIN32 MACOSX_BUNDLE
    impl.cpp
    impl/control/ImGuiTaskCollector.cpp
    impl/MainApp.cpp
    impl/mod.cpp
    impl/vulkan/Frame.cpp
    impl/vulkan/Gpu.cpp
    main.cpp
)
target_include_directories(vk-gltf-viewer PRIVATE
    extlibs/include
    ${Stb_INCLUDE_DIR}
    include
)
target_sources(vk-gltf-viewer PRIVATE
    FILE_SET CXX_MODULES
    BASE_DIRS extlibs interface
    FILES
        extlibs/module-ports/imgui/imgui.cppm
        extlibs/module-ports/imgui/imgui.glfw.cppm
        extlibs/module-ports/imgui/imgui.internal.cppm
        extlibs/module-ports/imgui/imgui.vulkan.cppm
        extlibs/module-ports/ImGuizmo.cppm
        extlibs/reflect.cppm
        interface/mod.cppm
        interface/asset.cppm
        interface/AppState.cppm
        interface/control/AppWindow.cppm
        interface/control/Camera.cppm
        interface/control/ImGuiTaskCollector.cppm
        interface/control/Task.cppm
        interface/global.cppm
        interface/gltf/algorithm/miniball.cppm
        interface/gltf/Animation.cppm
        interface/gltf/AssetExtended.cppm
        interface/gltf/AssetExternalBuffers.cppm
        interface/gltf/AssetProcessError.cppm
        interface/gltf/SceneHierarchy.cppm
        interface/gltf/util.cppm
        interface/gui/popup/mod.cppm
        interface/gui/utils.cppm
        interface/helpers/concepts.cppm
        interface/helpers/fastgltf.cppm
        interface/helpers/Flags.cppm
        interface/helpers/formatter/ByteSize.cppm
        interface/helpers/full_optional.cppm
        interface/helpers/functional.cppm
        interface/helpers/io.cppm
        interface/helpers/Lazy.cppm
        interface/helpers/optional.cppm
        interface/helpers/PairHasher.cppm
        interface/helpers/ranges/mod.cppm
        interface/helpers/ranges/concat.cppm
        interface/helpers/TempStringBuffer.cppm
        interface/helpers/type_map.cppm
        interface/imgui/mod.cppm
        interface/imgui/ColorSpaceAndUsageCorrectedTextures.cppm
        interface/imgui/UserData.cppm
        interface/imgui/GuiTextures.cppm
        interface/imgui/scoped.cppm
        interface/imgui/util.cppm
        interface/imgui/widget.cppm
        interface/MainApp.cppm
        interface/math/bit.cppm
        interface/math/Frustum.cppm
        interface/math/Plane.cppm
        interface/Renderer.cppm
        interface/shader_selector/mask_node_mouse_picking_frag.cppm
        interface/shader_selector/mask_node_mouse_picking_vert.cppm
        interface/shader_selector/mask_jump_flood_seed_frag.cppm
        interface/shader_selector/mask_jump_flood_seed_vert.cppm
        interface/shader_selector/mask_multi_node_mouse_picking_frag.cppm
        interface/shader_selector/primitive_frag.cppm
        interface/shader_selector/primitive_vert.cppm
        interface/shader_selector/unlit_primitive_frag.cppm
        interface/shader_selector/unlit_primitive_vert.cppm
        interface/vulkan/attachment_group/MousePicking.cppm
        interface/vulkan/attachment_group/JumpFloodSeed.cppm
        interface/vulkan/attachment_group/Scene.cppm
        interface/vulkan/attachment_group/ImGui.cppm
        interface/vulkan/buffer/IndirectDrawCommands.cppm
        interface/vulkan/buffer/Materials.cppm
        interface/vulkan/buffer/PrimitiveAttributes.cppm
        interface/vulkan/descriptor_set_layout/Asset.cppm
        interface/vulkan/descriptor_set_layout/BloomApply.cppm
        interface/vulkan/descriptor_set_layout/ImageBasedLighting.cppm
        interface/vulkan/descriptor_set_layout/InverseToneMapping.cppm
        interface/vulkan/descriptor_set_layout/MousePicking.cppm
        interface/vulkan/descriptor_set_layout/Outline.cppm
        interface/vulkan/descriptor_set_layout/Renderer.cppm
        interface/vulkan/descriptor_set_layout/Skybox.cppm
        interface/vulkan/descriptor_set_layout/WeightedBlendedComposition.cppm
        interface/vulkan/Frame.cppm
        interface/vulkan/FrameDeferredTask.cppm
        interface/vulkan/gltf/AssetExtended.cppm
        interface/vulkan/Gpu.cppm
        interface/vulkan/imgui/GuiTextures.cppm
        interface/vulkan/mipmap.cppm
        interface/vulkan/pipeline/BloomApplyRenderPipeline.cppm
        interface/vulkan/pipeline/TonemappingRenderPipeline.cppm
        interface/vulkan/pipeline/GridRenderPipeline.cppm
        interface/vulkan/pipeline/InverseToneMappingRenderPipeline.cppm
        interface/vulkan/pipeline/JumpFloodComputePipeline.cppm
        interface/vulkan/pipeline/JumpFloodSeedRenderPipeline.cppm
        interface/vulkan/pipeline/MultiNodeMousePickingRenderPipeline.cppm
        interface/vulkan/pipeline/NodeMousePickingRenderPipeline.cppm
        interface/vulkan/pipeline/OutlineRenderPipeline.cppm
        interface/vulkan/pipeline/PrepassPipelineConfig.cppm
        interface/vulkan/pipeline/PrimitiveRenderPipeline.cppm
        interface/vulkan/pipeline/SkyboxRenderPipeline.cppm
        interface/vulkan/pipeline/UnlitPrimitiveRenderPipeline.cppm
        interface/vulkan/pipeline/WeightedBlendedCompositionRenderPipeline.cppm
        interface/vulkan/pipeline_layout/BloomApply.cppm
        interface/vulkan/pipeline_layout/InverseToneMapping.cppm
        interface/vulkan/pipeline_layout/MousePicking.cppm
        interface/vulkan/pipeline_layout/Outline.cppm
        interface/vulkan/pipeline_layout/Primitive.cppm
        interface/vulkan/pipeline_layout/PrimitiveNoShading.cppm
        interface/vulkan/pipeline_layout/Skybox.cppm
        interface/vulkan/pipeline_layout/WeightedBlendedComposition.cppm
        interface/vulkan/render_pass/BloomApply.cppm
        interface/vulkan/render_pass/Tonemapping.cppm
        interface/vulkan/render_pass/Scene.cppm
        interface/vulkan/sampler/BrdfLut.cppm
        interface/vulkan/sampler/Cubemap.cppm
        interface/vulkan/shader_type/Material.cppm
        interface/vulkan/SharedData.cppm
        interface/vulkan/specialization_constants/SpecializationMap.cppm
        interface/vulkan/Swapchain.cppm
        interface/vulkan/texture/Checkerboard.cppm
        interface/vulkan/texture/Fallback.cppm
        interface/vulkan/texture/ImGuiColorSpaceAndUsageCorrectedTextures.cppm
        interface/vulkan/texture/Textures.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
    bloom
    Boost::container
    Boost::container_hash
    BS_thread_pool::module
    cpp_util::cstring_view::module
    cubemap
    fmt::module
    glm::module
    ibl
    imgui::imgui
    imguizmo::imguizmo
    meshoptimizer::meshoptimizer
    nfd::nfd
    vkgltf::bindless
    vku::module
    $<$<PLATFORM_ID:Linux>:-lX11> # Starting from ImGui v1.92.3, using GLFW backend on Linux/BSD etc. requires linking with -lX11.
)
target_compile_definitions(vk-gltf-viewer PRIVATE
    GLFW_INCLUDE_NONE
)

if (CGAL_FOUND)
    target_link_libraries(vk-gltf-viewer PRIVATE CGAL::CGAL)
    target_compile_definitions(vk-gltf-viewer PRIVATE EXACT_BOUNDING_VOLUME_USING_CGAL)
else()
    message(STATUS "CGAL not found, using approximate bounding volume calculation.")
endif()

if (Ktx_FOUND)
    target_compile_definitions(vk-gltf-viewer PRIVATE SUPPORT_KHR_TEXTURE_BASISU)
else()
    message(STATUS "KTX not found, KHR_texture_basisu extension will not be supported.")
endif()

if (OpenEXR_FOUND)
    target_link_libraries(vk-gltf-viewer PRIVATE OpenEXR::OpenEXR)
    target_compile_definitions(vk-gltf-viewer PRIVATE SUPPORT_EXR_SKYBOX)
else()
    message(STATUS "OpenEXR not found, loading EXR skybox will not be supported.")
endif()

if (APPLE)
    target_link_libraries(vk-gltf-viewer PRIVATE metal-cpp::metal-cpp)

    set_target_properties(vk-gltf-viewer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "${PROJECT_DESCRIPTION}"
    )

    if (CI)
        find_package(Vulkan COMPONENTS MoltenVK REQUIRED)

        set(MOLTENVK_ICD "asset/resources/vulkan")
        target_sources(vk-gltf-viewer PRIVATE ${MOLTENVK_ICD})
        set_target_properties(vk-gltf-viewer PROPERTIES RESOURCE ${MOLTENVK_ICD})

        # Embed libMoltenVK.dylib in the bundle
        add_custom_command(TARGET vk-gltf-viewer POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy ${Vulkan_MoltenVK_LIBRARY} "${CMAKE_CURRENT_BINARY_DIR}/vk-gltf-viewer.app/Contents/Frameworks")
    endif()
endif()

# --------------------
# Shader compilation.
# --------------------

target_link_shaders(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    FILES
        shaders/grid.frag
        shaders/grid.vert
        shaders/jump_flood_seed.frag
        shaders/jump_flood_seed.vert
        shaders/jump_flood.comp
        shaders/multi_node_mouse_picking.frag
        shaders/node_mouse_picking.vert
        shaders/outline.frag
        shaders/screen_quad.vert
        shaders/skybox.frag
        shaders/skybox.vert
)

target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    MACRO_DEFS "SEPARATE_IMAGE_SAMPLER=$<PLATFORM_ID:Darwin>"
    FILES
        shaders/node_mouse_picking.frag
    MACRO_NAMES "KHR_SHADER_ATOMIC_INT64"
    MACRO_VALUES 0 1
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    FILES
        shaders/mask_jump_flood_seed.vert
        shaders/mask_node_mouse_picking.vert
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_0_ALPHA_ATTRIBUTE"
    MACRO_VALUES
        "0 0" "0 1"
        "1 0" "1 1"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    MACRO_DEFS "SEPARATE_IMAGE_SAMPLER=$<PLATFORM_ID:Darwin>"
    FILES
        shaders/mask_node_mouse_picking.frag
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_0_ALPHA_ATTRIBUTE" "KHR_SHADER_ATOMIC_INT64"
    MACRO_VALUES
        "0 0 0" "0 0 1" "0 1 0" "0 1 1"
        "1 0 0" "1 0 1" "1 1 0" "1 1 1"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    MACRO_DEFS "SEPARATE_IMAGE_SAMPLER=$<PLATFORM_ID:Darwin>"
    FILES
        shaders/mask_jump_flood_seed.frag
        shaders/mask_multi_node_mouse_picking.frag
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_0_ALPHA_ATTRIBUTE"
    MACRO_VALUES
        "0 0" "0 1"
        "1 0" "1 1"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    FILES shaders/primitive.vert
    MACRO_NAMES "TEXCOORD_COUNT" "HAS_COLOR_0_ATTRIBUTE" "FRAGMENT_SHADER_GENERATED_TBN"
    MACRO_VALUES
        "0 0 0" "0 0 1" "0 1 0" "0 1 1"
        "1 0 0" "1 0 1" "1 1 0" "1 1 1"
        "2 0 0" "2 0 1" "2 1 0" "2 1 1"
        "3 0 0" "3 0 1" "3 1 0" "3 1 1"
        "4 0 0" "4 0 1" "4 1 0" "4 1 1"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    MACRO_DEFS "SEPARATE_IMAGE_SAMPLER=$<PLATFORM_ID:Darwin>"
    FILES shaders/primitive.frag
    MACRO_NAMES "TEXCOORD_COUNT" "HAS_COLOR_0_ATTRIBUTE" "FRAGMENT_SHADER_GENERATED_TBN" "ALPHA_MODE" "EXT_SHADER_STENCIL_EXPORT"
    MACRO_VALUES
        "0 0 0 0 0" "0 0 0 1 0" "0 0 0 2 0" "0 0 1 0 0" "0 0 1 1 0" "0 0 1 2 0"
        "0 1 0 0 0" "0 1 0 1 0" "0 1 0 2 0" "0 1 1 0 0" "0 1 1 1 0" "0 1 1 2 0"
        "1 0 0 0 0" "1 0 0 1 0" "1 0 0 2 0" "1 0 1 0 0" "1 0 1 1 0" "1 0 1 2 0"
        "1 1 0 0 0" "1 1 0 1 0" "1 1 0 2 0" "1 1 1 0 0" "1 1 1 1 0" "1 1 1 2 0"
        "2 0 0 0 0" "2 0 0 1 0" "2 0 0 2 0" "2 0 1 0 0" "2 0 1 1 0" "2 0 1 2 0"
        "2 1 0 0 0" "2 1 0 1 0" "2 1 0 2 0" "2 1 1 0 0" "2 1 1 1 0" "2 1 1 2 0"
        "3 0 0 0 0" "3 0 0 1 0" "3 0 0 2 0" "3 0 1 0 0" "3 0 1 1 0" "3 0 1 2 0"
        "3 1 0 0 0" "3 1 0 1 0" "3 1 0 2 0" "3 1 1 0 0" "3 1 1 1 0" "3 1 1 2 0"
        "4 0 0 0 0" "4 0 0 1 0" "4 0 0 2 0" "4 0 1 0 0" "4 0 1 1 0" "4 0 1 2 0"
        "4 1 0 0 0" "4 1 0 1 0" "4 1 0 2 0" "4 1 1 0 0" "4 1 1 1 0" "4 1 1 2 0"
        "0 0 0 0 1" "0 0 0 1 1" "0 0 0 2 1" "0 0 1 0 1" "0 0 1 1 1" "0 0 1 2 1"
        "0 1 0 0 1" "0 1 0 1 1" "0 1 0 2 1" "0 1 1 0 1" "0 1 1 1 1" "0 1 1 2 1"
        "1 0 0 0 1" "1 0 0 1 1" "1 0 0 2 1" "1 0 1 0 1" "1 0 1 1 1" "1 0 1 2 1"
        "1 1 0 0 1" "1 1 0 1 1" "1 1 0 2 1" "1 1 1 0 1" "1 1 1 1 1" "1 1 1 2 1"
        "2 0 0 0 1" "2 0 0 1 1" "2 0 0 2 1" "2 0 1 0 1" "2 0 1 1 1" "2 0 1 2 1"
        "2 1 0 0 1" "2 1 0 1 1" "2 1 0 2 1" "2 1 1 0 1" "2 1 1 1 1" "2 1 1 2 1"
        "3 0 0 0 1" "3 0 0 1 1" "3 0 0 2 1" "3 0 1 0 1" "3 0 1 1 1" "3 0 1 2 1"
        "3 1 0 0 1" "3 1 0 1 1" "3 1 0 2 1" "3 1 1 0 1" "3 1 1 1 1" "3 1 1 2 1"
        "4 0 0 0 1" "4 0 0 1 1" "4 0 0 2 1" "4 0 1 0 1" "4 0 1 1 1" "4 0 1 2 1"
        "4 1 0 0 1" "4 1 0 1 1" "4 1 0 2 1" "4 1 1 0 1" "4 1 1 1 1" "4 1 1 2 1"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    FILES shaders/unlit_primitive.vert
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_0_ATTRIBUTE"
    MACRO_VALUES
        "0 0" "0 1"
        "1 0" "1 1"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    MACRO_DEFS "SEPARATE_IMAGE_SAMPLER=$<PLATFORM_ID:Darwin>"
    FILES shaders/unlit_primitive.frag
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_0_ATTRIBUTE" "ALPHA_MODE"
    MACRO_VALUES
        "0 0 0" "0 0 1" "0 0 2" "0 1 0" "0 1 1" "0 1 2"
        "1 0 0" "1 0 1" "1 0 2" "1 1 0" "1 1 1" "1 1 2"
)
target_link_shader_variants(vk-gltf-viewer PRIVATE
    TARGET_ENV vulkan1.2
    FILES
        shaders/bloom_apply.frag
        shaders/tonemapping.frag
        shaders/inverse_tone_mapping.frag
        shaders/weighted_blended_composition.frag
    MACRO_NAMES "AMD_SHADER_TRINARY_MINMAX"
    MACRO_VALUES 0 1
)