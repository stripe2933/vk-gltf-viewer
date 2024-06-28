cmake_minimum_required (VERSION 3.28)
project(vk-gltf-viewer
	LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)

# --------------------
# External dependencies.
# --------------------

if (NOT MSVC) # For MSVC, use <execution> instead.
	find_package(OpenMP REQUIRED)
	# OpenMP must be linked to all C++ modules for definition consistency.
	link_libraries(OpenMP::OpenMP_CXX) # Therefore it's in here.
endif()

find_package(Boost REQUIRED)
find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(ImGuizmo CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Vulkan REQUIRED)

set(VKU_USE_GLFW ON)
add_subdirectory(extlibs)

# --------------------
# Module configurations for the external dependencies.
# --------------------

target_compile_definitions(Vulkan-Hpp_module
PUBLIC
	VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
PRIVATE
	VULKAN_HPP_NO_SMART_HANDLE
	$<$<PLATFORM_ID:Darwin>:VK_ENABLE_BETA_EXTENSIONS>
)

target_compile_definitions(VulkanMemoryAllocator-Hpp_module PRIVATE
	VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
	VULKAN_HPP_NO_SMART_HANDLE
	$<$<PLATFORM_ID:Darwin>:VK_ENABLE_BETA_EXTENSIONS>
	VMA_STATIC_VULKAN_FUNCTIONS=0
	VMA_DYNAMIC_VULKAN_FUNCTIONS=1
	VMA_VULKAN_VERSION=1002000 # MoltenVK supports Vulkan 1.2.
)

target_compile_definitions(glm_module PUBLIC
	GLM_FORCE_DEPTH_ZERO_TO_ONE
	GLM_FORCE_XYZW_ONLY
)

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
	impl/vulkan/frame/Frame.cpp
	impl/vulkan/frame/SharedData.cpp
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
	main.cpp
)
target_include_directories(vk-gltf-viewer PRIVATE
	include
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
		interface/helpers/formatters/glm.cppm
		interface/helpers/formatters/joiner.cppm
		interface/helpers/formatters/omitted.cppm
		interface/helpers/full_optional.cppm
		interface/helpers/ranges.cppm
		interface/helpers/type_map.cppm
		interface/io/ktxvk/DeviceInfo.cppm
		interface/io/ktxvk/Texture.cppm
		interface/io/logger.cppm
		interface/io/StbDecoder.cppm
		interface/MainApp.cppm
		interface/vulkan/frame/Frame.cppm
		interface/vulkan/frame/SharedData.cppm
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
)
target_link_libraries(vk-gltf-viewer PRIVATE
	Boost::boost
	fastgltf::fastgltf
	imgui::imgui
	imguizmo::imguizmo
	KTX::ktx
	mikktspace::mikktspace
	pbrenvmap
	vku
)