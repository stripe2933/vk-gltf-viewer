vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stripe2933/VulkanMemoryAllocator-Hpp
    REF acb5c74caaa9bdc6396669316a468d57a3ad9698
    SHA512 a13e266b5c57b2de7c798c5ce95a8d58f5f3ff7e0563b4217292b79fd1977111767fa8e8942f798b424b50a7b7281cd18c5c42e783fc26384852b48c05ab29c9
    HEAD_REF master
)

file(COPY "${SOURCE_PATH}/include/" DESTINATION "${CURRENT_PACKAGES_DIR}/include/${PORT}")

file(COPY "${SOURCE_PATH}/src/vk_mem_alloc.cppm" DESTINATION "${CURRENT_PACKAGES_DIR}/include/${PORT}")

file(COPY "${CMAKE_CURRENT_LIST_DIR}/unofficial-vulkan-memory-allocator-hpp-config.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/unofficial-${PORT}")

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
