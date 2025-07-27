vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stripe2933/VulkanMemoryAllocator-Hpp
    REF ff1f3dd1dc5b1652e8229678fb814d2bc7b195f2
    SHA512 2a7da930175e72591bb2e26656b610e4531ecf38a4004d878c4861801343a682d8d0845cdc0926d0ff54e8cb42074248ff1528b9b48bb2db90c911c27cf70703
    HEAD_REF master
)

file(COPY "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/${PORT}")

file(COPY "${SOURCE_PATH}/src/vk_mem_alloc.cppm" DESTINATION "${CURRENT_PACKAGES_DIR}/include/${PORT}")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/unofficial-vulkan-memory-allocator-hpp-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/unofficial-${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
