#!/usr/bin/env bash
# This script scans all .h and .hpp files and adds missing header guards
# The format of the guard follows the convention of "parameter_prev_frame.hpp"

set -euo pipefail

# Path to reference file (used only for formatting style or comments)
REFERENCE_FILE="parameter_prev_frame.hpp"

# Function to normalize a file name into a valid macro name
make_guard_name() {
    local file="$1"
    local base
    base=$(basename "$file")
    echo "${base^^}" | sed -E 's/[^A-Z0-9_]/_/g'
}

# Function to check if file already has a guard
has_guard() {
    grep -qE '^\s*#\s*ifndef\s+\S+' "$1"
}

# Loop over all .h / .hpp files
find . -type f \( -name "*.h" -o -name "*.hpp" \) | while read -r file; do
    if has_guard "$file"; then
        echo "✅ Guard already present: $file"
        continue
    fi

    guard=$(make_guard_name "$file")
    echo "🧩 Adding guard to: $file  (#ifndef ${guard})"

    tmp=$(mktemp)

    {
        echo "#ifndef ${guard}"
        echo "#define ${guard}"
        echo
        cat "$file"
        echo
        echo "#endif // ${guard}"
    } > "$tmp"

    mv "$tmp" "$file"
done
