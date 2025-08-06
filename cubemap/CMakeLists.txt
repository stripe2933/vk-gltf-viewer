add_library(cubemap)
target_include_directories(cubemap PRIVATE include)
target_sources(cubemap PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES
        interface/mod.cppm
        interface/CubemapComputePipeline.cppm
        interface/SubgroupMipmapComputePipeline.cppm
)
target_compile_features(cubemap PUBLIC cxx_std_23)
target_link_libraries(cubemap PUBLIC vku::vku)
set_target_properties(cubemap PROPERTIES CXX_MODULE_STD 1)

target_link_shaders(cubemap PUBLIC
    FILES shader/cubemap.comp
)
target_link_shader_variants(cubemap PUBLIC
    TARGET_ENV vulkan1.2
    FILES shader/subgroup_mipmap.comp
    MACRO_NAMES "AMD_SHADER_IMAGE_LOAD_STORE_LOD"
    MACRO_VALUES 0 1
)