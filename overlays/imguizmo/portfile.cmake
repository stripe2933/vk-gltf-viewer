vcpkg_check_linkage(ONLY_STATIC_LIBRARY)

vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO CedricGuillemet/ImGuizmo
    REF 2310acda820d7383d4c4884b7945ada92cd16a47
    SHA512 989dc593d3bc36e8efc9480e9d665310a73d811a9ce7111598083f836efe6efe27c66a4717de53a923411605d65355af6cad5a62cf7ddeff2c9803570d1eaa41
    HEAD_REF master
)

file(COPY "${CMAKE_CURRENT_LIST_DIR}/CMakeLists.txt" DESTINATION "${SOURCE_PATH}")

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS_DEBUG
        -DIMGUIZMO_SKIP_HEADERS=ON
)

vcpkg_cmake_install()

vcpkg_copy_pdbs()
vcpkg_cmake_config_fixup(PACKAGE_NAME ${PORT} CONFIG_PATH share/${PORT})

vcpkg_install_copyright(FILE_LIST "${SOURCE_PATH}/LICENSE")
