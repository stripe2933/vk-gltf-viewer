function(target_link_shaders TARGET)
    if (${Vulkan_glslc_FOUND})
        message(STATUS "Using Vulkan glslc for shader compilation.")
    elseif (${Vulkan_glslangValidator_FOUND})
        message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
    else()
        message(FATAL_ERROR "No shader compiler found.")
    endif ()

    foreach (source IN LISTS ARGN)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE source)

        set(output "shader/${filename}.spv")

        if (${Vulkan_glslc_FOUND})
            set(depfile "shader_depfile/${filename}.d")
            add_custom_command(
                OUTPUT "${output}"
                COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF "${depfile}" $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 "${source}" -o "${output}"
                DEPENDS "${source}"
                BYPRODUCTS "${depfile}"
                COMMENT "Compiling SPIRV: ${source} -> ${output}"
                DEPFILE "${depfile}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        elseif (${Vulkan_glslangValidator_FOUND})
            add_custom_command(
                OUTPUT "${output}"
                COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V $<$<CONFIG:Debug>:-Od> --target-env vulkan1.2 "${source}" -o "${output}"
                DEPENDS "${source}"
                COMMENT "Compiling SPIRV: ${source} -> ${output}"
                VERBATIM
                COMMAND_EXPAND_LISTS
            )
        endif ()

        # Make target depends on output shader file.
        string(CONFIGURE "${TARGET}_${filename}" shader_target)
        add_custom_target("${shader_target}" DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${output}")
        add_dependencies("${TARGET}" "${shader_target}")
    endforeach ()

    target_compile_definitions(${TARGET} PRIVATE COMPILED_SHADER_DIR="shader")
endfunction()

function(target_link_shaders_variant TARGET MACRO_NAME MACRO_VALUES)
    if (${Vulkan_glslc_FOUND})
        message(STATUS "Using Vulkan glslc for shader compilation.")
    elseif (${Vulkan_glslangValidator_FOUND})
        message(WARNING "Vulkan glslc not found, using glslangValidator for shader compilation instead. Modifying indirectly included files will NOT trigger recompilation.")
    else()
        message(FATAL_ERROR "No shader compiler found.")
    endif ()

    foreach (source IN LISTS ARGN)
        # Get filename from source.
        cmake_path(GET source FILENAME filename)

        # Make source path absolute.
        cmake_path(ABSOLUTE_PATH source OUTPUT_VARIABLE source)

        foreach (macro_value IN LISTS MACRO_VALUES)
            set(output "shader/${filename}_${MACRO_NAME}_${macro_value}.spv")

            if (${Vulkan_glslc_FOUND})
                set(depfile "shader_depfile/${filename}_${MACRO_NAME}_${macro_value}.d")
                add_custom_command(
                    OUTPUT "${output}"
                    COMMAND ${Vulkan_GLSLC_EXECUTABLE} -MD -MF "${depfile}" $<$<CONFIG:Release>:-O> --target-env=vulkan1.2 -D${MACRO_NAME}=${macro_value} "${source}" -o "${output}"
                    DEPENDS "${source}"
                    BYPRODUCTS "${depfile}"
                    COMMENT "Compiling SPIRV: ${source} -> ${output}"
                    DEPFILE "${depfile}"
                    VERBATIM
                    COMMAND_EXPAND_LISTS
                )
            elseif (${Vulkan_glslangValidator_FOUND})
                add_custom_command(
                    OUTPUT "${output}"
                    COMMAND ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE} -V $<$<CONFIG:Debug>:-Od> --target-env vulkan1.2 "${source}" -D${MACRO_NAME}=${macro_value} -o "${output}"
                    DEPENDS "${source}"
                    COMMENT "Compiling SPIRV: ${source} -> ${output}"
                    VERBATIM
                    COMMAND_EXPAND_LISTS
                )
            endif ()

            # Make target depends on output shader file.
            string(CONFIGURE "${TARGET}_${filename}_${MACRO_NAME}_${macro_value}" shader_target)
            add_custom_target("${shader_target}" DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/${output}")
            add_dependencies("${TARGET}" "${shader_target}")
        endforeach ()
    endforeach ()

    target_compile_definitions(${TARGET} PRIVATE COMPILED_SHADER_DIR="shader")
endfunction()