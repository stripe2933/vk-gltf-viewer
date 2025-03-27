cmake_minimum_required (VERSION 3.30)

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
find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(imguizmo CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
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

add_library(vk-gltf-viewer_asset)
target_sources(vk-gltf-viewer_asset PUBLIC
    FILE_SET CXX_MODULES
    FILES
    asset/font/fontawesome_webfont_ttf.cppm
    asset/font/mod.cppm
    asset/mod.cppm
)
add_library(vk-gltf-viewer::asset ALIAS vk-gltf-viewer_asset)

add_executable(vk-gltf-viewer WIN32 MACOSX_BUNDLE
    impl.cpp
    impl/control/AppWindow.cpp
    impl/control/ImGuiTaskCollector.cpp
    impl/AppState.cpp
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
    FILES
        extlibs/module-ports/imgui/imgui.cppm
        extlibs/module-ports/imgui/imgui.math.cppm
        extlibs/module-ports/imgui/imgui.glfw.cppm
        extlibs/module-ports/imgui/imgui.internal.cppm
        extlibs/module-ports/imgui/imgui.vulkan.cppm
        extlibs/module-ports/ImGuizmo.cppm
        extlibs/reflect.cppm
        interface/mod.cppm
        interface/AppState.cppm
        interface/control/AppWindow.cppm
        interface/control/Camera.cppm
        interface/control/ImGuiTaskCollector.cppm
        interface/control/Task.cppm
        interface/global.cppm
        interface/gltf/algorithm/bounding_box.cppm
        interface/gltf/algorithm/MikktSpaceInterface.cppm
        interface/gltf/algorithm/miniball.cppm
        interface/gltf/algorithm/misc.cppm
        interface/gltf/algorithm/traversal.cppm
        interface/gltf/Animation.cppm
        interface/gltf/AssetExternalBuffers.cppm
        interface/gltf/AssetProcessError.cppm
        interface/gltf/data_structure/SkinJointCountExclusiveScan.cppm
        interface/gltf/data_structure/MaterialVariantsMapping.cppm
        interface/gltf/data_structure/NodeInstanceCountExclusiveScanWithCount.cppm
        interface/gltf/data_structure/SceneInverseHierarchy.cppm
        interface/gltf/data_structure/TargetWeightCountExclusiveScan.cppm
        interface/gltf/NodeWorldTransforms.cppm
        interface/gltf/OrderedPrimitives.cppm
        interface/gltf/TextureUsage.cppm
        interface/helpers/AggregateHasher.cppm
        interface/helpers/algorithm.cppm
        interface/helpers/concepts.cppm
        interface/helpers/enums/FlagTraits.cppm
        interface/helpers/enums/Flags.cppm
        interface/helpers/fastgltf.cppm
        interface/helpers/formatter/ByteSize.cppm
        interface/helpers/full_optional.cppm
        interface/helpers/functional.cppm
        interface/helpers/imgui/mod.cppm
        interface/helpers/imgui/table.cppm
        interface/helpers/io.cppm
        interface/helpers/optional.cppm
        interface/helpers/ranges/mod.cppm
        interface/helpers/ranges/concat.cppm
        interface/helpers/ranges/contains.cppm
        interface/helpers/span.cppm
        interface/helpers/TempStringBuffer.cppm
        interface/helpers/tristate.cppm
        interface/helpers/type_map.cppm
        interface/helpers/type_variant.cppm
        interface/helpers/vulkan.cppm
        interface/MainApp.cppm
        interface/math/extended_arithmetic.cppm
        interface/math/Frustum.cppm
        interface/math/Plane.cppm
        interface/shader_selector/mask_depth_frag.cppm
        interface/shader_selector/mask_depth_vert.cppm
        interface/shader_selector/mask_jump_flood_seed_frag.cppm
        interface/shader_selector/mask_jump_flood_seed_vert.cppm
        interface/shader_selector/primitive_frag.cppm
        interface/shader_selector/primitive_vert.cppm
        interface/shader_selector/unlit_primitive_frag.cppm
        interface/shader_selector/unlit_primitive_vert.cppm
        interface/vulkan/attachment_group/DepthPrepass.cppm
        interface/vulkan/attachment_group/JumpFloodSeed.cppm
        interface/vulkan/attachment_group/SceneOpaque.cppm
        interface/vulkan/attachment_group/SceneWeightedBlended.cppm
        interface/vulkan/attachment_group/Swapchain.cppm
        interface/vulkan/buffer/mod.cppm
        interface/vulkan/buffer/CombinedIndices.cppm
        interface/vulkan/buffer/CubeIndices.cppm
        interface/vulkan/buffer/IndirectDrawCommands.cppm
        interface/vulkan/buffer/InstancedNodeWorldTransforms.cppm
        interface/vulkan/buffer/InverseBindMatrices.cppm
        interface/vulkan/buffer/Materials.cppm
        interface/vulkan/buffer/MorphTargetWeights.cppm
        interface/vulkan/buffer/Nodes.cppm
        interface/vulkan/buffer/PrimitiveAttributes.cppm
        interface/vulkan/buffer/Primitives.cppm
        interface/vulkan/buffer/SkinJointIndices.cppm
        interface/vulkan/buffer/StagingBufferStorage.cppm
        interface/vulkan/descriptor_set_layout/Asset.cppm
        interface/vulkan/descriptor_set_layout/ImageBasedLighting.cppm
        interface/vulkan/descriptor_set_layout/Skybox.cppm
        interface/vulkan/Frame.cppm
        interface/vulkan/generator/ImageBasedLightingResourceGenerator.cppm
        interface/vulkan/generator/MipmappedCubemapGenerator.cppm
        interface/vulkan/Gpu.cppm
        interface/vulkan/image/Images.cppm
        interface/vulkan/mipmap.cppm
        interface/vulkan/pipeline/BrdfmapComputer.cppm
        interface/vulkan/pipeline/CubemapComputer.cppm
        interface/vulkan/pipeline/CubemapToneMappingRenderer.cppm
        interface/vulkan/pipeline/DepthRenderer.cppm
        interface/vulkan/pipeline/JumpFloodComputer.cppm
        interface/vulkan/pipeline/JumpFloodSeedRenderer.cppm
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
        interface/vulkan/sampler/BrdfLut.cppm
        interface/vulkan/sampler/Cubemap.cppm
        interface/vulkan/sampler/Samplers.cppm
        interface/vulkan/shader_type/Accessor.cppm
        interface/vulkan/shader_type/Material.cppm
        interface/vulkan/shader_type/Node.cppm
        interface/vulkan/shader_type/Primitive.cppm
        interface/vulkan/shader_type/TextureTransform.cppm
        interface/vulkan/SharedData.cppm
        interface/vulkan/specialization_constants/SpecializationMap.cppm
        interface/vulkan/texture/Fallback.cppm
        interface/vulkan/texture/Textures.cppm
        interface/vulkan/trait/PostTransferObject.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
    Boost::container
    Boost::container_hash
    BS_thread_pool::module
    cpp_util::cstring_view::module
    fastgltf::module
    glm::module
    imgui::imgui
    imguizmo::imguizmo
    mikktspace::mikktspace
    nfd::nfd
    vku::vku
    vk-gltf-viewer::asset
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
    target_link_libraries(vk-gltf-viewer PRIVATE KTX::ktx)
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

if (APPLE AND ${CMAKE_BUILD_TYPE} STREQUAL "Release")
    set_target_properties(vk-gltf-viewer PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_INFO_STRING "${PROJECT_DESCRIPTION}"
    )

    find_package(Vulkan COMPONENTS MoltenVK REQUIRED)
    set(APP_BUNDLE_CONTENTS_DIR "${CMAKE_CURRENT_BINARY_DIR}/vk-gltf-viewer.app/Contents")
    add_custom_command(TARGET vk-gltf-viewer POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${APP_BUNDLE_CONTENTS_DIR}/lib" "${APP_BUNDLE_CONTENTS_DIR}/Resources"
            && ${CMAKE_COMMAND} -E copy ${Vulkan_LIBRARY} ${Vulkan_MoltenVK_LIBRARY} "${APP_BUNDLE_CONTENTS_DIR}/lib" # Copy libvulkan.dylib, libMoltenVK.dylib
            && ${CMAKE_COMMAND} -E rename "${APP_BUNDLE_CONTENTS_DIR}/lib/libvulkan.dylib" "${APP_BUNDLE_CONTENTS_DIR}/lib/libvulkan.1.dylib"
            && ${CMAKE_COMMAND} -E copy_directory "${CMAKE_CURRENT_SOURCE_DIR}/asset/resources" "${APP_BUNDLE_CONTENTS_DIR}/Resources"
            && install_name_tool -add_rpath "@executable_path/../lib/" "${APP_BUNDLE_CONTENTS_DIR}/MacOS/vk-gltf-viewer")
endif()

# --------------------
# Shader compilation.
# --------------------

include(cmake/CompileShader.cmake)

target_link_shaders(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES
        shaders/brdfmap.comp
        shaders/cubemap_tone_mapping.frag
        shaders/cubemap.comp
        shaders/depth.frag
        shaders/depth.vert
        shaders/jump_flood_seed.frag
        shaders/jump_flood_seed.vert
        shaders/jump_flood.comp
        shaders/multiply.comp
        shaders/outline.frag
        shaders/screen_quad.vert
        shaders/skybox.frag
        shaders/skybox.vert
        shaders/spherical_harmonic_coefficients_sum.comp
        shaders/spherical_harmonics.comp
        shaders/weighted_blended_composition.frag
)

target_link_shader_variants(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES
        shaders/prefilteredmap.comp
        shaders/subgroup_mipmap.comp
    MACRO_NAMES "AMD_SHADER_IMAGE_LOAD_STORE_LOD"
    MACRO_VALUES 0 1
)
target_link_shader_variants(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES
        shaders/mask_depth.vert
        shaders/mask_depth.frag
        shaders/mask_jump_flood_seed.vert
        shaders/mask_jump_flood_seed.frag
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_ALPHA_ATTRIBUTE"
    MACRO_VALUES
        "0 0" "0 1"
        "1 0" "1 1"
)
target_link_shader_variants(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES shaders/primitive.vert
    MACRO_NAMES "TEXCOORD_COUNT" "HAS_COLOR_ATTRIBUTE" "FRAGMENT_SHADER_GENERATED_TBN"
    MACRO_VALUES
        "0 0 0" "0 0 1" "0 1 0" "0 1 1"
        "1 0 0" "1 0 1" "1 1 0" "1 1 1"
        "2 0 0" "2 0 1" "2 1 0" "2 1 1"
        "3 0 0" "3 0 1" "3 1 0" "3 1 1"
        "4 0 0" "4 0 1" "4 1 0" "4 1 1"
)
target_link_shader_variants(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES shaders/primitive.frag
    MACRO_NAMES "TEXCOORD_COUNT" "HAS_COLOR_ATTRIBUTE" "FRAGMENT_SHADER_GENERATED_TBN" "ALPHA_MODE"
    MACRO_VALUES
        "0 0 0 0" "0 0 0 1" "0 0 0 2" "0 0 1 0" "0 0 1 1" "0 0 1 2"
        "0 1 0 0" "0 1 0 1" "0 1 0 2" "0 1 1 0" "0 1 1 1" "0 1 1 2"
        "1 0 0 0" "1 0 0 1" "1 0 0 2" "1 0 1 0" "1 0 1 1" "1 0 1 2"
        "1 1 0 0" "1 1 0 1" "1 1 0 2" "1 1 1 0" "1 1 1 1" "1 1 1 2"
        "2 0 0 0" "2 0 0 1" "2 0 0 2" "2 0 1 0" "2 0 1 1" "2 0 1 2"
        "2 1 0 0" "2 1 0 1" "2 1 0 2" "2 1 1 0" "2 1 1 1" "2 1 1 2"
        "3 0 0 0" "3 0 0 1" "3 0 0 2" "3 0 1 0" "3 0 1 1" "3 0 1 2"
        "3 1 0 0" "3 1 0 1" "3 1 0 2" "3 1 1 0" "3 1 1 1" "3 1 1 2"
        "4 0 0 0" "4 0 0 1" "4 0 0 2" "4 0 1 0" "4 0 1 1" "4 0 1 2"
        "4 1 0 0" "4 1 0 1" "4 1 0 2" "4 1 1 0" "4 1 1 1" "4 1 1 2"
)
target_link_shader_variants(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES shaders/unlit_primitive.vert
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_ATTRIBUTE"
    MACRO_VALUES
        "0 0" "0 1"
        "1 0" "1 1"
)
target_link_shader_variants(vk-gltf-viewer
    TARGET_ENV vulkan1.2
    FILES shaders/unlit_primitive.frag
    MACRO_NAMES "HAS_BASE_COLOR_TEXTURE" "HAS_COLOR_ATTRIBUTE" "ALPHA_MODE"
    MACRO_VALUES
        "0 0 0" "0 0 1" "0 0 2" "0 1 0" "0 1 1" "0 1 2"
        "1 0 0" "1 0 1" "1 0 2" "1 1 0" "1 1 1" "1 1 2"
)