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
	VMA_STATIC_VULKAN_FUNCTIONS=0
	VMA_DYNAMIC_VULKAN_FUNCTIONS=1
	VMA_VULKAN_VERSION=1002000 # MoltenVK supports Vulkan 1.2.
)

# --------------------
# Project targets.
# --------------------

add_executable(vk-gltf-viewer
	impl/MainApp.cpp
	main.cpp
)
target_sources(vk-gltf-viewer PRIVATE
	FILE_SET CXX_MODULES
	BASE_DIRS interface
	FILES
		interface/mod.cppm
		interface/helpers/mod.cppm
		interface/helpers/ranges.cppm
		interface/MainApp.cppm
)
target_link_libraries(vk-gltf-viewer PRIVATE 
	vku
)