cmake_minimum_required(VERSION 3.28)
project(pbrenvmap
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)

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
        interface/pipelines/CubemapComputer.cppm
        interface/pipelines/MultiplyComputer.cppm
        interface/pipelines/PrefilteredmapComputer.cppm
        interface/pipelines/SphericalHarmonicCoefficientsSumComputer.cppm
        interface/pipelines/SphericalHarmonicsComputer.cppm
        interface/pipelines/SubgroupMipmapComputer.cppm
)
target_link_libraries(pbrenvmap PUBLIC
    vku
)