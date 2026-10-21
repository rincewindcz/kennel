#!/bin/bash
# Builds the armhf sysroot (Debian trixie) for cmake/armhf-linux.cmake.
# Usage: scripts/sysroot.sh [DEST]   (default build/sysroot-armhf)
# Package index signature checked with debian-archive-keyring when installed, packages by SHA256.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
dest=${1:-$repo/build/sysroot-armhf}
cache=$repo/build/debs
suite=trixie
mirror=http://deb.debian.org/debian
keyring=/usr/share/keyrings/debian-archive-keyring.gpg
packages=(libc6 libc6-dev linux-libc-dev libgcc-14-dev libgcc-s1 libstdc++-14-dev libstdc++6
          libsdl3-0 libsdl3-dev libsdl3-ttf0 libsdl3-ttf-dev libasound2t64 libasound2-dev zlib1g zlib1g-dev)

stamp=$dest/.packages
[[ -f $stamp && $(<"$stamp") == "${packages[*]}" ]] && exit 0

mkdir -p "$cache"
curl -fsSL -o "$cache/InRelease" "$mirror/dists/$suite/InRelease"
if [[ -r $keyring ]]; then
    gpgv --keyring "$keyring" "$cache/InRelease"
else
    echo "warning: $keyring missing, Release signature not verified" >&2
fi
curl -fsSL -o "$cache/Packages.xz" "$mirror/dists/$suite/main/binary-armhf/Packages.xz"
want=$(awk '/SHA256:/{f=1;next} /^[A-Z]/{f=0} f && $3=="main/binary-armhf/Packages.xz"{print $1}' "$cache/InRelease")
(cd "$cache" && echo "$want  Packages.xz" | sha256sum -c --quiet)

rm -rf "$dest"
mkdir -p "$dest/usr/lib" "$dest/usr/bin" "$dest/usr/sbin"
ln -s usr/lib "$dest/lib"
ln -s usr/bin "$dest/bin"
ln -s usr/sbin "$dest/sbin"

index=$(xz -dc "$cache/Packages.xz")
for pkg in "${packages[@]}"; do
    read -r file sha < <(awk -v p="$pkg" '$1=="Package:"{cur=$2} cur==p && $1=="Filename:"{f=$2} cur==p && $1=="SHA256:"{print f, $2; exit}' <<<"$index")
    [[ -n ${file:-} ]] || { echo "package $pkg not found" >&2; exit 1; }
    deb=$cache/$(basename "$file")
    [[ -f $deb ]] || curl -fsSL -o "$deb" "$mirror/$file"
    echo "$sha  $deb" | sha256sum -c --quiet
    tmp=$(mktemp -d)
    (cd "$tmp" && ar x "$deb" && tar -xf data.tar.* -C "$dest")
    rm -rf "$tmp"
    unset file sha
done
echo "${packages[*]}" > "$stamp"
