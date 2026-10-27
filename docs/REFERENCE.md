# kennel: reference

Game and app launcher for Linux handhelds with a 640x480 screen and a gamepad. Written in C++20 on SDL3 and SDL3_ttf. First target: goodluckOS on the GA36-MB (R36S clone, Allwinner A23, Mali-400), where it replaces Puppy and reads the same `.puppy` files. Device integration lives in platform profiles (`platforms/*.ini`), so other systems need a profile, not code changes.

The repository builds kennel and `kennel-read`, a minimal e-book reader (section "kennel-read"), and `kennel-term`, a terminal with an on-screen keyboard and command suggestions (section "kennel-term").

Design and implementation notes: [DESIGN.md](DESIGN.md).

## Features

| Area | Content |
|---|---|
| Home | one category at a time; carousel with a large focused card (300x200), neighbours scaled and dimmed, circular with more than two entries (the first card has the last one on its left); title, play time, last played and session count under the card; blurred game art as background |
| Categories | Continue (recently played), Favorites, then the `.puppy` categories; hide and reorder in Settings |
| Hidden entries | Details > Hide from Home; Settings > Hidden entries lists them by category and shows them again |
| Appearance | accent colours Blue, Teal, Amber, Rose, Violet, Green, Crimson or a custom RGB colour; background gradient, game art of the selected entry, or a wallpaper (Files: X on an image or in the image viewer, Set as wallpaper); animation speed |
| Backup | Settings > Backup copies settings and data to `kennel-backup` on the external card (HOME without a card) and restores them |
| Art | `ART`, per-ROM images in `ART_DIRECTORIES` and `ENTRY_ICONS_DIRECTORIES`, `ICON`; without an image a stock card (category image or colour, title); decoded on a worker thread, cached as QOI |
| Top bar | icon and value for the clock (or uptime when the clock is not set), CPU load, free memory, volume and output, battery with charging state, TF card, USB link (the icon of the USB mode, dim in network mode until the PC sent a packet); Settings > Top bar selects the indicators |
| Bottom bar | button hints; dock (SELECT) with Settings, Expert and Terminal (expert mode), Files, Statistics, Screen off, Eject card, Restart, Power off; destructive actions ask first |
| Screen off after | Settings > Screen off after turns the backlight off after 1 to 30 minutes without a button press, dimmed for the last 10 s; the next press wakes it and is swallowed |
| Power off | Settings > Power off after (10 minutes to 2 hours without a button press, not on a charger) and Power off on low battery (3, 5 or 8 %, 30 s after a warning) |
| Quick settings | START: battery level with estimated time left, brightness, volume, mute, speaker or headphones, USB mode, shortcuts |
| Files | file browser: open, copy, cut, paste, rename, delete, new folder, info, hidden files; files open with their library entry, an archive's command, the reader, the text viewer or the image viewer; expert mode: terminal in the folder, run scripts |
| Details | X: statistics of the entry, Play, Favorite, Hide from Home, Start at boot, last app log, reset statistics; expert: run in terminal, entry internals |
| Statistics | per entry play time, sessions, last played, last exit code; statistics page with Overview, Most played, Categories, Recent and Battery tabs; error report when an app exits with a failure code |
| Battery | level sampled every minute while kennel or a launched app runs; graph per boot, drain rate and time left |
| Expert mode | Status (live), Logs (platform logs, port logs, kennel, kernel), Processes with terminate/kill, Library report and rescan, Sessions, Terminal, Files from `/`, performance overlay, HUD for the next launch, restart kennel, switch to the previous launcher, sync |
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

Requirements: CMake, Ninja, clang and lld (cross build), Arch packages `sdl3`, `sdl3_ttf`, `alsa-lib`, `zlib` (host build). Icons are rasterized PNGs in `assets/icons/{20,32}`; `scripts/icons.sh` regenerates them from `assets/icons/svg` with `rsvg-convert`.

```sh
scripts/build.sh host     # build/host/kennel, build/host/kennel-read, build/host/kennel-term
scripts/build.sh armhf    # build/armhf/{kennel,kennel-read,kennel-term}, armv7-a hard float, static libstdc++
scripts/build.sh all
```

