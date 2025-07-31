add_library(metal-cpp src/impl.cpp)
target_include_directories(metal-cpp PUBLIC extlibs/include include)
target_sources(metal-cpp PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES interface/mod.cppm
)
target_link_libraries(metal-cpp PUBLIC
    "-framework Foundation"
    "-framework QuartzCore"
    "-framework Metal"
)

add_library(metal-cpp::metal-cpp ALIAS metal-cpp)
