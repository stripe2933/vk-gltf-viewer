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
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE source)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        if (${Vulkan_glslc_FOUND})
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
                COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF shader_depfile/${filename}.d $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -mfmt=num ${source} -o "shader/${filename}.h"
                    && ${CMAKE_COMMAND} -E echo "export module ${target_identifier}:shader.${shader_identifier}\;" > shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E echo "namespace ${target_identifier}::shader { export constexpr unsigned int ${shader_identifier}[] = {" >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E cat shader/${filename}.h >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E rm shader/${filename}.h
                    && ${CMAKE_COMMAND} -E echo "}\;}" >> shader/${filename}.cppm
                DEPENDS ${source}
                BYPRODUCTS shader_depfile/${filename}.d
                COMMENT "Compiling SPIR-V: ${source} -> ${filename}.cppm"
                DEPFILE shader_depfile/${filename}.d
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
                COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V $<$<CONFIG:Debug>:-Od> --target-env vulkan1.2 -x ${source} -o "shader/${filename}.h"
                    && ${CMAKE_COMMAND} -E echo "export module ${target_identifier}:shader.${shader_identifier}\;" > shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E echo "namespace ${target_identifier}::shader { export constexpr unsigned int ${shader_identifier}[] = {" >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E cat shader/${filename}.h >> shader/${filename}.cppm
                    && ${CMAKE_COMMAND} -E rm shader/${filename}.h
                    && ${CMAKE_COMMAND} -E echo "}\;}" >> shader/${filename}.cppm
                DEPENDS ${source}
                COMMENT "Compiling SPIR-V: ${source}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        endif ()

        list(APPEND outputs ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm)
    endforeach ()

    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${outputs})
endfunction()

function(target_link_shaders_variant TARGET MACRO_NAME MACRO_VALUES)
    # Make target identifier.
    string(MAKE_C_IDENTIFIER ${TARGET} target_identifier)

    set(outputs "")
    foreach (source IN LISTS ARGN)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make shader identifier.
        string(MAKE_C_IDENTIFIER ${filename} shader_identifier)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE source)

        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
            COMMAND ${CMAKE_COMMAND} -E echo "export module ${target_identifier}:shader.${shader_identifier}\;" > shader/${filename}.cppm
            COMMAND ${CMAKE_COMMAND} -E echo "namespace ${target_identifier}::shader {" >> shader/${filename}.cppm
            COMMENT "Compiling SPIR-V: ${source} (${MACRO_NAME}=${MACRO_VALUES})"
            VERBATIM
            COMMAND_EXPAND_LISTS
        )

        foreach (macro_value IN LISTS MACRO_VALUES)
            # Make variant identifier.
            string(MAKE_C_IDENTIFIER "${MACRO_NAME}_${macro_value}" variant_identifier)

            if (${Vulkan_glslc_FOUND})
                add_custom_command(
                    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF shader_depfile/${filename}_${variant_identifier}.d $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -mfmt=num -D${MACRO_NAME}=${macro_value} "${source}" -o shader/${filename}_${variant_identifier}_body.h
                        && ${CMAKE_COMMAND} -E echo "export constexpr unsigned int ${shader_identifier}_${variant_identifier}[] = {" >> shader/${filename}.cppm
                        && ${CMAKE_COMMAND} -E cat shader/${filename}_${variant_identifier}_body.h >> shader/${filename}.cppm
                        && ${CMAKE_COMMAND} -E rm shader/${filename}_${variant_identifier}_body.h
                        && ${CMAKE_COMMAND} -E echo "}\;" >> shader/${filename}.cppm
                    DEPENDS "${source}"
                    BYPRODUCTS shader_depfile/${filename}_${variant_identifier}.d
                    DEPFILE shader_depfile/${filename}_${variant_identifier}.d
                    VERBATIM
                    COMMAND_EXPAND_LISTS
                    APPEND
                )
            elseif (${Vulkan_glslangValidator_FOUND})
                add_custom_command(
                    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
                    COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V $<$<CONFIG:Debug>:-Od> --target-env vulkan1.2 -x ${source} -D${MACRO_NAME}=${macro_value} -o shader/${filename}_${variant_identifier}_body.h
                        && ${CMAKE_COMMAND} -E echo "export constexpr unsigned int ${shader_identifier}_${variant_identifier}[] = {" >> shader/${filename}.cppm
                        && ${CMAKE_COMMAND} -E cat shader/${filename}_${variant_identifier}_body.h >> shader/${filename}.cppm
                        && ${CMAKE_COMMAND} -E rm shader/${filename}_${variant_identifier}_body.h
                        && ${CMAKE_COMMAND} -E echo "}\;" >> shader/${filename}.cppm
                    DEPENDS "${source}"
                    VERBATIM
                    COMMAND_EXPAND_LISTS
                    APPEND
                )
            endif ()

        endforeach ()

        add_custom_command(
            OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm
            COMMAND ${CMAKE_COMMAND} -E echo "}" >> shader/${filename}.cppm
            APPEND
            VERBATIM
            COMMAND_EXPAND_LISTS
        )

        list(APPEND outputs ${CMAKE_CURRENT_BINARY_DIR}/shader/${filename}.cppm)
    endforeach ()

    target_sources(${TARGET} PRIVATE FILE_SET CXX_MODULES FILES ${outputs})
endfunction()