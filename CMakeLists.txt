cmake_minimum_required (VERSION 3.28)
project ("vk-gltf-viewer")

set(CMAKE_CXX_STANDARD 23)

# --------------------
# Include CPM.cmake.
# --------------------

file(DOWNLOAD https://github.com/cpm-cmake/CPM.cmake/releases/latest/download/get_cpm.cmake
	${CMAKE_BINARY_DIR}/cmake/CPM.cmake
)
include(${CMAKE_BINARY_DIR}/cmake/CPM.cmake)

# --------------------
# External dependencies.
# --------------------

if (NOT MSVC) # For MSVC, use <execution> instead.
	find_package(OpenMP REQUIRED)
	# OpenMP must be linked to all C++ modules for definition consistency.
	link_libraries(OpenMP::OpenMP_CXX) # Therefore it's in here.
endif()

find_package(fastgltf CONFIG REQUIRED)
find_package(glm CONFIG REQUIRED)
find_package(Ktx CONFIG REQUIRED)
find_package(mikktspace CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(Vulkan REQUIRED)

# --------------------
# Module configurations for the external dependencies.
# --------------------

if (NOT TARGET Vulkan-Hpp_module)
	add_library(Vulkan-Hpp_module)
	target_sources(Vulkan-Hpp_module PUBLIC
		FILE_SET CXX_MODULES
		BASE_DIRS ${Vulkan_INCLUDE_DIR}/vulkan
		FILES ${Vulkan_INCLUDE_DIR}/vulkan/vulkan.cppm
	)
	target_link_libraries(Vulkan-Hpp_module PUBLIC Vulkan::Vulkan)
	add_library(Vulkan-Hpp::module ALIAS Vulkan-Hpp_module)
endif()

target_compile_definitions(Vulkan-Hpp_module
PUBLIC
	VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
PRIVATE
	VULKAN_HPP_NO_SMART_HANDLE
	$<$<PLATFORM_ID:Darwin>:VK_ENABLE_BETA_EXTENSIONS>
)

if (NOT TARGET VulkanMemoryAllocator-Hpp_module)
	add_library(VulkanMemoryAllocator-Hpp_module)
	target_sources(VulkanMemoryAllocator-Hpp_module PUBLIC
		FILE_SET CXX_MODULES
		BASE_DIRS ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src
		FILES ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm
	)
	target_link_libraries(VulkanMemoryAllocator-Hpp_module
		PUBLIC Vulkan-Hpp::module
		PRIVATE VulkanMemoryAllocator-Hpp
	)
	target_compile_definitions(VulkanMemoryAllocator-Hpp_module PRIVATE
		VULKAN_MEMORY_ALLOCATOR_HPP_USE_MODULE
	)

	add_library(VulkanMemoryAllocator-Hpp::module ALIAS VulkanMemoryAllocator-Hpp_module)
endif()

target_compile_definitions(VulkanMemoryAllocator-Hpp_module PRIVATE 
	VMA_STATIC_VULKAN_FUNCTIONS=0
	VMA_DYNAMIC_VULKAN_FUNCTIONS=1
	VMA_VULKAN_VERSION=1002000 # MoltenVK supports Vulkan 1.2.
)

if (NOT TARGET glm_module)
	if (NOT EXISTS ${glm_SOURCE_DIR}/glm/glm.cppm)
		message(STATUS "glm module not found. Downloading from repository...")

		set(glm_SOURCE_DIR ${PROJECT_BINARY_DIR})
		file(MAKE_DIRECTORY ${glm_SOURCE_DIR}/glm)
		file(DOWNLOAD https://raw.githubusercontent.com/g-truc/glm/master/glm/glm.cppm
			${glm_SOURCE_DIR}/glm/glm.cppm
		)
	endif()

	add_library(glm_module)
	target_sources(glm_module PUBLIC
		FILE_SET CXX_MODULES
		BASE_DIRS ${glm_SOURCE_DIR}
		FILES ${glm_SOURCE_DIR}/glm/glm.cppm
	)
	target_link_libraries(glm_module PUBLIC glm::glm)

	add_library(glm::module ALIAS glm_module)
endif()

target_compile_definitions(glm_module PRIVATE
	GLM_FORCE_DEPTH_ZERO_TO_ONE
	GLM_FORCE_XYZW_ONLY
)

# --------------------
# Internal dependencies.
# --------------------

set(VKU_USE_GLFW ON)
add_subdirectory(extlibs)

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer
	impl/gltf/AssetResources.cpp
	impl/gltf/SceneResources.cpp
	impl/MainApp.cpp
	impl/vulkan/frame/Frame.cpp
	impl/vulkan/frame/SharedData.cpp
	impl/vulkan/Gpu.cpp
	impl/vulkan/pipelines/BrdfmapComputer.cpp
	impl/vulkan/pipelines/DepthRenderer.cpp
	impl/vulkan/pipelines/PrimitiveRenderer.cpp
	impl/vulkan/pipelines/SkyboxRenderer.cpp
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
		interface/gltf/mod.cppm
		interface/gltf/algorithm/mod.cppm
		interface/gltf/algorithm/MikktSpaceInterface.cppm
		interface/gltf/AssetResources.cppm
		interface/gltf/SceneResources.cppm
		interface/helpers/mod.cppm
		interface/helpers/formatters/mod.cppm
		interface/helpers/formatters/glm.cppm
		interface/helpers/formatters/joiner.cppm
		interface/helpers/formatters/omitted.cppm
		interface/helpers/ranges.cppm
		interface/helpers/type_map.cppm
		interface/io/mod.cppm
		interface/io/ktxvk/mod.cppm
		interface/io/ktxvk/DeviceInfo.cppm
		interface/io/ktxvk/Texture.cppm
		interface/io/StbDecoder.cppm
		interface/MainApp.cppm
		interface/vulkan/mod.cppm
		interface/vulkan/frame/mod.cppm
		interface/vulkan/frame/Frame.cppm
		interface/vulkan/frame/SharedData.cppm
		interface/vulkan/Gpu.cppm
		interface/vulkan/pipelines/mod.cppm
		interface/vulkan/pipelines/BrdfmapComputer.cppm
		interface/vulkan/pipelines/DepthRenderer.cppm
		interface/vulkan/pipelines/PrimitiveRenderer.cppm
		interface/vulkan/pipelines/SkyboxRenderer.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE
	fastgltf::fastgltf
	KTX::ktx
	mikktspace::mikktspace
	vku
)