if (${Vulkan_glslc_FOUND})
    message(STATUS "Using Vulkan glslc for shader compilation.")

    # glslc needs to create dependency file in ${CMAKE_CURRENT_BINARY_DIR}/shader_depfile.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shader_depfile)
elseif (${Vulkan_glslangValidator_FOUND})
    message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
else()
    message(FATAL_ERROR "No shader compiler found.")
endif ()

function(target_link_shaders TARGET SCOPE)
    set(oneValueArgs TARGET_ENV)
    set(multiValueArgs FILES)
    cmake_parse_arguments(arg "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT arg_TARGET_ENV)
        set(arg_TARGET_ENV "vulkan1.0")
    endif()

    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    set(spirv_num_filenames "")
    set(shader_module_filenames "")
    foreach (source IN LISTS arg_FILES)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        # Make output SPIR-V num path.
        set(spirv_num_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.h")

        if (${Vulkan_glslc_FOUND})
            set(depfile "${CMAKE_CURRENT_BINARY_DIR}/shader_depfile/${filename}.d")
            add_custom_command(
                OUTPUT ${spirv_num_filename}
                COMMAND Vulkan::glslc -MD -MF ${depfile} $<$<CONFIG:Release>:-O> --target-env=${arg_TARGET_ENV} -mfmt=num ${source} -o ${spirv_num_filename}
                DEPENDS ${source}
                BYPRODUCTS ${depfile}
                DEPFILE ${depfile}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${spirv_num_filename}
                COMMAND Vulkan::glslangValidator -V $<$<CONFIG:Release>:-Os> --target-env ${arg_TARGET_ENV} -x ${source} -o ${spirv_num_filename}
                DEPENDS ${source}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        endif ()
        list(APPEND spirv_num_filenames ${spirv_num_filename})

        # --------------------
        # Make interface file.
        # --------------------

        set(shader_module_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm")
        configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shader_module.cmake.in ${shader_module_filename} @ONLY)
        list(APPEND shader_module_filenames ${shader_module_filename})
    endforeach ()

    # --------------------
    # Attach sources to the target.
    # --------------------

    target_sources(${TARGET} ${SCOPE} FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shader FILES ${spirv_num_filenames})
    target_sources(${TARGET} ${SCOPE} FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shader FILES ${shader_module_filenames})
endfunction()

function(target_link_shader_variants TARGET SCOPE)
    set(oneValueArgs TARGET_ENV)
    set(multiValueArgs FILES MACRO_NAMES MACRO_VALUES)
    cmake_parse_arguments(arg "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT arg_TARGET_ENV)
        set(arg_TARGET_ENV "vulkan1.0")
    endif()

    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)


    # "MACRO1;MACRO2;MACRO3" -> "int MACRO1, int MACRO2, int MACRO3"
    list(TRANSFORM arg_MACRO_NAMES PREPEND "int " OUTPUT_VARIABLE comma_separated_macro_params)
    list(JOIN comma_separated_macro_params ", " comma_separated_macro_params)

    # "MACRO1;MACRO2;MACRO3" -> "MACRO1, MACRO2, MACRO3"
    list(JOIN arg_MACRO_NAMES ", " comma_separated_macro_names)

    set(spirv_num_filenames "")
    set(shader_module_interface_filenames "")
    set(shader_module_impl_filenames "")
    foreach (source IN LISTS arg_FILES)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source)

        set(template_specializations "")
        set(extern_template_instantiations "")
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
            # e.g., If macro_values=[0, 1], variant_filename="0_1".
            list(JOIN macro_values "_" variant_filename)
            set(spirv_num_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}_${variant_filename}.h")

            if (${Vulkan_glslc_FOUND})
                set(depfile "${CMAKE_CURRENT_BINARY_DIR}/shader_depfile/${filename}_${variant_filename}.d")
                add_custom_command(
                    OUTPUT ${spirv_num_filename}
                    # Compile GLSL to SPIR-V.
                    COMMAND Vulkan::glslc -MD -MF ${depfile} $<$<CONFIG:Release>:-O> --target-env=${arg_TARGET_ENV} -mfmt=num ${macro_cli_defs} ${source} -o ${spirv_num_filename}
                    DEPENDS ${source}
                    BYPRODUCTS ${depfile}
                    DEPFILE ${depfile}
                    COMMAND_EXPAND_LISTS
                    VERBATIM
                )
            elseif (${Vulkan_glslangValidator_FOUND})
                add_custom_command(
                    OUTPUT ${spirv_num_filename}
                    # Compile GLSL to SPIR-V.
                    COMMAND Vulkan::glslangValidator -V $<$<CONFIG:Release>:-Os> --target-env ${arg_TARGET_ENV} -x ${macro_cli_defs} ${source} -o ${spirv_num_filename}
                    DEPENDS ${source}
                    COMMAND_EXPAND_LISTS
                    VERBATIM
                )
            endif ()

            # Make value parameter string.
            # e.g., If macro_values=[0, 1], value_params="0, 1".
            list(JOIN macro_values ", " value_params)

            list(APPEND spirv_num_filenames ${spirv_num_filename})
            list(APPEND template_specializations "SPECIALIZATION_BEGIN(${value_params})\n#include \"${spirv_num_filename}\"\nSPECIALIZATION_END()")
            list(APPEND extern_template_instantiations "INSTANTIATION(${value_params})")
        endforeach ()

        # --------------------
        # Make interface file.
        # --------------------

        set(shader_module_interface_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm")
        configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/variant_shader_module_interface.cmake.in ${shader_module_interface_filename} @ONLY)
        list(APPEND shader_module_interface_filenames ${shader_module_interface_filename})

        # --------------------
        # Make implementation file.
        # --------------------

        set(shader_module_impl_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp")
        configure_file(${CMAKE_CURRENT_FUNCTION_LIST_DIR}/variant_shader_module_impl.cmake.in ${shader_module_impl_filename} @ONLY)
        list(APPEND shader_module_impl_filenames ${shader_module_impl_filename})
    endforeach()

    # --------------------
    # Attach sources to the target.
    # --------------------

    target_sources(${TARGET} ${SCOPE} FILE_SET HEADERS BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shader FILES ${spirv_num_filenames})
    target_sources(${TARGET} ${SCOPE} FILE_SET CXX_MODULES BASE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/shader FILES ${shader_module_interface_filenames})
    target_sources(${TARGET} ${SCOPE} ${shader_module_impl_filenames})
endfunction()