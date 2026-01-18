# Function to embed a binary file as a C array
function(embed_resource resource_file output_file var_name)
    file(READ ${resource_file} hex_content HEX)
    string(LENGTH ${hex_content} hex_length)
    math(EXPR byte_length "${hex_length} / 2")

    # Convert hex string to comma-separated bytes
    string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex_content ${hex_content})
    # Remove trailing comma
    string(REGEX REPLACE ",$" "" hex_content ${hex_content})

    # Write the header file
    file(WRITE ${output_file} "// Auto-generated file - do not edit\n")
    file(APPEND ${output_file} "#pragma once\n")
    file(APPEND ${output_file} "#include <cstddef>\n\n")
    file(APPEND ${output_file} "inline const unsigned char ${var_name}_data[] = {\n")
    file(APPEND ${output_file} "${hex_content}\n")
    file(APPEND ${output_file} "};\n\n")
    file(APPEND ${output_file} "inline const size_t ${var_name}_size = ${byte_length};\n")
endfunction()
