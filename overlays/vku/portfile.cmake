vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO stripe2933/vku
    REF 1df76328d1f28598e1493989d38180a0b308f283
    HEAD_REF main
    SHA512 0a6e68e254022a3470d2fa5a77b356fe1f985bec165317926c939ae925b6eec9f2b3dd6517adc3c44c4131c06ac2a97fa359bd20a9f8f3023e742863650472ed
)

vcpkg_cmake_configure(SOURCE_PATH "${SOURCE_PATH}")
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

file(INSTALL "${SOURCE_PATH}/LICENSE.txt" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}" RENAME copyright)
configure_file("${CMAKE_CURRENT_LIST_DIR}/usage" "${CURRENT_PACKAGES_DIR}/share/${PORT}/usage" COPYONLY)
