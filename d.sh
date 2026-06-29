#!/bin/bash
set -e

echo "=== Embedding assets with minify + gzip ==="

mkdir -p assets
mkdir -p build

# wget -O ./assets/tailwind.min.js https://cdn.tailwindcss.com
# wget --compression=gzip -q -O - https://cdn.jsdelivr.net/npm/alpinejs@3.x.x/dist/cdn.min.js > ./assets/alpine.min.js

# Optional: sudo npm install -g html-minifier
# Optional: sudo apt install jq -y

for file in assets/*; do
    if [ -f "$file" ]; then
        filename=$(basename "$file")
        base_name="${filename%.*}"
        ext="${filename##*.}"
        var_name="${base_name//[^a-zA-Z0-9_]/_}_embed"
        
        output_header="build/${base_name}_embed.h"
        
        echo "Processing: $file"

        if [[ "$ext" == "html" || "$ext" == "css" || "$ext" == "js" ]]; then
            echo "  → Minifying + gzipping ($ext)..."
            minified="build/${filename}.min"
            
            if [[ "$ext" == "html" ]]; then
                html-minifier --collapse-whitespace --remove-comments "$file" -o "$minified"
#                html-minifier --collapse-whitespace --remove-comments --minify-css --minify-js "$file" -o "$minified"
            else
                cp "$file" "$minified"
            fi
            
            gzip -9 -c "$minified" > "build/${filename}.gz"
            rm -f "$minified"
            
            input_for_xxd="build/${filename}.gz"
            final_var_name="${var_name}_gz"
            
        elif [[ "$ext" == "json" ]]; then
            echo "  → Minifying JSON only (no gzip for cJSON compatibility)..."
            minified="build/${filename}.min"
            
            if command -v jq >/dev/null 2>&1; then
#                cp "$file" "$minified"
                jq -c . "$file" > "$minified"
            else
                cp "$file" "$minified"
            fi
            
            input_for_xxd="$minified"
            final_var_name="${var_name}"
        else
            echo "  → Embedding raw"
            input_for_xxd="$file"
            final_var_name="${var_name}"
        fi

        echo "  → Embedding as $final_var_name"
        echo -n "static const " > "$output_header"
        xxd -i -n "$final_var_name" "$input_for_xxd" >> "$output_header"
        
        # Metadata
        {
            echo ""
#            echo "static const unsigned int ${final_var_name}_len = $(wc -c < "$input_for_xxd");"
            echo "// Processed size: $(wc -c < "$input_for_xxd") bytes"
        } >> "$output_header"
    fi
done

echo "=== Building and flashing ==="
cd build
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