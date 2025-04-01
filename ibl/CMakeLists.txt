add_library(ibl)
target_include_directories(ibl PRIVATE include)
target_sources(ibl PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS interface
    FILES
        interface/mod.cppm
        interface/BrdfmapRenderer.cppm
        interface/PrefilteredmapComputer.cppm
        interface/SphericalHarmonicCoefficientComputer.cppm
)
target_compile_features(ibl PUBLIC cxx_std_23)
target_link_libraries(ibl PUBLIC vku::vku)
set_target_properties(ibl PROPERTIES CXX_MODULE_STD 1)

target_link_shaders(ibl PUBLIC
    TARGET_ENV vulkan1.1
    FILES
        shader/spherical_harmonic_coefficient_image_to_buffer.comp
        shader/spherical_harmonic_coefficient_buffer_to_buffer.comp
        shader/screen_quad.vert shader/brdfmap.frag
)
target_link_shader_variants(ibl PUBLIC
    TARGET_ENV vulkan1.2
    FILES shader/prefilteredmap.comp
    MACRO_NAMES "AMD_SHADER_IMAGE_LOAD_STORE_LOD"
    MACRO_VALUES 0 1
)