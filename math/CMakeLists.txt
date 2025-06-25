add_library(math)
target_sources(math PUBLIC
    FILE_SET CXX_MODULES
    FILES math.cppm
)
target_compile_features(math PUBLIC cxx_std_23)
set_target_properties(math PROPERTIES CXX_MODULE_STD 1)