`armhf` first runs `scripts/sysroot.sh`, which assembles `build/sysroot-armhf` from Debian trixie armhf packages (glibc, libstdc++ 14, SDL3 3.2, SDL3_ttf 3.2, ALSA, zlib); the package index signature is checked with `debian-archive-keyring` when installed, packages by SHA256. `kennel` links only glibc, SDL3, SDL3_ttf and ALSA dynamically, `kennel-read` and `kennel-term` glibc, SDL3, SDL3_ttf and zlib. After the build, every undefined dynamic symbol of the three binaries is checked against the libraries of the goodluckOS rootfs in `GLOS_ROOT` when it is set (an extracted goodluckOS root file system); a missing symbol fails the build. CMake options: `KENNEL_ALSA` (default on), `KENNEL_STATIC_CXX` (default off, on for `armhf`).

## Run on the host

`scripts/testroot.sh` builds `build/testroot`: the goodluckOS system `.puppy` files and icons, the HOME `.puppy` files of the goodluck ports, empty ROM files, a few generated ROM images, and the test books of `scripts/testbooks.sh` in `home/player/books` with the Books entry. `GOODLUCK` names the goodluck tree the files come from. When `KENNEL_TEST_BOOKS` names a folder, its EPUB, FB2 and MOBI files are copied there too, to exercise cover extraction; everything stays in `build/`. It prints the command line:

```sh
env -u XDG_CONFIG_HOME -u XDG_DATA_HOME -u XDG_CACHE_HOME -u XDG_RUNTIME_DIR \
    KENNEL_ROOT=$PWD/build/testroot HOME=$PWD/build/testroot/home/player \
    KENNEL_PLATFORM=goodluckos build/host/kennel
```

CMake symlinks `assets/`, `platforms/` and `tools/kennel-run` into the build directory, so `build/host/kennel` runs from the repository without `KENNEL_ASSETS`. `KENNEL_ROOT` prefixes absolute library paths (sources, ROM and art directories, icons); `/proc`, `/sys` and `/dev` are never prefixed. Launch commands point at device paths and fail on the host.

| Variable | Meaning |
|---|---|
| `KENNEL_PLATFORM` | platform id (searched as `<id>.ini` in `<exe dir>/platforms` and `<exe dir>/../share/kennel/platforms`) or a path containing `/` |
| `KENNEL_ROOT` | test tree prefix for absolute library paths |
| `KENNEL_ASSETS` | asset directory; default `<exe dir>/assets`, then `<exe dir>/../share/kennel/assets` |
| `KENNEL_AUTOMATE` | development script, section "Testing" |

## Install on goodluckOS

```sh
scripts/build.sh armhf
scripts/stage-glos.sh        # build/stage-glos, build/kennel-glos.zip, build/kennel-fonts.zip
```

Unzip `kennel-glos.zip` into the HOME root (MTP: `GA36MB -> HOME`). It contains `ports/kennel/` (`kennel`, `kennel-read`, `kennel-term`, `kennel-run`, assets, `platforms/goodluckos.ini`), the Books, Terminal and kennel entries in `.local/share/applications/` (`books.puppy`, `terminal.puppy`, `kennel.puppy`) and an empty `books/` folder. `kennel-fonts.zip` is optional and is unzipped the same way: the reading fonts beyond Literata, Inter and Atkinson (Alegreya, Bitter, Charis SIL, Crimson Pro, EB Garamond, Lora, Merriweather, PT Serif, Source Serif) in `ports/kennel/assets/fonts/read/`. Make kennel the launcher in one of two ways:

- start it once from Puppy (System > kennel, from `kennel.puppy`) and set Settings > Launcher at boot to kennel;
- copy `tools/launcher-glos.sh` to `HOME/.local/launcher.sh`.

The launcher script runs kennel and starts Puppy when kennel exits with an error, so a broken build does not leave the unit without a launcher. Settings > Launcher at boot switches back to Puppy.

Over the USB serial console (root, `glos.py` from the goodluck tree):

