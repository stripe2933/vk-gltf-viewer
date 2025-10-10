cmake_minimum_required(VERSION 3.24)

project(vku
    LANGUAGES CXX
    VERSION 1.4.328
)

option(VKU_DISPATCH_LOADER_DYNAMIC "Use dynamic dispatch loader.")
option(VKU_BUILD_MODULE "Build vku::module target with C++20 modules.")

find_package(Vulkan REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)

# ----- vku::vku -----

add_library(vku INTERFACE)
target_compile_features(vku INTERFACE cxx_std_20)
target_include_directories(vku INTERFACE include)
target_link_libraries(vku INTERFACE
    Vulkan::Headers
    GPUOpen::VulkanMemoryAllocator
    unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
)

# For usage consistency between FetchContent and find_package.
add_library(vku::vku ALIAS vku)

# ----- vku::module -----

if (VKU_BUILD_MODULE)
    cmake_minimum_required(VERSION 3.30)

    get_target_property(
        VulkanMemoryAllocator_Hpp_INCLUDE_DIR
        unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
        INTERFACE_INCLUDE_DIRECTORIES
    )

    add_library(vku_module)
    target_sources(vku_module PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS
            ${Vulkan_INCLUDE_DIRS}/vulkan
            ${VulkanMemoryAllocator_Hpp_INCLUDE_DIR}/vulkan-memory-allocator-hpp
            module
        FILES
            ${Vulkan_INCLUDE_DIRS}/vulkan/vulkan.cppm
            ${VulkanMemoryAllocator_Hpp_INCLUDE_DIR}/vulkan-memory-allocator-hpp/vk_mem_alloc.cppm
            module/vku.cppm
    )
    target_compile_definitions(vku_module
        PUBLIC
            $<$<BOOL:${VKU_DISPATCH_LOADER_DYNAMIC}>:VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VMA_STATIC_VULKAN_FUNCTIONS=0 VMA_DYNAMIC_VULKAN_FUNCTIONS=1>
        PRIVATE
            VMA_ENABLE_STD_MODULE=1
            VMA_ENABLE_VULKAN_HPP_MODULE
            $<$<BOOL:${MSVC}>:VULKAN_HPP_NO_SMART_HANDLE> # See https://github.com/KhronosGroup/Vulkan-Hpp/blob/main/README.md#c20-named-module for the details.
            $<$<PLATFORM_ID:Windows>:VK_USE_PLATFORM_WIN32_KHR>
            $<$<PLATFORM_ID:Darwin>:VK_USE_PLATFORM_METAL_EXT VK_ENABLE_BETA_EXTENSIONS> # For VK_KHR_portability_subset availability.
    )
    target_link_libraries(vku_module PUBLIC vku)

    set_target_properties(vku_module PROPERTIES CXX_MODULE_STD 1)

    add_library(vku::module ALIAS vku_module)
endif()