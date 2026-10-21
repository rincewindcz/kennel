#!/bin/bash
# Builds kennel. Usage: scripts/build.sh [host|armhf|all]   (default host)
#   host   native build in build/host (Arch: sdl3, sdl3_ttf, alsa-lib)
#   armhf  cross build in build/armhf against the Debian trixie sysroot (scripts/sysroot.sh),
#          then checks the binary's dynamic symbols against the goodluckOS rootfs in GLOS_ROOT
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
target=${1:-host}
glos_root=${GLOS_ROOT:-/mnt/ext/r36s/goodluck/glos-root}

build_host() {
    cmake -G Ninja -S "$repo" -B "$repo/build/host" -DCMAKE_BUILD_TYPE=Release
    ninja -C "$repo/build/host"
}

# Every undefined dynamic symbol of the binary must be exported by one of its NEEDED libraries
# in the device rootfs. Versioned names (memcpy@GLIBC_2.4) are compared by name only.
check_symbols() {
    local bin=$1 root=$2 lib path missing
    local exports undefined
    exports=$(mktemp)
    undefined=$(mktemp)
    trap 'rm -f -- "${exports:-}" "${undefined:-}"; trap - RETURN' RETURN
    for lib in $(llvm-readelf -d "$bin" | sed -n 's/.*(NEEDED).*\[\(.*\)\]/\1/p'); do
        path=""
        for dir in "$root/usr/lib" "$root/lib"; do
            [[ -e $dir/$lib ]] && { path=$dir/$lib; break; }
        done
        [[ -n $path ]] || { echo "symbol check: $lib not found in $root" >&2; return 1; }
        # Symlinks inside the rootfs may be absolute: resolve them below the rootfs.
        while [[ -L $path ]]; do
            local link
            link=$(readlink "$path")
            [[ $link == /* ]] && path=$root$link || path=$(dirname "$path")/$link
        done
        llvm-nm -D --defined-only "$path" | awk '{print $NF}' | sed 's/@.*//' >>"$exports"
    done
    llvm-nm -D --undefined-only "$bin" | awk '$1 == "U" {print $2}' | sed 's/@.*//' | sort -u >"$undefined"
    sort -u -o "$exports" "$exports"
    missing=$(comm -23 "$undefined" "$exports")
    if [[ -n $missing ]]; then
        echo "symbol check: missing in $root:" >&2
        echo "$missing" >&2
        return 1
    fi
    echo "symbol check: $(wc -l <"$undefined") symbols resolved against $root"
}

build_armhf() {
    "$repo/scripts/sysroot.sh"
    cmake -G Ninja -S "$repo" -B "$repo/build/armhf" -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_TOOLCHAIN_FILE="$repo/cmake/armhf-linux.cmake" -DKENNEL_STATIC_CXX=ON
    ninja -C "$repo/build/armhf"
    if [[ -d $glos_root ]]; then
        check_symbols "$repo/build/armhf/kennel" "$glos_root"
    else
        echo "symbol check skipped: $glos_root missing"
    fi
}

case $target in
    host) build_host ;;
    armhf) build_armhf ;;
    all) build_host; build_armhf ;;
    *) echo "usage: $0 [host|armhf|all]" >&2; exit 2 ;;
esac