```sh
scripts/deploy-glos.sh               # changed files only (md5 compared on the device), creates HOME/books
scripts/deploy-glos.sh --launcher    # also installs tools/launcher-glos.sh as .local/launcher.sh
scripts/deploy-glos.sh --fonts       # also copies the optional reading fonts
```

Changed files are pushed into `HOME/ports/.kennel-incoming` and then renamed into place on the same file system: writing over the running binary fails with `ETXTBSY`, a rename does not. The running kennel keeps the old binary until it restarts. Environment: `GLOS_PY` (required, path of `glos.py`), `GLOS_PORT` (default `/dev/ttyACM0`). The console transfers about 0.75 MB/s.

## Testing

`scripts/testbooks.sh` fills `build/testbooks`: Project Gutenberg books (War and Peace as EPUB and TXT, Pride and Prejudice as EPUB, TXT and single-file HTML with images, R.U.R. by Karel Capek in Czech as EPUB and TXT) and, generated from the Pride and Prejudice text by `scripts/mkbooks.py` (calibre is not needed), FB2, PalmDOC PDB (compressed and uncompressed) and an uncompressed UTF-8 MOBI.

Host screenshots without a display: `KENNEL_AUTOMATE` runs comma-separated tokens, one every 300 ms; with `SDL_VIDEODRIVER=offscreen` no window is needed.

| Token | Effect |
|---|---|
| `up`, `down`, `left`, `right`, `a`, `b`, `x`, `y`, `l1`, `r1`, `l2`, `r2`, `start`, `select` | logical key press (`a` Confirm, `b` Back) |
| `wait:MS` | pause |
| `shot:PATH` | save the next frame as BMP |
| `type:TEXT` | kennel-term only: TEXT as typed on the keyboard (no commas) |
| `quit` | exit |

```sh
env -u XDG_CONFIG_HOME -u XDG_DATA_HOME -u XDG_CACHE_HOME -u XDG_RUNTIME_DIR SDL_VIDEODRIVER=offscreen \
    KENNEL_ROOT=$PWD/build/testroot HOME=$PWD/build/testroot/home/player KENNEL_PLATFORM=goodluckos \
    KENNEL_AUTOMATE="wait:600,shot:/tmp/home.bmp,down,wait:900,shot:/tmp/row.bmp,x,wait:600,shot:/tmp/details.bmp,quit" \
    build/host/kennel
```

On the device, `tools/padinject.c` writes evdev button presses to `/dev/input/event2` (the GA36-MB gamepad; `-d DEVICE` for another), so kennel can be driven over the serial console. Buttons: `a b x y l1 r1 l2 r2 select start mode l3 r3 up down left right`, optionally `NAME:HOLD_MS` (default 80 ms); `wait:MS` pauses. `a` is `BTN_EAST`, which `SDL_GAMECONTROLLERCONFIG` maps to the south button (Confirm).

```sh
clang --target=arm-linux-gnueabihf --sysroot=build/sysroot-armhf -march=armv7-a -mfpu=neon-vfpv4 \
    -mfloat-abi=hard -fuse-ld=lld -O2 -o build/tools/padinject tools/padinject.c
glos.py push build/tools/padinject /root/bin
glos.py sh '/root/bin/padinject down down right a'
glos.py sh '/root/bin/drmshot > /tmp/shot.ppm'      # screen capture, drmshot from the goodluck tree
```

## kennel-read

Minimal e-book reader: one font, scrolling by lines, no images, no effects. Opened by kennel from the Books category (`books.puppy`, folders `HOME/books` and `/media/external/books`, per-book images in `books/icons`) or directly: `kennel-read BOOK`. `kennel-read --dump BOOK` prints the parsed book. goodluckOS before the `utf8` vfat mount option decodes file names on HOME as ISO 8859-1: characters outside it (e.g. Czech č, ř, š) show as `?`; books with a title in their metadata show that title instead. As with every Puppy archive, files with the same name but different extensions in one category show as one entry (the last one found), so keep one format per book.

