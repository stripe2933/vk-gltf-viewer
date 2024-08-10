cmake_minimum_required (VERSION 3.30)

# Enable CMake's experimental standard library module support.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

project(vk-gltf-viewer LANGUAGES CXX)

set(CMAKE_CXX_MODULE_STD 1)

# --------------------
# External dependencies.
# --------------------

find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(unofficial-imguizmo CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(vku CONFIG REQUIRED)
find_package(Vulkan COMPONENTS shaderc_combined REQUIRED)

add_subdirectory(extlibs)

# --------------------
# Module configurations for the external dependencies.
# --------------------

if (NOT TARGET glm_module)
	add_library(glm_module)
	target_sources(glm_module PUBLIC
		FILE_SET CXX_MODULES
		FILES extlibs/module-ports/glm.cppm
	)
	target_link_libraries(glm_module PUBLIC glm::glm)
	target_compile_definitions(glm_module PUBLIC
		GLM_FORCE_DEPTH_ZERO_TO_ONE
		GLM_FORCE_XYZW_ONLY
		GLM_ENABLE_EXPERIMENTAL
		GLM_GTC_INLINE_NAMESPACE
		GLM_EXT_INLINE_NAMESPACE
		GLM_GTX_INLINE_NAMESPACE
	)

	add_library(glm::module ALIAS glm_module)
endif()

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer
	impl.cpp
	impl/control/AppWindow.cpp
    impl/AppState.cpp
    impl/control/ImGui.cpp
	impl/gltf/AssetResources.cpp
	impl/gltf/SceneResources.cpp
	impl/helpers/enum_to_string.cpp
	impl/MainApp.cpp
	impl/mod.cpp
	impl/vulkan/attachment_groups.cpp
	impl/vulkan/Frame.cpp
	impl/vulkan/Gpu.cpp
	impl/vulkan/pipeline/AlphaMaskedDepthRenderer.cpp
	impl/vulkan/pipeline/BrdfmapComputer.cpp
	impl/vulkan/pipeline/DepthRenderer.cpp
	impl/vulkan/pipeline/JumpFloodComputer.cpp
	impl/vulkan/pipeline/OutlineRenderer.cpp
	impl/vulkan/pipeline/SkyboxRenderer.cpp
	impl/vulkan/pipeline/SphericalHarmonicsRenderer.cpp
	impl/vulkan/SharedData.cpp
	main.cpp
)
target_include_directories(vk-gltf-viewer PRIVATE
	${Stb_INCLUDE_DIR}
)
target_sources(vk-gltf-viewer PRIVATE
	FILE_SET CXX_MODULES
	FILES
		extlibs/thread_pool.cppm
		interface/mod.cppm
		interface/control/AppWindow.cppm
		interface/control/Camera.cppm
		interface/control/ImGui.cppm
		interface/gltf/algorithm/MikktSpaceInterface.cppm
		interface/gltf/AssetResources.cppm
		interface/gltf/SceneResources.cppm
		interface/AppState.cppm
		interface/helpers/enum_to_string.cppm
		interface/helpers/extended_arithmetic.cppm
		interface/helpers/formatters/glm.cppm
		interface/helpers/formatters/joiner.cppm
		interface/helpers/formatters/omitted.cppm
		interface/helpers/full_optional.cppm
		interface/helpers/ranges.cppm
		interface/helpers/tristate.cppm
		interface/helpers/type_map.cppm
		interface/io/logger.cppm
		interface/io/StbDecoder.cppm
		interface/MainApp.cppm
		interface/mipmap.cppm
		interface/vulkan/attachment_group/JumpFloodSeed.cppm
		interface/vulkan/attachment_group/Scene.cppm
		interface/vulkan/attachment_groups.cppm
		interface/vulkan/buffer/CubeIndices.cppm
		interface/vulkan/dsl/Asset.cppm
		interface/vulkan/dsl/ImageBasedLighting.cppm
		interface/vulkan/dsl/Scene.cppm
		interface/vulkan/Frame.cppm
		interface/vulkan/Gpu.cppm
		interface/vulkan/pipeline/AlphaMaskedDepthRenderer.cppm
		interface/vulkan/pipeline/AlphaMaskedFacetedPrimitiveRenderer.cppm
		interface/vulkan/pipeline/AlphaMaskedJumpFloodSeedRenderer.cppm
		interface/vulkan/pipeline/AlphaMaskedPrimitiveRenderer.cppm
		interface/vulkan/pipeline/BrdfmapComputer.cppm
		interface/vulkan/pipeline/DepthRenderer.cppm
		interface/vulkan/pipeline/FacetedPrimitiveRenderer.cppm
		interface/vulkan/pipeline/JumpFloodComputer.cppm
		interface/vulkan/pipeline/JumpFloodSeedRenderer.cppm
		interface/vulkan/pipeline/OutlineRenderer.cppm
		interface/vulkan/pipeline/PrimitiveRenderer.cppm
		interface/vulkan/pipeline/SkyboxRenderer.cppm
		interface/vulkan/pipeline/SphericalHarmonicsRenderer.cppm
		interface/vulkan/pipeline_layout/SceneRendering.cppm
		interface/vulkan/sampler/BrdfLutSampler.cppm
		interface/vulkan/sampler/CubemapSampler.cppm
		interface/vulkan/sampler/SingleTexelSampler.cppm
		interface/vulkan/SharedData.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
	fastgltf::fastgltf
	glm::module
	imgui::imgui
	unofficial::imguizmo::imguizmo
	mikktspace::mikktspace
	pbrenvmap
	vku::vku
)
target_compile_definitions(vk-gltf-viewer PRIVATE
	GLFW_INCLUDE_NONE
	COMPILED_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)