if (${Vulkan_glslc_FOUND})
    message(STATUS "Using Vulkan glslc for shader compilation.")

    # glslc needs to create dependency file in ${CMAKE_CURRENT_BINARY_DIR}/shader_depfile.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shader_depfile)
elseif (${Vulkan_glslangValidator_FOUND})
    message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
else()
    message(FATAL_ERROR "No shader compiler found.")
endif ()

# --------------------
# Build bin2mod (application converts a binary file to module interface unit)
# --------------------

add_executable(bin2mod ${CMAKE_CURRENT_LIST_DIR}/bin2mod.cpp)
target_compile_features(bin2mod PRIVATE cxx_std_23)
set_target_properties(bin2mod PROPERTIES CXX_MODULE_STD 1)

# --------------------
# target_link_shaders
# --------------------

function(target_link_shaders TARGET SCOPE)
    set(oneValueArgs TARGET_ENV)
    set(multiValueArgs FILES)
    cmake_parse_arguments(arg "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT arg_TARGET_ENV)
        set(arg_TARGET_ENV "vulkan1.0")
    endif()

    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    set(spirv_filenames "")
    set(shader_module_filenames "")
    foreach (source IN LISTS arg_FILES)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        # Make output filenames.
        set(spirv_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.spv")
        set(shader_module_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm")

        if (${Vulkan_glslc_FOUND})
            set(depfile "${CMAKE_CURRENT_BINARY_DIR}/shader_depfile/${filename}.d")
            add_custom_command(
                OUTPUT ${shader_module_filename}
                COMMAND Vulkan::glslc -MD -MF ${depfile} $<$<CONFIG:Release>:-O> --target-env=${arg_TARGET_ENV} ${source} -o ${spirv_filename}
                COMMAND bin2mod --namespace ${target_identifier}::shader ${spirv_filename} -o ${shader_module_filename}
                DEPENDS ${source}
                BYPRODUCTS ${depfile} ${spirv_filename}
                DEPFILE ${depfile}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${shader_module_filename}
                COMMAND Vulkan::glslangValidator -V $<$<CONFIG:Release>:-Os> --target-env ${arg_TARGET_ENV} ${source} -o ${spirv_filename}
                COMMAND bin2mod --namespace ${target_identifier}::shader ${spirv_filename} -o ${shader_module_filename}
                DEPENDS ${source}
                BYPRODUCTS ${spirv_filename}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        endif ()

        list(APPEND shader_module_filenames ${shader_module_filename})
    endforeach ()

    target_sources(${TARGET} ${SCOPE}
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shader
        FILES ${shader_module_filenames}
    )
endfunction()

# --------------------
# target_link_shader_variants
# --------------------

function(target_link_shader_variants TARGET SCOPE)
    set(oneValueArgs TARGET_ENV)
    set(multiValueArgs FILES MACRO_NAMES MACRO_VALUES)
    cmake_parse_arguments(arg "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT arg_TARGET_ENV)
        set(arg_TARGET_ENV "vulkan1.0")
    endif()

    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    set(shader_module_filenames "")
    foreach (source IN LISTS arg_FILES)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        # Make output filenames.
        set(spirv_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.spv")
        set(shader_module_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm")

        set(spirv_variant_filenames "")
        foreach (macro_values IN LISTS arg_MACRO_VALUES)
            # Split whitespace-delimited string to list.
            separate_arguments(macro_values)

            # Create CLI macro definitions by zipping the macro names and values.
            # e.g. MACRO_NAMES=[MACRO1, MACRO2], macro_values=[0, 1] -> macro_cli_defs="-DMACRO1=0 -DMACRO2=1"
            set(macro_cli_defs "")
            foreach (macro_name macro_value IN ZIP_LISTS arg_MACRO_NAMES macro_values)
                list(APPEND macro_cli_defs "-D${macro_name}=${macro_value}")
            endforeach ()

            # Make filename-like parameter string.
            # e.g., If macro_values=[0, 1], variant_identifier="0_1".
            list(JOIN macro_values "_" variant_identifier)
            set(spirv_variant_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}_${variant_identifier}.spv")

            # TODO: use glslc alternative path when it can change the shader entrypoint name.
            add_custom_command(
                OUTPUT ${spirv_variant_filename}
                COMMAND Vulkan::glslangValidator -V $<$<CONFIG:Release>:-Os> --target-env ${arg_TARGET_ENV} -e main_${variant_identifier} --source-entrypoint main ${macro_cli_defs} ${source} -o ${spirv_variant_filename}
                DEPENDS ${source}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )

            list(APPEND spirv_variant_filenames ${spirv_variant_filename})
        endforeach ()

        add_custom_command(
            OUTPUT ${shader_module_filename}
            COMMAND spirv-link ${spirv_variant_filenames} -o ${spirv_filename}
            COMMAND spirv-opt -Os ${spirv_filename} -o ${spirv_filename}
            COMMAND bin2mod --namespace ${target_identifier}::shader ${spirv_filename} -o ${shader_module_filename}
            DEPENDS ${spirv_variant_filenames}
            BYPRODUCTS ${spirv_filename}
            COMMAND_EXPAND_LISTS
            VERBATIM
        )

        list(APPEND shader_module_filenames ${shader_module_filename})
    endforeach()

    target_sources(${TARGET} ${SCOPE}
        FILE_SET CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shader
        FILES ${shader_module_filenames}
    )
endfunction()