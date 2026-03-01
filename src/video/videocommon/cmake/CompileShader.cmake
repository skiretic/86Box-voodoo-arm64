#
# CompileShader.cmake -- Compile a GLSL shader to SPIR-V and embed as C header.
#
# Defines the function:
#   compile_shader(GLSL_SOURCE OUTPUT_HEADER ARRAY_NAME)
#
# Requires GLSLC to be set to a valid glslc path before including this file.
#

function(compile_shader GLSL_SOURCE OUTPUT_HEADER ARRAY_NAME)
    get_filename_component(SHADER_NAME "${GLSL_SOURCE}" NAME)

    # Intermediate SPIR-V binary
    set(SPV_FILE "${CMAKE_CURRENT_BINARY_DIR}/generated/${SHADER_NAME}.spv")

    # Step 1: GLSL -> SPIR-V
    add_custom_command(
        OUTPUT "${SPV_FILE}"
        COMMAND "${GLSLC}"
            --target-env=vulkan1.2
            -O
            -Werror
            -o "${SPV_FILE}"
            "${GLSL_SOURCE}"
        DEPENDS "${GLSL_SOURCE}"
        COMMENT "Compiling shader ${SHADER_NAME} -> SPIR-V"
        VERBATIM
    )

    # Step 2: SPIR-V -> C header
    add_custom_command(
        OUTPUT "${OUTPUT_HEADER}"
        COMMAND "${CMAKE_COMMAND}"
            -D "INPUT=${SPV_FILE}"
            -D "OUTPUT=${OUTPUT_HEADER}"
            -D "ARRAY=${ARRAY_NAME}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/SpvToHeader.cmake"
        DEPENDS "${SPV_FILE}"
        COMMENT "Embedding SPIR-V ${SHADER_NAME} -> ${ARRAY_NAME}"
        VERBATIM
    )
endfunction()
