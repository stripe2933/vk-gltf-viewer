vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stripe2933/vku
    REF v0.1.0-test12
    SHA512 41546051f3b689cb4ccf14c5024f9d207cebbeaa5aa57f09c4e100858913151524eb0d22926c743eaccd06b8379bd35f31e53bd91857899cde17431d26b60f4a
    HEAD_REF module
    PATCHES vcpkg-deps.patch
)

# Module project doesn't use header files.
set(VCPKG_POLICY_EMPTY_INCLUDE_FOLDER enabled)

# Set CMake variables from the requested features.
vcpkg_check_features(OUT_FEATURE_OPTIONS FEATURE_OPTIONS
    FEATURES
        std-module VKU_USE_STD_MODULE
        shaderc VKU_USE_SHADERC
        dynamic-dispatcher VKU_DEFAULT_DYNAMIC_DISPATCHER
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS ${FEATURE_OPTIONS}
)
vcpkg_cmake_install()

file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/debug/share)
file(RENAME ${CURRENT_PACKAGES_DIR}/debug/cmake/vku ${CURRENT_PACKAGES_DIR}/debug/share/vku)
file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/share)
file(RENAME ${CURRENT_PACKAGES_DIR}/cmake/vku ${CURRENT_PACKAGES_DIR}/share/vku)

vcpkg_cmake_config_fixup(PACKAGE_NAME "vku")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/cmake" "${CURRENT_PACKAGES_DIR}/debug/cmake")

file(INSTALL "${SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
