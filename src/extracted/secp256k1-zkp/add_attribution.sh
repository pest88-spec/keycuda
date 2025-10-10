#!/bin/bash

# Script to add attribution headers to secp256k1-zkp extracted files

SECP256K1_DIR="/root/keycuda/src/extracted/secp256k1-zkp"
ATTRIBUTION_FILE="$SECP256K1_DIR/attribution_header.txt"

# Function to add attribution header to a file
add_attribution() {
    local file="$1"
    local relative_path="${file#$SECP256K1_DIR/}"
    local origin_path="src/$relative_path"

    # Create attribution header with correct origin path
    local attribution=$(cat "$ATTRIBUTION_FILE" | sed "s|@origin_path  src/secp256k1.c|@origin_path  $origin_path|")

    # Check if file already has attribution header
    if grep -q "Extracted from secp256k1-zkp" "$file"; then
        echo "Skipping $file (already has attribution)"
        return
    fi

    # Read original content
    local original_content=$(cat "$file")

    # Write attribution header + original content
    {
        echo "$attribution"
        echo ""
        echo "$original_content"
    } > "$file.tmp"

    # Replace original file
    mv "$file.tmp" "$file"
    echo "Added attribution to $file"
}

# Add attribution to all .c and .h files
find "$SECP256K1_DIR" -name "*.c" -o -name "*.h" | while read -r file; do
    # Skip the attribution template file itself
    if [[ "$file" != *"attribution_header.txt" ]] && [[ "$file" != *"add_attribution.sh" ]]; then
        add_attribution "$file"
    fi
done

echo "Attribution headers added successfully!"