#
# SpvToHeader.cmake -- Convert a SPIR-V binary file to a C header.
#
# Usage (from CMake script mode):
#   cmake -D INPUT=shader.spv -D OUTPUT=shader_spv.h -D ARRAY=shader_spv -P SpvToHeader.cmake
#
# Produces a header like:
#   static const uint32_t shader_spv[] = { 0x07230203, 0x00010500, ... };
#   static const uint32_t shader_spv_size = sizeof(shader_spv);
#

if(NOT INPUT OR NOT OUTPUT OR NOT ARRAY)
    message(FATAL_ERROR "SpvToHeader: INPUT, OUTPUT, and ARRAY must be defined.")
endif()

file(READ "${INPUT}" SPV_HEX HEX)
string(LENGTH "${SPV_HEX}" SPV_HEX_LEN)

# Each byte is two hex chars; SPIR-V is always a multiple of 4 bytes.
math(EXPR WORD_COUNT "${SPV_HEX_LEN} / 8")

set(WORDS "")
set(IDX 0)
set(COL 0)
while(IDX LESS SPV_HEX_LEN)
    # Read 4 bytes (8 hex chars) in file order (little-endian SPIR-V words).
    # file(READ ... HEX) gives bytes in file order, so byte0 byte1 byte2 byte3.
    # SPIR-V is always little-endian on disk, and we want the uint32_t value.
    string(SUBSTRING "${SPV_HEX}" ${IDX} 2 B0)
    math(EXPR OFF1 "${IDX} + 2")
    string(SUBSTRING "${SPV_HEX}" ${OFF1} 2 B1)
    math(EXPR OFF2 "${IDX} + 4")
    string(SUBSTRING "${SPV_HEX}" ${OFF2} 2 B2)
    math(EXPR OFF3 "${IDX} + 6")
    string(SUBSTRING "${SPV_HEX}" ${OFF3} 2 B3)

    # Reconstruct little-endian uint32: byte3 byte2 byte1 byte0
    set(WORD "0x${B3}${B2}${B1}${B0}")

    if(WORDS)
        string(APPEND WORDS ",")
        if(COL GREATER_EQUAL 6)
            string(APPEND WORDS "\n    ")
            set(COL 0)
        else()
            string(APPEND WORDS " ")
        endif()
    endif()
    string(APPEND WORDS "${WORD}")

    math(EXPR IDX "${IDX} + 8")
    math(EXPR COL "${COL} + 1")
endwhile()

set(HEADER "/* Auto-generated from SPIR-V -- do not edit. */\n")
string(APPEND HEADER "#ifndef ${ARRAY}_H\n")
string(APPEND HEADER "#define ${ARRAY}_H\n\n")
string(APPEND HEADER "#include <stdint.h>\n")
string(APPEND HEADER "#include <stddef.h>\n\n")
string(APPEND HEADER "static const uint32_t ${ARRAY}[] = {\n    ${WORDS}\n};\n\n")
string(APPEND HEADER "static const size_t ${ARRAY}_size = sizeof(${ARRAY});\n\n")
string(APPEND HEADER "#endif /* ${ARRAY}_H */\n")

file(WRITE "${OUTPUT}" "${HEADER}")
