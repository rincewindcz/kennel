#!/bin/bash
# Copies build/stage-glos (kennel, kennel-read, kennel-term, the Books, Terminal and kennel entries) to a
# goodluckOS device over the USB serial console (glos.py, root) and creates ~/books.
# Files whose md5 already matches on the device are skipped.
# Usage: scripts/deploy-glos.sh [--launcher] [--fonts]
#   --launcher  also installs tools/launcher-glos.sh as /home/player/.local/launcher.sh
#   --fonts     also copies the optional reading fonts (build/stage-glos-fonts)
# Environment: GLOS_PY (required: path of glos.py), GLOS_PORT (default /dev/ttyACM0)
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
stage=$repo/build/stage-glos
fonts_stage=$repo/build/stage-glos-fonts
glos_py=${GLOS_PY:?set GLOS_PY to the path of glos.py}
port=${GLOS_PORT:-/dev/ttyACM0}
home=/home/player
launcher=0
fonts=0
for arg in "$@"; do
    case $arg in
        --launcher) launcher=1 ;;
        --fonts) fonts=1 ;;
        *) echo "unknown option $arg" >&2; exit 2 ;;
    esac
done

[[ -d $stage/ports/kennel && -d $fonts_stage/ports ]] || { echo "$stage missing, run scripts/stage-glos.sh" >&2; exit 1; }
glos() { python3 "$glos_py" --port "$port" --timeout 300 "$@"; }
# The device shell (busybox ash) has no brace expansion: list the entries here.
entries=".local/share/applications/books.puppy .local/share/applications/terminal.puppy .local/share/applications/kennel.puppy"

# md5 of what is already on the device; one short command, the console has a line limit.
declare -A source=()
collect() {
    local root=$1 f
    shift
    while IFS= read -r f; do source[$f]=$root/$f; done < <(cd "$root" && find "$@" -type f)
}
collect "$stage" ports .local
((fonts)) && collect "$fonts_stage" ports
mapfile -t files < <(printf '%s\n' "${!source[@]}" | sort)
remote=$(glos sh "cd $home && { [ -d ports/kennel ] && find ports/kennel -type f | xargs md5sum; md5sum $entries; } 2>/dev/null; true")

declare -A changed=()
for f in "${files[@]}"; do
    sum=$(md5sum "${source[$f]}" | cut -d' ' -f1)
    grep -qxF "$sum  $f" <<<"$remote" && continue
    dir=$(dirname "$f")
    changed[$dir]+="${source[$f]}"$'\n'
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

glos sh "mkdir -p $home/books && cd $home && chown player:player books $entries && chown -R player:player $home/ports/kennel && chmod 755 $home/ports/kennel/kennel $home/ports/kennel/kennel-read $home/ports/kennel/kennel-term $home/ports/kennel/kennel-run" >/dev/null
echo "deployed to $home/ports/kennel"
