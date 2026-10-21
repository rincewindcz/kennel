#!/bin/bash
# Copies build/stage-glos (kennel, kennel-read, the Books entry) to a goodluckOS device over the USB
# serial console (glos.py, root) and creates ~/books.
# Files whose md5 already matches on the device are skipped.
# Usage: scripts/deploy-glos.sh [--launcher]   --launcher also installs tools/launcher-glos.sh
#        as /home/player/.local/launcher.sh
# Environment: GLOS_PY (default /mnt/ext/r36s/goodluck/tools/glos.py), GLOS_PORT (default /dev/ttyACM0)
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
stage=$repo/build/stage-glos
glos_py=${GLOS_PY:-/mnt/ext/r36s/goodluck/tools/glos.py}
port=${GLOS_PORT:-/dev/ttyACM0}
home=/home/player
launcher=0
[[ ${1:-} == --launcher ]] && launcher=1

[[ -d $stage/ports/kennel ]] || { echo "$stage missing, run scripts/stage-glos.sh" >&2; exit 1; }
glos() { python3 "$glos_py" --port "$port" --timeout 300 "$@"; }

# md5 of what is already on the device; one short command, the console has a line limit.
mapfile -t files < <(cd "$stage" && find ports .local -type f | sort)
remote=$(glos sh "cd $home && { [ -d ports/kennel ] && find ports/kennel -type f | xargs md5sum; md5sum .local/share/applications/books.puppy; } 2>/dev/null; true")

declare -A changed=()
for f in "${files[@]}"; do
    sum=$(md5sum "$stage/$f" | cut -d' ' -f1)
    grep -qxF "$sum  $f" <<<"$remote" && continue
    dir=$(dirname "$f")
    changed[$dir]+="$stage/$f"$'\n'
done

# Files go to a directory on the same filesystem first and are renamed into place: writing over
# the running binary fails with ETXTBSY, a rename does not.
incoming=$home/ports/.kennel-incoming
for dir in "${!changed[@]}"; do
    mapfile -t list < <(printf '%s' "${changed[$dir]}")
    glos push "${list[@]}" "$incoming/$dir"
done
if ((${#changed[@]})); then
    glos sh "cd $incoming && find . -type f | while read -r f; do mkdir -p \"$home/\${f%/*}\" && mv -f \"\$f\" \"$home/\$f\"; done; cd / && rm -rf $incoming" >/dev/null
else
    echo "device up to date"
fi

if ((launcher)); then
    tmp=$(mktemp -d)
    cp "$repo/tools/launcher-glos.sh" "$tmp/launcher.sh"
    glos push "$tmp/launcher.sh" "$home/.local"
    rm -rf "$tmp"
    glos sh "chmod 755 $home/.local/launcher.sh; chown player:player $home/.local/launcher.sh" >/dev/null
fi

glos sh "mkdir -p $home/books && chown player:player $home/books $home/.local/share/applications/books.puppy && chown -R player:player $home/ports/kennel && chmod 755 $home/ports/kennel/kennel $home/ports/kennel/kennel-read $home/ports/kennel/kennel-run" >/dev/null
echo "deployed to $home/ports/kennel"