| Format | Extensions | Read | Dropped |
|---|---|---|---|
| Plain text | `txt` | UTF-8 only; blank-line separated paragraphs (hard-wrapped text) or one paragraph per line | |
| EPUB 2 and 3 | `epub` | spine order, table of contents, title and author | images, CSS, scripts, DRM-protected books |
| HTML, XHTML | `html`, `htm`, `xhtml` | block structure, headings, italic and bold | images, CSS, scripts |
| FictionBook 2 | `fb2` | sections and titles, emphasis, empty lines | embedded binaries (the cover is shown by kennel) |
| PalmDOC, MOBI | `pdb`, `mobi`, `prc` | PalmDOC (uncompressed and compressed), MOBI without DRM in PalmDOC or no compression | HUFF/CDIC compression, KF8 (AZW3), DRM |

Other formats (PDF, AZW3, DOCX) are converted on a computer, for example with calibre: `ebook-convert book.azw3 book.epub`.

| Control | Action |
|---|---|
| Up / Down | one line or one screen, see settings (holding accelerates) |
| Left / Right, L1 / R1 | one screen |
| L2 / R2 | previous / next chapter |
| SELECT | chapter list |
| START | settings panel |
| B | save the position and exit |

Settings: font family (every family found, see below), size 14 to 36, line spacing 1.0 to 2.0, margins (narrow, normal, wide), alignment (left or justified), formatting (plain: one style; styled: bold headings, italic and bold text), paragraphs (first-line indent or spacing), theme (dark, sepia, light, custom), Up/Down by line or by full page (`scroll=line` or `page` in `read.ini`), status bar on or off with each item on or off (chapter, pages, progress in percent, reading time, battery), brightness. The custom theme shows background and text colour rows with R, G and B sliders (0 to 255); the colours are stored as `custom_bg` and `custom_fg` (`#rrggbb`) in `read.ini` and can be edited there. Brightness sliders here and in kennel's quick settings step one backlight level with the d-pad and a tenth of the range with the stick (the console has 10 levels, so both step one) and never go below level 1. The reading position is a byte offset into the book text, so it survives font and layout changes; progress is shown in percent. Pages are screens as turned with Left/Right (one line of overlap); the page count appears once the whole book is laid out in the background and is recounted after font or layout changes. Reading time is the active time per book: the gap between two button presses counts up to 3 minutes, and the total is stored with the position.

Reading fonts: Literata (default), Inter and Atkinson are bundled; `kennel-fonts.zip` adds Alegreya, Bitter, Charis SIL, Crimson Pro, EB Garamond, Lora, Merriweather, PT Serif, Source Serif. User fonts go to `$XDG_DATA_HOME/kennel/fonts/` (`~/.local/share/kennel/fonts/`) as `<Family>-Regular.ttf`, optionally with `-Bold`, `-Italic` and `-BoldItalic`; missing styles are synthesized. Every family with a Regular file is listed, sorted by name; a file name without spaces is shown split into words (`CharisSIL` as Charis SIL). A user font replaces a bundled family of the same name.

## kennel-term

Terminal for the gamepad: the user's login shell (`sh -l` on goodluckOS) on a pseudo terminal with `TERM=xterm-256color`, drawn with JetBrains Mono through the reader's glyph atlas. Opened from the dock, quick settings and the Expert page (expert mode), from Files (Terminal here, Run), from the Terminal category (`terminal.puppy`), or directly: `kennel-term [--cwd DIR] [-e COMMAND]`. With `-e` the screen stays after the command ends until a button is pressed and the exit status is returned. The terminal is launched like an app (kennel restarts when it closes) and records no play session.

The escape sequence parser covers what busybox tools and ncurses use with that terminfo entry: cursor movement, erase, insert and delete of characters and lines, scroll regions, SGR with 16, 256 and RGB colours, bold, dim, italic, underline, inverse, the alternate screen (1049), application cursor keys, DEC line drawing, device status and attribute reports, window titles. Not supported: double width characters, mouse reporting, reflow on resize. `top`, `less`, `vi` and `alsamixer` were checked. 2000 lines of scrollback.

Two modes, switched with SELECT:

