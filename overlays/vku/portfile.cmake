vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stripe2933/vku
    REF "v${VERSION}"
    HEAD_REF module
    SHA512 535fc5dc27d0bfa48278735fe8cec0134e3123913bab393304dc83ef061b9c94da8ed49f244970942e51b0c4b6e9889e793bf34bef537a2783543e842d27c3ca
    PATCHES vcpkg-deps.patch base-dir.patch cmake-4.0.patch no-vulkan-loader.patch
)

# Module project doesn't use header files.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

# Set CMake variables from the requested features.
vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        shaderc VKU_USE_SHADERC
        dynamic-dispatcher VKU_DEFAULT_DYNAMIC_DISPATCHER
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS}
)
vcpkg_cmake_install()

if (NOT VCPKG_BUILD_TYPE STREQUAL "release")
    file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/debug/share)
    file(RENAME ${CURRENT_PACKAGES_DIR}/debug/cmake/vku ${CURRENT_PACKAGES_DIR}/debug/share/vku)
endif()
if (NOT VCPKG_BUILD_TYPE STREQUAL "debug")
    file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/share)
    file(RENAME ${CURRENT_PACKAGES_DIR}/cmake/vku ${CURRENT_PACKAGES_DIR}/share/vku)
endif()

vcpkg_cmake_config_fixup(PACKAGE_NAME "vku")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/cmake" "${CURRENT_PACKAGES_DIR}/debug/cmake")

# file(INSTALL "${SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
# configure_file("usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
