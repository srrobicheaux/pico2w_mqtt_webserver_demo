#!/bin/bash
set -e  # Exit immediately if any command fails

echo "=== Embedding assets ==="

# Create directories
mkdir -p assets
mkdir -p build

# Loop through all files in assets/ and embed them into build/
for file in assets/*; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        base_name="${filename//./_}"                    # Remove extension
        var_name="${base_name//[^a-zA-Z0-9_]/_}"      # Sanitize for C variable
        
        output_header="build/${base_name}_embed.h"
        
        echo "Embedding: $file → $output_header"
        echo -n "static const " > "$output_header"
        xxd -i -n "${var_name}" "$file" >> "$output_header"
    fi
done

echo "=== Building project ==="

cd build

# Reconfigure cmake if needed (in case new headers appear)
cmake ..

make -j4

echo "=== Starting Serial Monitor ==="
PORT="/dev/ttyACM0"

echo "Waiting for $PORT"
while [ -f $PORT ]; do
    echo -n "."
    sleep 1
done

echo "=== Flashing Pico ==="

# Prefer .uf2, fallback to .elf
UF2_FILE=$(ls -t *.uf2 2>/dev/null | head -n1)
ELF_FILE=$(ls -t *.elf 2>/dev/null | head -n1)

if [ -n "$UF2_FILE" ]; then
    echo "Flashing: $UF2_FILE"
    sudo ~/.pico-sdk/picotool/2.2.0-a4/picotool/picotool load "$UF2_FILE" -fx
elif [ -n "$ELF_FILE" ]; then
    echo "Flashing: $ELF_FILE"
    sudo ~/.pico-sdk/picotool/2.2.0-a4/picotool/picotool load "$ELF_FILE" -fx
else
    echo "Error: No .uf2 or .elf file found in build/"
    exit 1
fi

echo "Connecting to $PORT... Press Ctrl+C to stop."
sleep 1
cat "$PORT"