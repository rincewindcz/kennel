#!/bin/sh
# Rasterizes assets/icons/svg/*.svg (Lucide, stroke currentColor) to white PNGs in two sizes.
set -eu
cd "$(dirname "$0")/../assets/icons"
for size in 20 32; do
    mkdir -p "$size"
    for svg in svg/*.svg; do
        name=$(basename "$svg" .svg)
        sed 's/currentColor/#ffffff/g' "$svg" | rsvg-convert -w "$size" -h "$size" -o "$size/$name.png"
    done
done
