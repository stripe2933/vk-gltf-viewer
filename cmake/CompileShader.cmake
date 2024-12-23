if (${Vulkan_glslc_FOUND})
    message(STATUS "Using Vulkan glslc for shader compilation.")
elseif (${Vulkan_glslangValidator_FOUND})
    message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
else()
    message(FATAL_ERROR "No shader compiler found.")
endif ()

function(target_link_shaders TARGET)
    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    set(outputs "")
    foreach (source IN LISTS ARGN)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE absolute_source)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        if (${Vulkan_glslc_FOUND})
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
                COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF shader_depfile/${filename}.d $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -mfmt=num ${absolute_source} -o "shader/${filename}.h"
                COMMAND ${CMAKE_COMMAND} -E echo "export module ${target_identifier}:shader.${shader_identifier}\;" > shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E echo "namespace ${target_identifier}::shader { export constexpr unsigned int ${shader_identifier}[] = {" >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E cat shader/${filename}.h >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E rm shader/${filename}.h
                    && ${CMAKE_COMMAND} -E echo "}\;}" >> shader/${filename}.cppm
                DEPENDS ${absolute_source}
                BYPRODUCTS shader_depfile/${filename}.d
                COMMENT "Compiling SPIR-V: ${source} -> ${filename}.cppm"
                DEPFILE shader_depfile/${filename}.d
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
                COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V $<$<CONFIG:Debug>:-Od> --target-env vulkan1.2 -x ${absolute_source} -o "shader/${filename}.h"
                COMMAND ${CMAKE_COMMAND} -E echo "export module ${target_identifier}:shader.${shader_identifier}\;" > shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E echo "namespace ${target_identifier}::shader { export constexpr unsigned int ${shader_identifier}[] = {" >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E cat shader/${filename}.h >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E rm shader/${filename}.h
                    && ${CMAKE_COMMAND} -E echo "}\;}" >> shader/${filename}.cppm
                DEPENDS ${absolute_source}
                COMMENT "Compiling SPIR-V: ${source}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        endif ()

        list(APPEND outputs ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm)
    endforeach ()

    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${outputs})
endfunction()

function(target_link_shader_variants TARGET SOURCE MACRO_NAMES)
    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    # Get filename from source.
    cmake_path(GET SOURCE FILENAME filename)

    # Make shader identifier.
    string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

    # Make source path absolute.
    cmake_path(ABSOLUTE_PATH SOURCE OUTPUT_VARIABLE absolute_source)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp
        # Interface file generation.
        COMMAND ${CMAKE_COMMAND} -E echo "export module ${target_identifier}:shader.${shader_identifier}\;" > shader/${filename}.cppm
        COMMAND ${CMAKE_COMMAND} -E echo "namespace ${target_identifier}::shader { template <int...> struct ${shader_identifier}_t\;" >> shader/${filename}.cppm
        # Implementation file generation.
        COMMAND ${CMAKE_COMMAND} -E echo "module ${target_identifier}\;" > shader/${filename}.cpp
        COMMAND ${CMAKE_COMMAND} -E echo "import :shader.${shader_identifier}\;" >> shader/${filename}.cpp
        COMMENT "Compiling SPIR-V: ${SOURCE}"
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

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

        # Make value parameter string.
        # e.g., If macro_values=[0, 1], value_params="0, 1".
        list(JOIN macro_values ", " value_params)

        if (${Vulkan_glslc_FOUND})
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp
                # Compile GLSL to SPIR-V.
                COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF shader_depfile/${filename}_${variant_filename}.d $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -mfmt=num ${macro_cli_defs} "${absolute_source}" -o shader/${filename}_${variant_filename}_body.h
                # Interface file generation.
                COMMAND ${CMAKE_COMMAND} -E echo "template <> struct ${shader_identifier}_t<${value_params}> { static constexpr unsigned int value[] = {" >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E cat shader/${filename}_${variant_filename}_body.h >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E rm shader/${filename}_${variant_filename}_body.h
                    && ${CMAKE_COMMAND} -E echo "}\; }\;" >> shader/${filename}.cppm
                # Implementation file generation.
                COMMAND ${CMAKE_COMMAND} -E echo "extern template struct ${target_identifier}::shader::${shader_identifier}_t<${value_params}>\;" >> shader/${filename}.cpp
                DEPENDS "${absolute_source}"
                BYPRODUCTS shader_depfile/${filename}_${variant_filename}.d
                DEPFILE shader_depfile/${filename}_${variant_filename}.d
                VERBATIM
                COMMAND_EXPAND_LISTS
                APPEND
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp
                # Compile GLSL to SPIR-V.
                COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V $<$<CONFIG:Debug>:-Od> --target-env vulkan1.2 -x ${macro_cli_defs} ${absolute_source} -o shader/${filename}_${variant_filename}_body.h
                # Interface file generation.
                COMMAND ${CMAKE_COMMAND} -E echo "template <> struct ${shader_identifier}_t<${value_params}> { static constexpr unsigned int value[] = {" >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E cat shader/${filename}_${variant_filename}_body.h >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E rm shader/${filename}_${variant_filename}_body.h
                    && ${CMAKE_COMMAND} -E echo "}\; }\;" >> shader/${filename}.cppm
                # Implementation file generation.
                COMMAND ${CMAKE_COMMAND} -E echo "extern template struct ${target_identifier}::shader::${shader_identifier}_t<${value_params}>\;" >> shader/${filename}.cpp
                DEPENDS "${absolute_source}"
                VERBATIM
                COMMAND_EXPAND_LISTS
                APPEND
            )
        endif ()
    endforeach ()

    # Make template named type parameters string.
    # e.g., If there are 3 macros, template_named_type_params="int MACRO1, int MACRO2, int MACRO3".
    set(template_named_type_params "")
    foreach (macro_name IN LISTS MACRO_NAMES)
        list(APPEND template_named_type_params "int ${macro_name}")
    endforeach ()
    list(JOIN template_named_type_params ", " template_named_type_params)

    # Make named parameter string.
    # e.g., If there are 3 macros, name_params="MACRO1, MACRO2, MACRO3".
    list(JOIN MACRO_NAMES ", " name_params)

    add_custom_command(
        OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
        COMMAND ${CMAKE_COMMAND} -E echo "template <${template_named_type_params}> constexpr auto &${shader_identifier} = ${shader_identifier}_t<${name_params}>::value\; }" >> shader/${filename}.cppm
        APPEND
        VERBATIM
        COMMAND_EXPAND_LISTS
    )

    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm)
    target_sources(${TARGET} PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cpp)
endfunction()