| Mode | Screen | Buttons |
|---|---|---|
| Typing | terminal above the suggestion strip and the keyboard | d-pad moves on the keyboard, A presses the key, B backspace, X space, START enter, L2 shift (twice: caps lock), R2 symbols, L1/R1 choose a suggestion, Y inserts it |
| Keys | full screen terminal, hint bar | d-pad arrows, A enter, B escape, X space, Y tab, L1/R1 page up/down, L2 Ctrl-C, R2 `q`, START menu |

The keyboard has a row of terminal keys (Esc, Tab, Ctrl as a one-shot modifier, arrows, Home, End, PgUp, PgDn) and a gear key that opens the menu: font size (10 to 20), Ctrl-C, Ctrl-D, Ctrl-Z, Ctrl-L, Scroll back (d-pad and L1/R1 move through the scrollback), Clear scrollback, Controls, Close terminal. The right stick scrolls the scrollback in both modes. A USB keyboard types directly.

Suggestions follow what was typed at the shell prompt since the last Enter (kennel-term tracks it; after arrows, Tab or Escape the shell's line is unknown and suggestions pause until Enter, and they are off while a program runs in the foreground). With nothing typed: the most recent commands. Otherwise: earlier commands that continue the line, then completions of the last word: commands from `PATH` and shell builtins for the first word (ranked by use in the history), files and folders of the shell's working directory (`/proc/PID/cwd`) after it, folders only after `cd`; `~/` is expanded and special characters are escaped. Accepted commands get a trailing space, folders a `/`.

The shell gets `ENV=assets/term/shrc` unless `ENV` is set, which sets a prompt with the working directory (goodluckOS `/etc/profile` sets a bare `$ `).

## File browser

Files in the dock and quick settings starts in HOME (Expert > Files in `/`). Folders first, sorted by name; `..` goes up.

| Button | Action |
|---|---|
| A | open the folder or file |
| B | parent folder (closes at `/`) |
| X | actions: Open, Copy, Cut, Paste or Move here, Rename, Delete, Info (path, link target, size, mode, owner), New folder, Show hidden files, Terminal here (expert) |
| Y | HOME |
| START | close |

A file opens with its library entry (an archive file the catalog lists), else with the `[ARCHIVE]` whose `ENTRY_EXTENSIONS` contain its extension (archives whose `ENTRY_DIRECTORIES` hold the file first; with several, a dialog offers up to two plus View), books with kennel-read, images (`png`, `jpg`, `bmp`, `gif`, `qoi`) in the image viewer, text files (no NUL byte in the first 8 KiB) in the text viewer (first 512 KiB). In expert mode executable files and `.sh` scripts can be run in kennel-term. Copy, move across file systems and delete of folders run `cp -r`, `mv` and `rm -rf` on a worker thread; names that exist get a ` (2)` suffix. Rename and New folder use the on-screen keyboard (B delete, X space, Y clear, L1/R1 move the cursor, START done, SELECT cancel). A file browser that was open when an app started is reopened at the same folder and file when kennel returns.

## Files

| Path | Content |
|---|---|
| `$XDG_CONFIG_HOME/kennel/kennel.ini` (default `~/.config/kennel`) | settings |
| `$XDG_DATA_HOME/kennel/` (default `~/.local/share/kennel`) | `sessions.tsv`, `favorites`, `hidden` (entry ids hidden from Home), `boot`, `journal`, `battery.tsv`, `term_history` |
| `$XDG_CACHE_HOME/kennel/art/` (default `~/.cache/kennel`) | QOI thumbnails and backdrops, keyed by path, size and mtime |
| `$XDG_CACHE_HOME/kennel/titles.tsv` | e-book titles and authors, keyed by path, size and mtime; rewritten on catalog load with the books found |
| `$XDG_CACHE_HOME/kennel/wallpaper.qoi`, `wallpaper.key` | wallpaper scaled to the screen, keyed by path, size and mtime |
| `kennel-backup/` on the external card, else in HOME | backup: `config/` and `data/` copies of the two directories above, `backup.ini` (version, time, boot, uptime, session count) |
| `$XDG_RUNTIME_DIR/kennel/`, else `/dev/shm/kennel-UID`, else `/tmp/kennel-UID` | `state` (row and entry, open file browser), `kennel.log`, `kennel-read.log`, `kennel-term.log` |
| `$XDG_CONFIG_HOME/kennel/read.ini` | kennel-read settings |
| `$XDG_DATA_HOME/kennel/read/positions.tsv` | kennel-read reading positions and reading time, per book path |
| `$XDG_DATA_HOME/kennel/fonts/` | kennel-read user fonts, `<Family>-<Style>.ttf` |
| `$XDG_CONFIG_HOME/kennel/term.ini` | kennel-term settings (`font_size`) |
| next to the binary (symlinks in the CMake build directory), or `KENNEL_ASSETS` | `assets/` (fonts, reading fonts in `fonts/read`, icons, `term/shrc`), `platforms/`, `kennel-run` |

## Library: .puppy files

Read as Puppy does: the files in the platform's `sources`, in order (globs sorted); `[ENTRY]` keyed by `CATEGORY` and `NAME`, `[ARCHIVE]` keyed by `NAME`; a later definition replaces an earlier one in place. Keys used from Puppy: `CATEGORY`, `NAME`, `DESCRIPTION`, `COMMAND` (`%s` is the quoted ROM path), `ICON`, `TERMINAL`, `ENTRY_DIRECTORIES`, `ENTRY_EXTENSIONS`, `ENTRY_ICONS_DIRECTORIES`, `DEFAULT_ICON`. Additional keys, ignored by Puppy:

| Key | Section | Meaning |
|---|---|---|
| `ART` | `ENTRY` | card image, preferred over `ICON` |
| `ART_DIRECTORIES` | `ARCHIVE` | directories searched for `<stem>.png`, `.jpg`, `.jpeg` before `ENTRY_ICONS_DIRECTORIES` |
| `COLOR` | both | `#rrggbb` accent of generated cards and the category |
| `HIDDEN` | both | not shown on Home |

Images close to 3:2 and at least half the card size cover the card; portrait images (aspect below 0.85: book covers, portrait box art) get a poster card with the image at the left, the title beside it and the blurred image as background; smaller images are shown as an icon on a stock card. Archive files without their own image whose extension is `epub`, `fb2`, `mobi`, `azw`, `azw3` or `prc` show their embedded cover (EPUB cover-image, meta cover, guide or cover page; FB2 coverpage; MOBI EXTH 201), extracted on the art worker thread and cached in `~/.cache/kennel/art`. ROM titles drop trailing `(...)` and `[...]` groups, which are shown as tags. Archive files with these extensions use the title stored in the book (EPUB `dc:title`, FB2 `book-title`, MOBI full name or EXTH 503), with the author (EPUB `dc:creator`, FB2 first `author`, MOBI EXTH 100) as the first tag; books without a title keep the file name.

## Statistics

Every launch runs the command through `kennel-run JOURNAL ID COMMAND`, which appends a start record (boot id, uptime, wall time, entry id) and an end record (uptime, wall time, exit status) to `journal`. The command runs as an asynchronous child with the original stdin passed through fd 3 (terminal entries keep the console); the wrapper forwards SIGTERM, SIGINT and SIGHUP to it and waits until it is gone. On start kennel folds the journal into `sessions.tsv`:

| Case | Session |
|---|---|
| start and end record | complete, with exit status |
| no end record, same boot (wrapper killed: `appctl kill-application`) | ends at kennel's start, marked killed |
| no end record, earlier boot (power off in game) | counted, length unknown, not added to play time |

`sessions.tsv` columns: sequence, boot index, start wall time, start uptime, duration, exit status, flags (`k` killed, `u` unknown length), entry id. Durations use `CLOCK_BOOTTIME`; "last played" uses the wall clock when it is set (year 2024 or later), otherwise uptime within the same boot or the number of boots since. An empty ID (tools such as kennel-term) records nothing.

Battery history: kennel appends a sample to `battery.tsv` every minute and when the charging state changes; launches are prefixed with `KENNEL_BATTERY=<power_supply dir> KENNEL_BATTERY_LOG=<file>`, and `kennel-run` then appends a sample every minute while the app runs. Columns: boot id, uptime in seconds, capacity, first letter of the status (`C` charging, `D` discharging, `F` full, `N` not charging), voltage in mV. The file is cut to its last 512 KiB when it passes 1 MiB. The estimate is the least squares slope of the samples of the current charge or discharge within the last hour (at least 3 samples over 10 minutes), else the instantaneous current over the design capacity; rates under 0.5 % per hour count as unknown. The Battery tab of the statistics page shows the level, time left (or until full), the rate and a graph of the boot (up/down: earlier boots; gaps over 5 minutes stay empty).

## Platform profiles

`platforms/<id>.ini`, chosen by `ID` in `/etc/os-release` or by `KENNEL_PLATFORM` (an id or a path), searched in `<exe dir>/platforms` and `<exe dir>/../share/kennel/platforms`. Without a profile: built-in defaults (sources `~/.config/kennel/apps.puppy` and `~/.config/kennel/apps/*.puppy`, exec launch, windowed).

`[platform]`:

| Key | Meaning |
|---|---|
| `id`, `name` | identification |
| `launch` | `exec`: kennel releases the display, runs the app, waits and restarts itself; `command`: runs `launch_command` and expects to be stopped (goodluckOS appd) |
| `launch_command`, `terminal_flag` | `%c` the quoted command, `%t` `terminal_flag` for terminal entries |
| `stop_app`, `kill_app` | commands that end the running app |
| `poweroff`, `reboot`, `eject`, `external_mount` | system actions; empty hides the action |
| `backlight` | sysfs backlight directory (default: first of `/sys/class/backlight/*`) |
| `mixer_card`, `mixer_volume`, `mixer_output_switch` | ALSA card, volume control, speaker switch |
| `sources` | `.puppy` files and globs, `;`-separated, `~` is HOME |
| `hide_categories` | categories hidden until changed in Settings |
| `tail_categories` | categories placed after all others (system and tool categories) |
| `autolaunch` | file appd runs once at boot (Puppy format) |
| `app_log` | log of the last app |
| `system_icons` | directory with `<category>.png` images |
| `fallback_fonts` | fonts for glyphs missing in Inter (CJK) |
| `hud_env` | environment assignment for "Gallium HUD on next launch" |
| `launcher_script`, `previous_launcher` | launcher selection in Settings |
| `usb_mode`, `usb_mode_set`, `usb_modes` | USB mode in Settings and quick settings: `usb_mode` prints the current mode (run at start, when a host connects and when the setting opens without a known mode), `usb_mode_set` switches (`%m` the quoted mode), `usb_modes` lists `label:mode[:icon]` entries, `;`-separated; any of them empty hides the setting. The top bar shows the icon of the current mode, or the `usb` icon with the label when the entry has none |
| `contributors` | `;`-separated names listed from Settings > About > Platform |
| `usb_network` | network interface of the USB gadget (goodluckOS `usb0`); while its carrier is up and its received packet count has not grown since the host connected, the USB indicator is dim. The gadget reports carrier on enumeration, the packet count shows the host actually uses the link |
| `kernel_log` | command printing the kernel log |
| `mounts` | file systems shown in the status page |
| `width`, `height`, `fullscreen` | logical screen size, window mode |

`[logs]`: `Name=path` entries for the log viewer; `glob=pattern` adds matching files. `[status]`: `Name=command` entries whose output is shown in the status page.

## Licenses

| Component | License |
|---|---|
| Inter, JetBrains Mono (`assets/fonts`), reading fonts (`assets/fonts/read`, one `*-OFL.txt` or `*-LICENSE.txt` per family) | SIL Open Font License 1.1 |
| Lucide icons (`assets/icons`) | ISC |
| stb_image (`third_party/stb_image.h`) | public domain / MIT |
| QOI (`third_party/qoi.h`) | MIT |
| SDL3, SDL3_ttf, zlib | zlib |
