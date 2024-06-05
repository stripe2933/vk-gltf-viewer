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

find_package(glfw3 CONFIG REQUIRED)
find_package(Vulkan REQUIRED)

CPMAddPackage("gh:stripe2933/VulkanMemoryAllocator-Hpp#master")

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

if (NOT TARGET VulkanMemoryAllocator-Hpp_module)
	add_library(VulkanMemoryAllocator-Hpp_module)
	target_sources(VulkanMemoryAllocator-Hpp_module PUBLIC
		FILE_SET CXX_MODULES
		BASE_DIRS ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src
		FILES ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm
	)
	target_link_libraries(VulkanMemoryAllocator-Hpp_module PUBLIC 
		VulkanMemoryAllocator-Hpp
		Vulkan-Hpp::module
	)
	target_compile_definitions(VulkanMemoryAllocator-Hpp_module PUBLIC 
		VULKAN_MEMORY_ALLOCATOR_HPP_USE_MODULE
	)

	add_library(VulkanMemoryAllocator-Hpp::module ALIAS VulkanMemoryAllocator-Hpp_module)
endif()

target_compile_definitions(Vulkan-Hpp_module
PUBLIC
	VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1
PRIVATE
	VULKAN_HPP_NO_SMART_HANDLE
	$<$<PLATFORM_ID:Darwin>:VK_ENABLE_BETA_EXTENSIONS>
)

target_compile_definitions(VulkanMemoryAllocator-Hpp_module PRIVATE 
	VMA_STATIC_VULKAN_FUNCTIONS=0
	VMA_DYNAMIC_VULKAN_FUNCTIONS=1
	VMA_VULKAN_VERSION=1002000 # MoltenVK supports Vulkan 1.2.
)

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer main.cpp)
target_link_libraries(vk-gltf-viewer PRIVATE 
	glfw
	Vulkan-Hpp::module
	VulkanMemoryAllocator-Hpp::module
)