cmake_minimum_required(VERSION 3.28)
project(vku
    LANGUAGES CXX
)

# ----------------
# External dependencies.
# ----------------

find_package(Vulkan COMPONENTS shaderc_combined REQUIRED)
find_package(VulkanMemoryAllocator CONFIG REQUIRED)
find_package(unofficial-vulkan-memory-allocator-hpp CONFIG REQUIRED)
if (VKU_USE_GLFW)
    find_package(glfw3 CONFIG REQUIRED)
    set(VKU_USE_GLM ON)
endif()
if (VKU_USE_GLM)
    find_package(glm CONFIG REQUIRED)
endif()

# ----------------
# Module configurations for external dependencies.
# ----------------

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
    if (NOT EXISTS ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm)
        message(STATUS "VulkanMemoryAllocator-Hpp module not found. Downloading from repository...")

        set(VulkanMemoryAllocator-Hpp_SOURCE_DIR ${PROJECT_BINARY_DIR})
        file(MAKE_DIRECTORY ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src)
        file(DOWNLOAD https://raw.githubusercontent.com/YaaZ/VulkanMemoryAllocator-Hpp/master/src/vk_mem_alloc.cppm
            ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm
        )

        # For vcpkg, vk_mem_alloc.hpp is inside the include/vulkan-memory-allocator-hpp directory.
        file(READ ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm VCPKG_MODULE_CONTENTS)
        string(REPLACE "<vk_mem_alloc.hpp>" "<vulkan-memory-allocator-hpp/vk_mem_alloc.hpp>" VCPKG_MODULE_CONTENTS "${VCPKG_MODULE_CONTENTS}")
        file(WRITE ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm "${VCPKG_MODULE_CONTENTS}")
    endif()

    add_library(VulkanMemoryAllocator-Hpp_module)
    target_sources(VulkanMemoryAllocator-Hpp_module PUBLIC
        FILE_SET CXX_MODULES
        BASE_DIRS ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src
        FILES ${VulkanMemoryAllocator-Hpp_SOURCE_DIR}/src/vk_mem_alloc.cppm
    )
    target_link_libraries(VulkanMemoryAllocator-Hpp_module PUBLIC
        Vulkan::Vulkan
        unofficial::VulkanMemoryAllocator-Hpp::VulkanMemoryAllocator-Hpp
    )

    add_library(VulkanMemoryAllocator-Hpp::module ALIAS VulkanMemoryAllocator-Hpp_module)
endif()

if (VKU_USE_GLM AND NOT TARGET glm_module)
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

# ----------------
# Project libraries.
# ----------------

add_library(vku)
target_sources(vku PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES
        interface/mod.cppm
        interface/buffers/mod.cppm
        interface/buffers/AllocatedBuffer.cppm
        interface/buffers/Buffer.cppm
        interface/buffers/MappedBuffer.cppm
        interface/commands.cppm
        interface/descriptors/mod.cppm
        interface/descriptors/DescriptorSetLayouts.cppm
        interface/descriptors/DescriptorSets.cppm
        interface/details/mod.cppm
        interface/details/concepts.cppm
        interface/details/nofield.cppm
        interface/details/ranges.cppm
        interface/details/type_traits.cppm
        interface/gpu/mod.cppm
        interface/gpu/Gpu.cppm
        interface/images/mod.cppm
        interface/images/AllocatedImage.cppm
        interface/images/Image.cppm
        interface/pipelines/mod.cppm
        interface/pipelines/Shader.cppm
        interface/rendering/mod.cppm
        interface/rendering/Attachment.cppm
        interface/rendering/AttachmentGroup.cppm
        interface/rendering/AttachmentGroupBase.cppm
        interface/rendering/MsaaAttachment.cppm
        interface/rendering/MsaaAttachmentGroup.cppm
        interface/utils/mod.cppm
        interface/utils/RefHolder.cppm
        interface/wsi/mod.cppm
        $<$<BOOL:${VKU_USE_GLFW}>:interface/wsi/GlfwWindow.cppm>
)
target_link_libraries(vku PUBLIC
    Vulkan-Hpp::module Vulkan::shaderc_combined
    VulkanMemoryAllocator-Hpp::module
    $<$<BOOL:${VKU_USE_GLFW}>:glfw>
    $<$<BOOL:${VKU_USE_GLM}>:glm::module>
)
target_compile_definitions(vku
    PRIVATE
        $<$<BOOL:${VKU_USE_GLFW}>:VKU_USE_GLFW>
        $<$<BOOL:${VKU_USE_GLM}>:VKU_USE_GLM>
    PUBLIC
        $<$<BOOL:${VKU_USE_GLFW}>:GLFW_INCLUDE_NONE>
)