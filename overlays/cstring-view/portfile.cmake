vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stripe2933/cstring_view
    REF v1.0.1
    HEAD_REF module
    SHA512 288f8c11b568158244e06b72d45e7622f36249f06ec6f760fa819365834c9783fbdca4f3936e282cd99ab066dd7c58bce8f44311905fed73049072afd8c66c38
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
vcpkg_cmake_install()

if (NOT VCPKG_BUILD_TYPE STREQUAL "release")
    file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/debug/share)
    file(RENAME ${CURRENT_PACKAGES_DIR}/debug/cmake/cstring_view ${CURRENT_PACKAGES_DIR}/debug/share/cstring_view)
endif()
if (NOT VCPKG_BUILD_TYPE STREQUAL "debug")
    file(MAKE_DIRECTORY ${CURRENT_PACKAGES_DIR}/share)
    file(RENAME ${CURRENT_PACKAGES_DIR}/cmake/cstring_view ${CURRENT_PACKAGES_DIR}/share/cstring_view)
endif()

vcpkg_cmake_config_fixup(PACKAGE_NAME "cstring_view")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/cmake" "${CURRENT_PACKAGES_DIR}/debug/cmake")

file(INSTALL "${SOURCE_PATH}/LICENSE.md" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)