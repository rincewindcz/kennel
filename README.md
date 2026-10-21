# kennel

Game and app launcher for Linux handhelds with a 640x480 screen and a gamepad. Written in C++20 on SDL3 and SDL3_ttf. First target: goodluckOS on the GA36-MB (R36S clone, Allwinner A23, Mali-400), where it replaces Puppy and reads the same `.puppy` files. Device integration lives in platform profiles (`platforms/*.ini`), so other systems need a profile, not code changes.

Design and implementation notes: [docs/DESIGN.md](docs/DESIGN.md).

## Features

| Area | Content |
|---|---|
| Home | one category at a time; carousel with a large focused card (300x200), neighbours scaled and dimmed; title, play time, last played and session count under the card; blurred game art as background |
| Categories | Continue (recently played), Favorites, then the `.puppy` categories; hide and reorder in Settings |
| Art | `ART`, per-ROM images in `ART_DIRECTORIES` and `ENTRY_ICONS_DIRECTORIES`, `ICON`; without an image a stock card (category image or colour, title); decoded on a worker thread, cached as QOI |
| Top bar | clock (or uptime when the clock is not set), battery with charging state, volume and output, TF card, USB link; expert mode adds load and free RAM |
| Bottom bar | button hints; dock (SELECT) with Settings, Expert, Statistics, Screen off, Eject card, Restart, Power off; destructive actions ask first |
| Quick settings | START: brightness, volume, mute, speaker or headphones, shortcuts |
| Details | X: statistics of the entry, Play, Favorite, Start at boot, last app log, reset statistics; expert: run in terminal, entry internals |
| Statistics | per entry play time, sessions, last played, last exit code; statistics screen; error report when an app exits with a failure code |
| Expert mode | system status, logs (app, launcher, kernel, port logs), processes with terminate/kill, library diagnostics, sessions, frame time overlay, Gallium HUD for the next launch, restart kennel, switch to the previous launcher |
| Animations | fade, slide and scale only; Normal, Fast or Off; no redraw while idle |

## Controls

| Action | Pad | Host keyboard |
|---|---|---|
| Move | d-pad, left stick (hold repeats) | arrows |
| Confirm (Play) | A (south; B with "Confirm button: East") | Enter, Z |
| Back | B (east) | Esc, Backspace, X |
| Details | X (west) | A |
| Favorite | Y (north) | S |
| Jump 5 / page | L1, R1 | Q, W |
| Previous / next letter | L2, R2 | 1, 2 |
| Quick settings | START | Space |
| Dock | SELECT | Tab |

Up/down switch categories on Home. Global hotkeys of the system (goodluckOS: START+SELECT stops the app, MODE combinations) are not handled by kennel.

## Build

Requirements: CMake, Ninja, clang and lld (cross build), Arch packages `sdl3`, `sdl3_ttf`, `alsa-lib` (host build). Icons are rasterized PNGs in `assets/icons/{20,32}`; `scripts/icons.sh` regenerates them from `assets/icons/svg` with `rsvg-convert`.

```sh
scripts/build.sh host     # build/host/kennel
scripts/build.sh armhf    # build/armhf/kennel, armv7-a hard float, static libstdc++
scripts/build.sh all
```

`armhf` first runs `scripts/sysroot.sh`, which assembles `build/sysroot-armhf` from Debian trixie armhf packages (glibc, libstdc++ 14, SDL3 3.2, SDL3_ttf 3.2, ALSA); the package index signature is checked with `debian-archive-keyring` when installed, packages by SHA256. The binary links only glibc, SDL3, SDL3_ttf and ALSA dynamically. After the build, every undefined dynamic symbol is checked against the libraries of the goodluckOS rootfs in `GLOS_ROOT` (default `/mnt/ext/r36s/goodluck/glos-root`); a missing symbol fails the build. CMake options: `KENNEL_ALSA` (default on), `KENNEL_STATIC_CXX` (default off, on for `armhf`).

## Run on the host

`scripts/testroot.sh` builds `build/testroot`: the goodluckOS system `.puppy` files and icons, the HOME `.puppy` files of the goodluck ports, empty ROM files and a few generated ROM images. It prints the command line:

```sh
env -u XDG_CONFIG_HOME -u XDG_DATA_HOME -u XDG_CACHE_HOME \
    KENNEL_ROOT=$PWD/build/testroot HOME=$PWD/build/testroot/home/player \
    KENNEL_ASSETS=$PWD/assets KENNEL_PLATFORM=goodluckos build/host/kennel
```

`KENNEL_ROOT` prefixes absolute library paths (sources, ROM and art directories, icons); `/proc`, `/sys` and `/dev` are never prefixed. Launch commands point at device paths and fail on the host.

## Install on goodluckOS

```sh
scripts/build.sh armhf
scripts/stage-glos.sh        # build/stage-glos, build/kennel-glos.zip
```

Unzip `kennel-glos.zip` into the HOME root (MTP: `GA36MB -> HOME`). It contains `ports/kennel/` only: binary, `kennel-run`, assets and `platforms/goodluckos.ini`. Make kennel the launcher in one of two ways:

- start it once (for example from a Puppy entry with `COMMAND=/home/player/ports/kennel/kennel`) and set Settings > Launcher at boot to kennel;
- copy `tools/launcher-glos.sh` to `HOME/.local/launcher.sh`.

The launcher script runs kennel and starts Puppy when kennel exits with an error, so a broken build does not leave the unit without a launcher. Settings > Launcher at boot switches back to Puppy.

Over the USB serial console (root, `glos.py` from the goodluck tree):

