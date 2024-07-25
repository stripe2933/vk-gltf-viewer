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
find_package(ImGuizmo CONFIG REQUIRED)
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
	)

	add_library(glm::module ALIAS glm_module)
endif()

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer
	impl/control/AppWindow.cpp
    impl/AppState.cpp
    impl/control/ImGui.cpp
	impl/gltf/AssetResources.cpp
	impl/gltf/SceneResources.cpp
	impl/helpers/enum_to_string.cpp
	impl/MainApp.cpp
	impl/vulkan/attachment_groups.cpp
	impl/vulkan/Frame.cpp
	impl/vulkan/Gpu.cpp
	impl/vulkan/pipelines/AlphaMaskedDepthRenderer.cpp
	impl/vulkan/pipelines/AlphaMaskedPrimitiveRenderer.cpp
	impl/vulkan/pipelines/BrdfmapComputer.cpp
	impl/vulkan/pipelines/DepthRenderer.cpp
	impl/vulkan/pipelines/JumpFloodComputer.cpp
	impl/vulkan/pipelines/OutlineRenderer.cpp
	impl/vulkan/pipelines/PrimitiveRenderer.cpp
	impl/vulkan/pipelines/Rec709Renderer.cpp
	impl/vulkan/pipelines/SkyboxRenderer.cpp
	impl/vulkan/pipelines/SphericalHarmonicsRenderer.cpp
	impl/vulkan/SharedData.cpp
	main.cpp
)
target_include_directories(vk-gltf-viewer PRIVATE
	${Stb_INCLUDE_DIR}
)
target_sources(vk-gltf-viewer PRIVATE
	FILE_SET CXX_MODULES
	BASE_DIRS interface
	FILES
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
		interface/helpers/type_map.cppm
		interface/io/logger.cppm
		interface/io/StbDecoder.cppm
		interface/MainApp.cppm
		interface/vulkan/attachment_groups.cppm
		interface/vulkan/Frame.cppm
		interface/vulkan/Gpu.cppm
		interface/vulkan/pipelines/AlphaMaskedDepthRenderer.cppm
		interface/vulkan/pipelines/AlphaMaskedPrimitiveRenderer.cppm
		interface/vulkan/pipelines/BrdfmapComputer.cppm
		interface/vulkan/pipelines/DepthRenderer.cppm
		interface/vulkan/pipelines/JumpFloodComputer.cppm
		interface/vulkan/pipelines/OutlineRenderer.cppm
		interface/vulkan/pipelines/PrimitiveRenderer.cppm
		interface/vulkan/pipelines/Rec709Renderer.cppm
		interface/vulkan/pipelines/SkyboxRenderer.cppm
		interface/vulkan/pipelines/SphericalHarmonicsRenderer.cppm
		interface/vulkan/SharedData.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
	fastgltf::fastgltf
	glm::module
	imgui::imgui
	imguizmo::imguizmo
	mikktspace::mikktspace
	pbrenvmap
	vku::vku
)
target_compile_definitions(vk-gltf-viewer PRIVATE
	GLFW_INCLUDE_NONE
	COMPILED_SHADER_DIR="${CMAKE_CURRENT_SOURCE_DIR}/shaders"
)