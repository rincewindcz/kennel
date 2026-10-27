#!/bin/bash
# Stages the armhf build for goodluckOS as a HOME tree in build/stage-glos and packs it as
# build/kennel-glos.zip (unzip into the HOME root): kennel, kennel-read, kennel-term, the Books and
# Terminal entries, the kennel entry for starting it from Puppy and an empty books folder. The launcher script is not included:
# switch launchers in Settings > Launcher at boot, or install tools/launcher-glos.sh.
# Reading fonts other than the core families go to build/stage-glos-fonts, packed as build/kennel-fonts.zip
# (also unzipped into the HOME root).
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
stage=$repo/build/stage-glos
dest=$stage/ports/kennel
fonts_stage=$repo/build/stage-glos-fonts
fonts_dest=$fonts_stage/ports/kennel/assets/fonts/read
core_fonts=" Literata Inter Atkinson "
bin=$repo/build/armhf/kennel
reader=$repo/build/armhf/kennel-read
term=$repo/build/armhf/kennel-term

for b in "$bin" "$reader" "$term"; do
    [[ -x $b ]] || { echo "$b missing, run scripts/build.sh armhf" >&2; exit 1; }
done

rm -rf "$stage" "$fonts_stage" "$repo/build/kennel-glos.zip" "$repo/build/kennel-fonts.zip"
mkdir -p "$dest/assets/icons" "$dest/assets/fonts/read" "$dest/platforms" "$fonts_dest"
install -m 0755 "$bin" "$reader" "$term" "$repo/tools/kennel-run" "$dest/"
cp "$repo"/assets/fonts/*.* "$dest/assets/fonts/"
for f in "$repo"/assets/fonts/read/*; do
    family=$(basename "$f")
    [[ $core_fonts == *" ${family%%-*} "* ]] && cp "$f" "$dest/assets/fonts/read/" || cp "$f" "$fonts_dest/"
done
cp -r "$repo/assets/term" "$dest/assets/"
cp -r "$repo/assets/icons/20" "$repo/assets/icons/32" "$dest/assets/icons/"
cp "$repo/assets/icons/LICENSE-lucide.txt" "$dest/assets/icons/"
cp "$repo/assets/CONTRIBUTORS" "$dest/assets/"
cp "$repo/platforms/goodluckos.ini" "$dest/platforms/"
mkdir -p "$stage/.local/share/applications" "$stage/books"
cp "$repo/platforms/goodluckos/"{books,terminal,kennel}.puppy "$stage/.local/share/applications/"

(cd "$stage" && zip -qr "$repo/build/kennel-glos.zip" ports .local books)
(cd "$fonts_stage" && zip -qr "$repo/build/kennel-fonts.zip" ports)
echo "staged $stage, packed build/kennel-glos.zip ($(du -h "$repo/build/kennel-glos.zip" | cut -f1))"
echo "staged $fonts_stage, packed build/kennel-fonts.zip ($(du -h "$repo/build/kennel-fonts.zip" | cut -f1))"
