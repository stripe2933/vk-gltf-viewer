add_library(geometry)
target_sources(geometry PUBLIC
    FILE_SET CXX_MODULES
    FILES geometry.cppm
)
target_compile_features(geometry PUBLIC cxx_std_23)
target_link_libraries(geometry PUBLIC glm::module)
set_target_properties(geometry PROPERTIES CXX_MODULE_STD 1)