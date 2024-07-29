cmake_minimum_required(VERSION 3.30)

# Enable CMake's experimental standard library module support.
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "0e5b6991-d74f-4b3d-a41c-cf096e0b2508")

project(pbrenvmap LANGUAGES CXX)

set(CMAKE_CXX_MODULE_STD 1)

# --------------------
# Project targets.
# --------------------

add_library(pbrenvmap)
target_sources(pbrenvmap PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES
        interface/mod.cppm
        interface/details/ranges.cppm
        interface/pipeline/CubemapComputer.cppm
        interface/pipeline/MultiplyComputer.cppm
        interface/pipeline/PrefilteredmapComputer.cppm
        interface/pipeline/SphericalHarmonicCoefficientsSumComputer.cppm
        interface/pipeline/SphericalHarmonicsComputer.cppm
        interface/pipeline/SubgroupMipmapComputer.cppm
)
target_link_libraries(pbrenvmap PUBLIC
    vku::vku
)