```sh
scripts/deploy-glos.sh               # changed files only (md5 compared on the device)
scripts/deploy-glos.sh --launcher    # also installs tools/launcher-glos.sh as .local/launcher.sh
```

Environment: `GLOS_PY` (default `/mnt/ext/r36s/goodluck/tools/glos.py`), `GLOS_PORT` (default `/dev/ttyACM0`). The console transfers about 0.75 MB/s.

## Files

| Path | Content |
|---|---|
| `$XDG_CONFIG_HOME/kennel/kennel.ini` (default `~/.config/kennel`) | settings |
| `$XDG_DATA_HOME/kennel/` (default `~/.local/share/kennel`) | `sessions.tsv`, `favorites`, `boot`, `journal` |
| `$XDG_CACHE_HOME/kennel/art/` (default `~/.cache/kennel`) | QOI thumbnails and backdrops, keyed by path, size and mtime |
| `$XDG_RUNTIME_DIR/kennel/`, else `/dev/shm/kennel-UID`, else `/tmp/kennel-UID` | `state` (row and entry to restore), `kennel.log` |
| next to the binary, or `KENNEL_ASSETS` | `assets/` (fonts, icons), `platforms/` |

## Library: .puppy files

Read as Puppy does: the files in the platform's `sources`, in order (globs sorted); `[ENTRY]` keyed by `CATEGORY` and `NAME`, `[ARCHIVE]` keyed by `NAME`; a later definition replaces an earlier one in place. Keys used from Puppy: `CATEGORY`, `NAME`, `DESCRIPTION`, `COMMAND` (`%s` is the quoted ROM path), `ICON`, `TERMINAL`, `ENTRY_DIRECTORIES`, `ENTRY_EXTENSIONS`, `ENTRY_ICONS_DIRECTORIES`, `DEFAULT_ICON`. Additional keys, ignored by Puppy:

| Key | Section | Meaning |
|---|---|---|
| `ART` | `ENTRY` | card image, preferred over `ICON` |
| `ART_DIRECTORIES` | `ARCHIVE` | directories searched for `<stem>.png`, `.jpg`, `.jpeg` before `ENTRY_ICONS_DIRECTORIES` |
| `COLOR` | both | `#rrggbb` accent of generated cards and the category |
| `HIDDEN` | both | not shown on Home |

Images close to 3:2 and at least half the card size cover the card; smaller or other shapes are shown as an icon on a stock card. ROM titles drop trailing `(...)` and `[...]` groups, which are shown as tags.

## Statistics

Every launch runs the command through `kennel-run JOURNAL ID COMMAND`, which appends a start record (boot id, uptime, wall time, entry id) and an end record (uptime, wall time, exit status) to `journal`. On start kennel folds the journal into `sessions.tsv`:

| Case | Session |
|---|---|
| start and end record | complete, with exit status |
| no end record, same boot (wrapper killed: `appctl kill-application`) | ends at kennel's start, marked killed |
| no end record, earlier boot (power off in game) | counted, length unknown, not added to play time |

`sessions.tsv` columns: sequence, boot index, start wall time, start uptime, duration, exit status, flags (`k` killed, `u` unknown length), entry id. Durations use `CLOCK_BOOTTIME`; "last played" uses the wall clock when it is set (year 2024 or later), otherwise uptime within the same boot or the number of boots since.

## Platform profiles

`platforms/<id>.ini`, chosen by `ID` in `/etc/os-release`, or by `KENNEL_PLATFORM` (a name in `platforms/`, or a path). Without a profile: built-in defaults (sources `~/.config/kennel/apps.puppy` and `~/.config/kennel/apps/*.puppy`, exec launch, windowed).

`[platform]`:

| Key | Meaning |
|---|---|
| `id`, `name` | identification |
| `launch` | `exec`: kennel releases the display, runs the app, waits and restarts itself; `command`: runs `launch_command` and expects to be stopped (goodluckOS appd) |
| `launch_command`, `terminal_flag` | `%c` the quoted command, `%t` `terminal_flag` for terminal entries |
| `stop_app`, `kill_app` | commands that end the running app |
| `poweroff`, `reboot`, `eject`, `external_mount`, `screen_off` | system actions; empty hides the action |
| `backlight` | sysfs backlight directory (default: first of `/sys/class/backlight/*`) |
| `mixer_card`, `mixer_volume`, `mixer_output_switch` | ALSA card, volume control, speaker switch |
| `sources` | `.puppy` files and globs, `;`-separated, `~` is HOME |
| `hide_categories` | categories hidden until changed in Settings |
| `autolaunch` | file appd runs once at boot (Puppy format) |
| `app_log` | log of the last app |
| `system_icons` | directory with `<category>.png` images |
| `fallback_fonts` | fonts for glyphs missing in Inter (CJK) |
| `hud_env` | environment assignment for "Gallium HUD on next launch" |
| `launcher_script`, `previous_launcher` | launcher selection in Settings |
| `kernel_log` | command printing the kernel log |
| `mounts` | file systems shown in the status page |
| `width`, `height`, `fullscreen` | logical screen size, window mode |

`[logs]`: `Name=path` entries for the log viewer; `glob=pattern` adds matching files. `[status]`: `Name=command` entries whose output is shown in the status page.

## Licenses

| Component | License |
|---|---|
| Inter, JetBrains Mono (`assets/fonts`) | SIL Open Font License 1.1 |
| Lucide icons (`assets/icons`) | ISC |
| stb_image (`third_party/stb_image.h`) | public domain / MIT |
| QOI (`third_party/qoi.h`) | MIT |
| SDL3, SDL3_ttf | zlib |
