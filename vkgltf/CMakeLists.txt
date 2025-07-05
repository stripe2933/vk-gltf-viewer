add_library(vkgltf)
target_include_directories(vkgltf PRIVATE
    include
)
target_sources(vkgltf PUBLIC
    FILE_SET CXX_MODULES
    FILES
        vkgltf.cppm
        CombinedIndexBuffer.cppm
        PrimitiveAttributeBuffers.cppm
)
target_link_libraries(vkgltf PUBLIC
    fastgltf::module
    vku::vku
)
set_target_properties(vkgltf PROPERTIES
    CXX_MODULE_STD 1
)

add_library(vkgltf::vkgltf ALIAS vkgltf)