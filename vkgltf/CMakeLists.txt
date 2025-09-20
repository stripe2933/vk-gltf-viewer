project(vkgltf LANGUAGES CXX)

option(VKGLTF_USE_MIKKTSPACE "Use MikkTSpace dependency for tangent space generation")
option(VKGLTF_USE_KTX "Use KTX dependency for KHR_texture_basisu support")
option(VKGLTF_USE_DRACO "Use draco dependency for KHR_draco_mesh_compression support")
option(VKGLTF_USE_BINDLESS "Bindless")

# ----- External dependencies -----

find_package(fastgltf CONFIG REQUIRED)
find_package(Stb REQUIRED)
find_package(vku CONFIG REQUIRED)

if (VKGLTF_USE_MIKKTSPACE)
    find_package(mikktspace CONFIG REQUIRED)
endif()

if (VKGLTF_USE_KTX)
    find_package(Ktx CONFIG REQUIRED)
endif()

if (VKGLTF_USE_DRACO)
    find_package(draco CONFIG REQUIRED)
endif()

# ----- Module configurations for the external dependencies -----

add_library(fastgltf_module)
add_library(fastgltf::module ALIAS fastgltf_module)

target_sources(fastgltf_module PUBLIC FILE_SET CXX_MODULES FILES thirdparty/module/fastgltf.ixx)
target_link_libraries(fastgltf_module PRIVATE fastgltf::fastgltf)
target_compile_definitions(fastgltf_module PRIVATE FASTGLTF_USE_STD_MODULE=1)

# ----- vkgltf::util -----

add_library(vkgltf_util)
add_library(vkgltf::util ALIAS vkgltf_util)

target_compile_features(vkgltf_util PUBLIC cxx_std_23)
target_sources(vkgltf_util PUBLIC
    FILE_SET CXX_MODULES
    FILES module/util/mod.cppm
)
target_link_libraries(vkgltf_util PUBLIC
    fastgltf::module
)
set_target_properties(vkgltf_util PROPERTIES CXX_MODULE_STD 1)

# ----- vkgltf -----

add_library(vkgltf)
add_library(vkgltf::vkgltf ALIAS vkgltf)

target_compile_features(vkgltf PUBLIC cxx_std_23)
target_include_directories(vkgltf PRIVATE
    ${Stb_INCLUDE_DIR}
    include
)
target_sources(vkgltf PUBLIC
    FILE_SET CXX_MODULES
    FILES
        module/mod.cppm
        module/CombinedIndexBuffer.cppm
        module/Image.cppm
        module/PrimitiveAttributeBuffers.cppm
        module/sampler.cppm
        module/StagingBufferStorage.cppm
)
target_link_libraries(vkgltf PUBLIC
    fastgltf::module
    vku::vku
)
set_target_properties(vkgltf PROPERTIES CXX_MODULE_STD 1)

if (VKGLTF_USE_MIKKTSPACE)
    target_sources(vkgltf PUBLIC
        FILE_SET CXX_MODULES
        FILES module/mikktspace.cppm
    )
    target_link_libraries(vkgltf PRIVATE
        mikktspace::mikktspace
        vkgltf::util
    )
    target_compile_definitions(vkgltf PRIVATE
        USE_MIKKTSPACE
    )
endif()

if (VKGLTF_USE_KTX)
    target_link_libraries(vkgltf PRIVATE KTX::ktx)
    target_compile_definitions(vkgltf PRIVATE USE_KTX)
endif()

if (VKGLTF_USE_DRACO)
    target_link_libraries(vkgltf PRIVATE draco::draco)
    target_compile_definitions(vkgltf PRIVATE USE_DRACO)
endif()

# ----- vkgltf::bindless -----

if (VKGLTF_USE_BINDLESS)
    add_library(vkgltf_bindless)
    add_library(vkgltf::bindless ALIAS vkgltf_bindless)

    target_compile_features(vkgltf_bindless PUBLIC cxx_std_23)
    target_include_directories(vkgltf_bindless PRIVATE include)
    target_sources(vkgltf_bindless PUBLIC
        FILE_SET CXX_MODULES
        FILES
            module/bindless/mod.cppm
            module/bindless/NodeBuffer.cppm
            module/bindless/PrimitiveBuffer.cppm
            module/bindless/shader_type/Accessor.cppm
            module/bindless/shader_type/Node.cppm
            module/bindless/shader_type/Primitive.cppm
            module/bindless/SkinBuffer.cppm
    )
    target_link_libraries(vkgltf_bindless
        PUBLIC
            vkgltf::vkgltf
            vku::vku # TODO: this need to be transitively included by vkgltf::vkgltf, but CMake cannot configure it.
        PRIVATE vkgltf::util
    )

    set_target_properties(vkgltf_bindless PROPERTIES CXX_MODULE_STD 1)
endif()