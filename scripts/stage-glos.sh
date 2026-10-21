#!/bin/bash
# Stages the armhf build for goodluckOS as a HOME tree in build/stage-glos and packs it as
# build/kennel-glos.zip (unzip into the HOME root): kennel, kennel-read, the Books entry and an
# empty books folder. The launcher script is not included:
# switch launchers in Settings > Launcher at boot, or install tools/launcher-glos.sh.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
stage=$repo/build/stage-glos
dest=$stage/ports/kennel
bin=$repo/build/armhf/kennel
reader=$repo/build/armhf/kennel-read

for b in "$bin" "$reader"; do
    [[ -x $b ]] || { echo "$b missing, run scripts/build.sh armhf" >&2; exit 1; }
done

rm -rf "$stage" "$repo/build/kennel-glos.zip"
mkdir -p "$dest/assets/icons" "$dest/platforms"
install -m 0755 "$bin" "$reader" "$repo/tools/kennel-run" "$dest/"
cp -r "$repo/assets/fonts" "$dest/assets/"
cp -r "$repo/assets/icons/20" "$repo/assets/icons/32" "$dest/assets/icons/"
cp "$repo/assets/icons/LICENSE-lucide.txt" "$dest/assets/icons/"
cp "$repo/platforms/goodluckos.ini" "$dest/platforms/"
mkdir -p "$stage/.local/share/applications" "$stage/books"
cp "$repo/platforms/goodluckos/books.puppy" "$stage/.local/share/applications/"

(cd "$stage" && zip -qr "$repo/build/kennel-glos.zip" ports .local books)
echo "staged $stage, packed build/kennel-glos.zip ($(du -h "$repo/build/kennel-glos.zip" | cut -f1))"
