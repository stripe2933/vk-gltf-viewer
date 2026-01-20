vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO KhronosGroup/Vulkan-Headers
    REF "v${VERSION}"
    SHA512 20f8ae8545757fd06954b9a75bf8f1c986d2d4f306d8dab822c57153a7ec2f7324c73a2bb1f72c9e68845d1edb6190d8ed2dc0d780bdd9ec80cb0453a389eb4d
    HEAD_REF main
)

set(VCPKG_BUILD_TYPE release) # header-only port

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DVULKAN_HEADERS_ENABLE_MODULE=OFF
        -DVULKAN_HEADERS_ENABLE_TESTS=OFF
)
vcpkg_cmake_install()

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE.md")
file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

vcpkg_from_github(
    OUT_SOURCE_PATH VULKAN_HPP_SOURCE_PATH
    REPO sharadhr/Vulkan-Hpp
    REF a73bfd630e1d7fa63a24096b5b24d683b9fa4dd8
    SHA512 68bc7b1e388ecd159a4901242d2c8ddbf8017941cdbfeb69779a644ffc638b6ce69859078b6f8840a3b6acfcfad63e39160eb4ace8c77c85f5fbafe65218d9f6
    HEAD_REF vulkan-cppm-abi-breaking-style
)
file(COPY "${VULKAN_HPP_SOURCE_PATH}/vulkan" DESTINATION "${CURRENT_PACKAGES_DIR}/include")