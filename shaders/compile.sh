#!/bin/bash

# Get the script's filename
script_name=$(basename "$0")

# Iterate over shader files in the script's directory
for file in "$(dirname "$0")"/*.{vert,frag,tesc,tese,geom,comp}; do
  # Check if it is a file and not the script itself
  if [ -f "$file" ] && [ "$(basename "$file")" != "$script_name" ]; then
    # Call glslc to compile the file
    glslc --target-env=vulkan1.2 -O "$file" -o "$file.spv"
  fi
done