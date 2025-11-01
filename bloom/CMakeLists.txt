add_library(bloom)
target_include_directories(bloom PRIVATE include)
target_sources(bloom PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES interface/mod.cppm
)
target_compile_features(bloom PUBLIC cxx_std_23)
target_link_libraries(bloom PUBLIC vku::module)
set_target_properties(bloom PROPERTIES CXX_MODULE_STD 1)

target_link_shader_variants(bloom PUBLIC
    TARGET_ENV vulkan1.3
    FILES
        shader/downsample.comp
        shader/upsample.comp
    MACRO_NAMES "AMD_SHADER_IMAGE_LOAD_STORE_LOD"
    MACRO_VALUES 0 1
)