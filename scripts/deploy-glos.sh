#!/bin/bash
# Copies build/stage-glos to a goodluckOS device over the USB serial console (glos.py, root).
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
mapfile -t files < <(cd "$stage" && find ports -type f | sort)
remote=$(glos sh "cd $home && [ -d ports/kennel ] && find ports/kennel -type f | xargs md5sum; true")

declare -A changed=()
for f in "${files[@]}"; do
    sum=$(md5sum "$stage/$f" | cut -d' ' -f1)
    grep -qxF "$sum  $f" <<<"$remote" && continue
    dir=$(dirname "$f")
    changed[$dir]+="$stage/$f"$'\n'
done

for dir in "${!changed[@]}"; do
    mapfile -t list < <(printf '%s' "${changed[$dir]}")
    glos push "${list[@]}" "$home/$dir"
done
[[ ${#changed[@]} -eq 0 ]] && echo "device up to date"

if ((launcher)); then
    tmp=$(mktemp -d)
    cp "$repo/tools/launcher-glos.sh" "$tmp/launcher.sh"
    glos push "$tmp/launcher.sh" "$home/.local"
    rm -rf "$tmp"
    glos sh "chmod 755 $home/.local/launcher.sh; chown player:player $home/.local/launcher.sh" >/dev/null
fi

glos sh "chown -R player:player $home/ports/kennel && chmod 755 $home/ports/kennel/kennel $home/ports/kennel/kennel-run" >/dev/null
echo "deployed to $home/ports/kennel"
