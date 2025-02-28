if (${Vulkan_glslc_FOUND})
    message(STATUS "Using Vulkan glslc for shader compilation.")

    # glslc needs to create dependency file in ${CMAKE_CURRENT_BINARY_DIR}/shader_depfile.
    file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/shader_depfile)
elseif (${Vulkan_glslangValidator_FOUND})
    message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
else()
    message(FATAL_ERROR "No shader compiler found.")
endif ()

function(target_link_shaders TARGET)
    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    set(spirv_num_filenames "")
    set(shader_module_filenames "")
    foreach (source IN LISTS ARGN)
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
                COMMAND Vulkan::glslc -MD -MF ${depfile} $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -mfmt=num ${source} -o ${spirv_num_filename}
                DEPENDS ${source}
                BYPRODUCTS ${depfile}
                DEPFILE ${depfile}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${spirv_num_filename}
                COMMAND Vulkan::glslangValidator -V $<$<CONFIG:Release>:-Os> --target-env vulkan1.2 -x ${source} -o ${spirv_num_filename}
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
        configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/shader_module.cmake.in ${shader_module_filename} @ONLY)
        list(APPEND shader_module_filenames ${shader_module_filename})
    endforeach ()

    # --------------------
    # Attach sources to the target.
    # --------------------

    target_sources(${TARGET} PRIVATE FILE_SET HEADERS FILES ${spirv_num_filenames})
    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${shader_module_filenames})
endfunction()

function(target_link_shader_variants TARGET SOURCE MACRO_NAMES)
    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    # Get filename from source.
    cmake_path(GET SOURCE FILENAME filename)

    # Make shader identifier.
    string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

    # Make source path absolute.
    cmake_path(ABSOLUTE_PATH SOURCE)

    set(spirv_num_filenames "")
    set(template_specializations "")
    set(extern_template_instantiations "")
    foreach (macro_values IN LISTS ARGN)
        # Split whitespace-delimited string to list.
        separate_arguments(macro_values)

        # Create CLI macro definitions by zipping the macro names and values.
        # e.g. MACRO_NAMES=[MACRO1, MACRO2], macro_values=[0, 1] -> macro_cli_defs="-DMACRO1=0 -DMACRO2=1"
        set(macro_cli_defs "")
        foreach (macro_name macro_value IN ZIP_LISTS MACRO_NAMES macro_values)
            string(APPEND macro_cli_defs "-D${macro_name}=${macro_value} ")
        endforeach ()

        # Split whitespace-delimited string to list (to prevent macro_cli_defs are passed with double quotes).
        separate_arguments(macro_cli_defs)

        # Make filename-like parameter string.
        # e.g., If macro_values=[0, 1], variant_filename="0_1".
        list(JOIN macro_values "_" variant_filename)
        set(spirv_num_filename "${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}_${variant_filename}.h")

        if (${Vulkan_glslc_FOUND})
            set(depfile "${CMAKE_CURRENT_BINARY_DIR}/shader_depfile/${filename}_${variant_filename}.d")
            add_custom_command(
                OUTPUT ${spirv_num_filename}
                # Compile GLSL to SPIR-V.
                COMMAND Vulkan::glslc -MD -MF ${depfile} $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -mfmt=num ${macro_cli_defs} ${SOURCE} -o ${spirv_num_filename}
                DEPENDS ${SOURCE}
                BYPRODUCTS ${depfile}
                DEPFILE ${depfile}
                COMMAND_EXPAND_LISTS
                VERBATIM
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${spirv_num_filename}
                # Compile GLSL to SPIR-V.
                COMMAND Vulkan::glslangValidator -V $<$<CONFIG:Release>:-Os> --target-env vulkan1.2 -x ${macro_cli_defs} ${SOURCE} -o ${spirv_num_filename}
                DEPENDS ${SOURCE}
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

    # "MACRO1;MACRO2;MACRO3" -> "int MACRO1, int MACRO2, int MACRO3"
    list(TRANSFORM MACRO_NAMES PREPEND "int " OUTPUT_VARIABLE comma_separated_macro_params)
    list(JOIN comma_separated_macro_params ", " comma_separated_macro_params)

    # "MACRO1;MACRO2;MACRO3" -> "MACRO1, MACRO2, MACRO3"
    list(JOIN MACRO_NAMES ", " comma_separated_macro_names)

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/variant_shader_module_interface.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm @ONLY)

    # --------------------
    # Make implementation file.
    # --------------------

    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/variant_shader_module_impl.cmake.in ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp @ONLY)

    # --------------------
    # Attach sources to the target.
    # --------------------

    target_sources(${TARGET} PRIVATE FILE_SET HEADERS FILES ${spirv_num_filenames})
    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm)
    target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp)
endfunction()