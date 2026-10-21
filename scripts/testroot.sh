#!/bin/bash
# Builds build/testroot, a goodluckOS-like tree for running the host build: the device's system
# .puppy files and icons, the HOME .puppy files of the goodluck ports, empty ROM files, a few
# generated per-ROM images and the kennel-read test books. Launch commands point at device paths
# and fail on the host.
# Environment: GOODLUCK (required: goodluck tree with glos-root/, out/ and the port folders)
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
goodluck=${GOODLUCK:?set GOODLUCK to the goodluck tree}
root=$repo/build/testroot
home=$root/home/player

rm -rf "$root"
mkdir -p "$root/usr/share" "$home/.local/share/applications" "$home/.local/share/icons"
cp -r "$goodluck/glos-root/usr/share/puppy" "$goodluck/glos-root/usr/share/icons" "$root/usr/share/"
[[ -f $goodluck/upstream/goodluckOS/home/overlay/apps.puppy ]] && cp "$goodluck/upstream/goodluckOS/home/overlay/apps.puppy" "$home/"

for dir in "$goodluck"/out/*/stage/.local/share "$goodluck"/out/stage/.local/share; do
    [[ -d $dir/applications ]] && cp "$dir"/applications/*.puppy "$home/.local/share/applications/"
    [[ -d $dir/icons ]] && cp "$dir"/icons/* "$home/.local/share/icons/"
done
[[ -f $goodluck/dosbox/dosbox.puppy ]] && cp "$goodluck/dosbox/dosbox.puppy" "$home/.local/share/applications/"

roms() {
    local system=$1
    shift
    mkdir -p "$home/roms/$system/icons"
    for name in "$@"; do : >"$home/roms/$system/$name"; done
}
roms snes "Super Metroid (USA, Europe).sfc" "Chrono Trigger (USA).sfc" "The Legend of Zelda - A Link to the Past (USA).sfc" \
    "Donkey Kong Country (USA) (Rev 2).sfc" "Super Mario World (USA).sfc" "F-Zero (USA).sfc" "Secret of Mana (USA).sfc" \
    "EarthBound (USA).sfc" "Mega Man X (USA) (Rev 1).sfc"
roms gba "Advance Wars (USA).gba" "Metroid Fusion (USA).gba" "Golden Sun (USA, Europe).gba" "Mother 3 (Japan) [T-En].gba"
roms nes "Super Mario Bros. 3 (USA).nes" "Mega Man 2 (USA).nes" "Castlevania (USA) (Rev 1).nes" "Kirby's Adventure (USA).nes"
roms psx "Castlevania - Symphony of the Night (USA).chd" "Final Fantasy VII (USA) (Disc 1).chd" "Crash Bandicoot (USA).chd"
roms genesis "Sonic the Hedgehog 2 (World).md" "Streets of Rage 2 (USA).md" "Gunstar Heroes (USA).md"
roms dos "Seek and Destroy.zip" "Dark Forces.zip"

# Per-ROM art: one landscape image (cover), one square icon (contained on a stock card).
art() {
    local file=$1 size=$2 from=$3 to=$4 label=$5
    magick -size "$size" "gradient:$from-$to" -gravity center -fill white -pointsize 40 -annotate 0 "$label" "$file"
}
art "$home/roms/snes/icons/Super Metroid (USA, Europe).png" 480x320 '#1d3557' '#e63946' 'SUPER METROID'
art "$home/roms/snes/icons/Chrono Trigger (USA).jpg" 960x640 '#2a9d8f' '#264653' 'CHRONO TRIGGER'
art "$home/roms/snes/icons/F-Zero (USA).png" 128x128 '#ffb703' '#fb8500' 'F'
art "$home/roms/gba/icons/Metroid Fusion (USA).png" 240x160 '#6a4c93' '#1982c4' 'FUSION'

# Books for kennel-read (scripts/testbooks.sh), shown by the Books entry.
"$repo/scripts/testbooks.sh" >/dev/null
mkdir -p "$home/books"
cp "$repo"/build/testbooks/* "$home/books/"
# Optional: own books with covers (KENNEL_TEST_BOOKS).
extra=${KENNEL_TEST_BOOKS:-}
if [[ -d $extra ]]; then
    find "$extra" -maxdepth 1 -type f \( -iname '*.epub' -o -iname '*.fb2' -o -iname '*.mobi' \) -exec cp {} "$home/books/" \;
fi
cp "$repo/platforms/goodluckos/books.puppy" "$home/.local/share/applications/"

cat <<MSG
testroot ready: $root
run the host build with:
  env -u XDG_CONFIG_HOME -u XDG_DATA_HOME -u XDG_CACHE_HOME KENNEL_ROOT=$root HOME=$home \\
      KENNEL_PLATFORM=goodluckos $repo/build/host/kennel
MSG
