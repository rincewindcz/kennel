# kennel: design

Launcher for Linux handhelds, first target goodluckOS on the GA36-MB A23 unit (640x480, Mali-400 with Mesa lima, 494 MB RAM, dual Cortex-A7 at 1008 MHz). Developed as a standalone project; goodluckOS support is a platform profile and Puppy compatibility, so a different base system needs a new profile only. Usage and file reference: [REFERENCE.md](REFERENCE.md).

## 1. Goals

| Goal | Measure |
|---|---|
| Fast return from a game | first frame about 400 ms after the supervisor starts the launcher (section 11.1) |
| Large art, few items on screen | focused card 300x200, one neighbour visible on each side |
| Smooth, simple motion | fade, translate, scale only; vsync while animating, no redraw when idle |
| Drop-in for Puppy on goodluckOS | same `.puppy` files and precedence, same `~/autolaunch` format, launches through `appctl` |
| No system changes | installs into HOME, started from `~/.local/launcher.sh`, falls back to Puppy |
| Portable | device specifics in `platforms/<id>.ini`; only glibc, SDL3, SDL3_ttf and ALSA as shared dependencies |
| Expert mode | status, logs, processes, library diagnostics without leaving the launcher |

Non-goals: scraping (the device has no network), video previews, touch input.

## 2. Technology

| Item | Choice | Reason |
|---|---|---|
| Language | C++20; clang cross build against Debian trixie armhf | `std::span`, designated initializers; trixie libraries are older than or equal to the goodluckOS ones |
| Video, input, threads | SDL3 renderer on KMSDRM (`opengles2` on lima) | on the device; its SDL2 is `sdl2-compat` over SDL3 |
| Text | SDL3_ttf rasterizes each string once (`TTF_RenderText_Blended`, white) into its own texture, cached and tinted at draw time; unused for 120 frames and over 400 entries: evicted | the SDL3_ttf renderer text engine (`TTF_Text`) cost about 6 ms per text object on Mali-400 (atlas updates stall the GPU), a small new texture about 1 ms; Puppy re-rasterizes every string on every redraw |
| Images | stb_image (PNG, JPEG, BMP) on a worker thread; QOI disk cache | no SDL3_image dependency; the device's SDL3_image build differs from Debian's |
| Shapes | 9-grid textures with analytic anti-aliased masks (rounded rects, rings, soft shadows; shadow blur rounded to 4 px steps so few masks exist), vertex-colour gradients, background glow from one 64x64 radial texture | no extra library, sharp at any size; a large shadow mask cost about 40 ms to build on the device |
| Audio mixer | libasound simple mixer (optional, `KENNEL_ALSA`) | volume, mute, speaker switch |
| Icons | Lucide (ISC) SVGs rasterized to white PNGs at 20 and 32 px by `scripts/icons.sh`, tinted at draw time | no SVG code on the device |
| Fonts | Inter Regular, SemiBold, Bold; JetBrains Mono for logs; platform fallback fonts for CJK; kennel-read: Literata, Inter, Atkinson in Regular, Italic, Bold, BoldItalic (`assets/fonts/read`), nine more in the optional `kennel-fonts.zip`, user fonts in `DATA/fonts` | the device has no monospace font; reading fonts are static styles, subset to Latin, Greek, Cyrillic and punctuation with layout tables dropped by `scripts/fonts.sh` (from google/fonts, variable fonts instanced; Literata and Inter subset in place); the core set keeps the package small |
| Books | zlib for EPUB (zip, deflate); own tolerant markup tokenizer for XHTML, OPF, NCX, FB2 and MOBI HTML | zlib is on the device; no XML library needed for broken markup |
| Config, data | INI (`kennel.ini`), TSV (`sessions.tsv`), plain lists (`favorites`) | readable and editable over MTP |

The cross build (`scripts/build.sh armhf`) links libstdc++ statically and checks every undefined dynamic symbol against the libraries of the goodluckOS rootfs.

## 3. Platform integration

`platforms/<id>.ini`, selected by `ID` in `/etc/os-release` or `KENNEL_PLATFORM` (an id, or a path containing `/`), searched in `<exe dir>/platforms` and `<exe dir>/../share/kennel/platforms`. Keys: REFERENCE.md, section "Platform profiles". `tail_categories` places system and tool categories (goodluckOS: Emulators/Frontends, Terminal, System) after the game categories.

Launch modes:

| Mode | Sequence |
|---|---|
| `command` (goodluckOS) | save state, run `launch_command` (`doas appctl launch-application %c %t`); appd kills the launcher cgroup (SIGKILL through `cgroup.kill`, no clean-up possible), runs the app as `player`, and starts the launcher again when it exits. If kennel is still alive after 8 s, it reports the failure. |
| `exec` (generic) | save state, shut down the art worker, renderer and SDL (releases DRM master), fork and run the command, wait, then `execv` itself; the new process restores the state like after an appd restart |

Both modes run the command through `kennel-run`, so statistics work the same way. appd restarts a launcher that dies within 5 s after 1 s, forever; `tools/launcher-glos.sh` therefore falls back to `/usr/bin/puppy` on a non-zero exit.

## 4. Screen layout (640x480)

```
+----------------------------------------------------------------+  0
| 14:32 (or uptime)  Title      [load free]  [TF][USB][vol][bat] |  top bar 32
+----------------------------------------------------------------+
| [icon] Super Nintendo                      Favorites ^          |  header: name, count,
|        124 items . Super Nintendo          Game Boy v           |  neighbouring rows
|                                                                |
|  +-------+   +--------------------------+   +-------+          |  carousel, centre y 206
|  |204x136|   |   300x200 focused, ring  |   |       |          |  side: scale 0.68,
|  +-------+   +--------------------------+   +-------+          |  alpha 0.55
|                 ======o=========                               |  position bar
|                    Super Metroid                               |  title 26 px
|        3 h 12 min . 2 days ago . 14 sessions                   |  stats line
|                    USA, Europe                                 |  tags or category
+----------------------------------------------------------------+ 444
| (A) Play (X) Details (Y) Favorite (START) Quick  [dock icons]   |  bottom bar 36
+----------------------------------------------------------------+ 480
```

Background: vertical gradient with an accent glow; with "Background: Game art", the selected entry's backdrop (128x96 cover, box-blurred, scaled to the screen at 55 % over a dark gradient), changed 180 ms after the selection settles and crossfaded in 350 ms.

### 4.1 Top bar

Each indicator is an icon with an optional value; left: time and screen title, right to left: battery, volume, free memory, CPU, TF card, USB. Settings > Top bar turns each one on or off.

| Indicator | Source | Shown |
|---|---|---|
| Clock or uptime | `CLOCK_REALTIME` when after 2024-01-01, else `CLOCK_BOOTTIME`; setting Auto, Clock, Uptime, Off | always |
| Screen title | top screen's `title()` | outside Home, when there is room |
| Battery %, charging | first `/sys/class/power_supply/*` of type `Battery`: `capacity`, `status`; chargers of type `Mains`/`USB`: `online` | amber at 20 %, red at 10 % |
| Volume, output | ALSA volume control, mute switch, speaker switch | headphone glyph when the speaker is off |
| Free memory | `MemAvailable`, as `438M` or `1.2G` | amber under 10 % of `MemTotal` |
| CPU | busy share of all cores from `/proc/stat` between two polls | amber from 85 % |
| TF card | `external_mount` in `/proc/mounts` | when mounted |
| USB | `/sys/class/udc/*/state` = `configured` | when connected to a PC |

Values are polled every second; a redraw happens only when a shown value changes.

### 4.2 Bottom bar and dock

Left: hints of the top screen, with button glyphs from `SDL_GetGamepadButtonLabel`. Right, on Home: the dock, focused with SELECT, moved with left/right, A opens, B, SELECT or up leave. The focused item opens into a pill with its label.

| Item | Action | Confirmation |
|---|---|---|
| Settings | settings page | no |
| Expert | expert page (expert mode only) | no |
| Terminal | kennel-term (expert mode only) | no |
| Files | file browser in HOME | no |
| Statistics | statistics page | no |
| Screen off | backlight to 0; the next button press restores it and is swallowed | no |
| Eject card | `eject` command (shown while mounted), then library reload | yes |
| Restart | `reboot` command | yes, Cancel preselected |
| Power off | `poweroff` command | yes, Cancel preselected |

## 5. Navigation

| Input | Home | Lists and panels |
|---|---|---|
| left/right, left stick | previous/next card; a row of three or more is circular: the neighbours of an end card are drawn from the other end and a fresh press at an end scrolls on to it; held: 350 ms delay, 60 ms rate, 30 ms after 2 s | change value; backlight levels: d-pad one level, stick a tenth of the range |
| up/down | previous/next category, slide and fade | move, wrapping on a fresh press |
| L1 / R1 | 5 cards back/forward | page |
| L2 / R2 | start of the previous/next initial letter, shown in a toast | |
| A | launch | select |
| B | first card; on the first card, first row | back |
| X | details | |
| Y | favorite toggle | |
| START | quick settings | close quick settings |
| SELECT | dock | |

The stick uses hysteresis (press at half travel, release at a quarter) so a stick springing back produces no stray move. "Confirm button: East" swaps A and B.

## 6. Library

Rows on Home: Continue (last 20 played), Favorites (by title), then the categories in the user order (Settings > Categories, X/Y move), minus hidden ones (platform `hide_categories` until the user changes the list). Empty rows are not shown. Sort within categories: Name (file order for entries, title order for archives, then file name), Last played, Play time.

Parser: as Puppy (REFERENCE.md, "Library: .puppy files"), plus diagnostics with file and line for ignored sections and replaced definitions, shown in expert mode.

E-book titles: read from the book on the catalog load (EPUB package document, the first 64 KiB of FB2, MOBI record 0) and cached in `titles.tsv` by path, size and mtime; a load with every title cached reads no book.

Art lookup per entry: `ART`, `ART_DIRECTORIES` and `ENTRY_ICONS_DIRECTORIES` (each directory listed once into a stem index, png before jpg before jpeg, exact stem before case-insensitive), `ICON`. Archive entries without an image get a stock card over `DEFAULT_ICON`; category images also come from `system_icons/<category>.png`.

### 6.1 Art cache

| Item | Value |
|---|---|
| Card | 300x200, 14 px rounded corners baked into the alpha channel |
| Cover mode | image at least half the card size and within 35 % of 3:2: area-average downscale (premultiplied) and centre crop |
| Icon mode | other images: fitted into 220x124, at most 2x, drawn centred on a stock card above the title |
| Stock card | category image with a dark scrim, or a diagonal gradient of the category colour (hash of the name, or `COLOR`), title in 26 px, tags above it; composed into a render target (premultiplied alpha) |
| Backdrop | 128x96 cover, three 7 px box blur passes |
| Disk cache | `~/.cache/kennel/art/<fnv1a(path, size, mtime, card size)>.{c,i,b}.qoi` |
| Worker | one thread; each frame's requests replace the queue, so entries scrolled past are not decoded; a finished job wakes the main loop with `SDL_EVENT_USER` |
| Main thread | at most 3 uploads and 3 stock compositions per frame; 64 textures, least recently used evicted; images fade in over 220 ms |

Requested per frame: visible cards (focused first in draw order), the next and previous 3 as prefetch, the backdrop of the settled selection.

## 7. Launch and statistics

Launch: overlay with the card and title fades in (180 ms), then the platform launch runs (section 3). The command is wrapped as

```
<exe dir>/kennel-run <data>/journal <id> <command>
```

`kennel-run` (POSIX sh, written for busybox ash):

1. appends `S token boot_id uptime wall id`;
2. starts `/bin/sh -c command` as an asynchronous child; an asynchronous command gets `/dev/null` as stdin unless redirected, so the original stdin is passed through fd 3 and terminal apps keep the console;
3. traps SIGTERM, SIGINT and SIGHUP and forwards SIGTERM to the child (`appctl stop-application` signals the whole cgroup: the app ends, the wrapper survives), and loops on `wait` until the child is gone;
4. clears the traps and reads `/proc/uptime` with `cat`: the busybox ash `read` builtin spins at 100 % CPU while a caught signal is pending;
5. appends `E token uptime wall status`.

Autolaunch entries written by kennel use the same wrapper, so sessions started at boot are counted too.

On start, kennel increments its boot counter when `/proc/sys/kernel/random/boot_id` changed, then folds the journal into `sessions.tsv` (cases in REFERENCE.md, section "Statistics") and truncates it. Entry statistics (launches, total time, first wall time, last session) are recomputed from `sessions.tsv` on load; resetting an entry rewrites the file without its sessions.

After a return, a session that ended with a status other than 0, 130 (SIGINT) or 143 (SIGTERM, user stop), and was not killed, opens a dialog with the exit code or signal and a link to the app log ("Report app errors" setting).

The goodluckOS clock starts at 1970 on every boot (no battery-backed RTC), so "last played" falls back to uptime within the boot, then to "previous boot" or "N boots ago".

### 7.1 Details (X)

Card, statistics (play time, sessions, last played, last session length, last exit, first played when the clock was set), title, category and tags, description. Actions: Play, Favorite, Hide from Home (entry id in `hidden`; Settings > Hidden entries lists hidden entries by category with a toggle that shows them again), Start at boot (writes `autolaunch` as `# category<TAB>name` plus the wrapped command, the Puppy format), Last app log, Reset statistics (confirmation). Expert mode adds Run in terminal and Entry internals (id, source file and line, file, art paths, command, wrapped command).

### 7.2 Statistics page

Tabs switched with L1/R1 (also left/right), changed with a 16 px slide and fade:

| Tab | Content |
|---|---|
| Overview | tiles: total play time, sessions, played entries, longest session; play time of this boot; the 4 most recent entries with last played and length |
| Most played | entries by total time (then sessions), rank, bar relative to the maximum, time and sessions |
| Categories | play time per category with bars, sessions and entries played; sessions of entries no longer in the catalog under "Removed entries" |
| Recent | up to 200 sessions, newest first: title, last played, length (or unknown), exit (killed, code) |
| Battery | tiles: level and status, time left or until full, rate per hour (last hour or current); graph of the boot's samples, 0 to 100 %, x from the first sample over at least an hour of uptime, 2 px columns, green while charging; up/down: earlier boots |

List tabs: up/down (wrapping on a fresh press), L2/R2 page, A opens Details of entries still in the catalog. The header shows the total time and session count.

## 8. Quick settings (START)

Panel from the right over dimmed Home: battery (level, estimated time left or until full), brightness (backlight levels 1 to max, sysfs, writable by group `video` on goodluckOS, persisted by `S90backlight`), volume, mute, output Speaker/Headphones, then Files, Statistics, Settings, Expert and Terminal (expert mode), Screen off. The same side panel (`MenuPanel`) lists the file browser's actions.

## 9. Settings

`~/.config/kennel/kennel.ini`, section `[kennel]`:

| Setting | Key | Values |
|---|---|---|
| Accent colour | `accent`, `accent_custom` | Blue, Teal, Amber, Rose, Violet, Green, Crimson, Custom (index); `#rrggbb` for Custom, edited with R, G and B sliders shown under the choice |
| Background | `background`, `wallpaper` | `gradient`, `art`, `wallpaper`; image path for `wallpaper`, set from the file browser (image actions, image viewer X); decoded once, scaled to cover the screen and cached as `wallpaper.qoi`; drawn under a dark gradient (0.45 to 0.8 on Home, 0.7 to 0.9 on pages) |
| Animations | `animations` | `normal`, `fast` (half duration), `off` |
| Top bar: time | `time` | `auto`, `clock`, `uptime`, `off` |
| Top bar: CPU load, free memory, volume, battery, external card, USB | `bar_cpu`, `bar_memory`, `bar_volume`, `bar_battery`, `bar_card`, `bar_usb` | 0, 1 (default 1) |
| Sort games by | `sort` | `name`, `last_played`, `play_time` |
| Continue row, Favorites row | `show_continue`, `show_favorites` | 0, 1 |
| Categories | `hidden`, `order` | `;`-separated names |
| Confirm button | `swap_confirm` | 0 south, 1 east |
| Low battery warning | `low_battery` | 0, 10, 15, 20, 30 (toast at the level and at 5 %) |
| Screen off after | `screen_off_after` | seconds without a button press until the backlight goes to 0 (as the dock's Screen off), 0 never (default); the menu offers Never, 1, 2, 5, 10, 30 min and is shown only with a backlight. Only time with the screen on counts: the timer restarts while another program (goodluckOS `toggle-screen.sh` on the power button) holds the backlight at 0, and after kennel was stopped (a poll more than 2 s late). A screen kennel turned off and another program turned on again counts as on |
| Report app errors | `report_errors` | 0, 1 |
| Expert mode | `expert` | 0, 1 |
| Performance overlay (Expert page) | `perf_overlay` | 0, 1 |

Settings also offers Backup (section 17), Rescan library, About (version, platform, renderer, gamepad, start-up and library load times, paths, licenses) and, when the profile defines `launcher_script` and `previous_launcher`, Launcher at boot (rewrites the launcher script).

## 10. Expert mode

Menu: INSPECT (Status, Logs, Processes, Library, Sessions), TOOLS (Terminal, Files from `/`, Performance overlay, HUD for next launch), ACTIONS (Restart kennel, Switch to `previous_launcher`, Sync filesystems).

| Page | Content | Source |
|---|---|---|
| Status | text page reloaded every second, sections SYSTEM (hostname, kernel, uptime, load, CPUs and `scaling_cur_freq`, RAM, swap, zram), POWER (capacity, status, health, voltage, current, design capacity, estimated time left or until full with rate and source, battery history size and path, external power), STORAGE (each of `mounts`), DISPLAY (renderer, logical size, refresh, frame time), GPU (`devfreq` frequencies), USB (UDC states), INPUT (gamepad), KENNEL (version, platform, start-up, library, art textures, text cache), then one section per `[status]` command | `/proc`, `uname`, `statvfs`, `/sys/class/{power_supply,udc,devfreq}` |
| Logs | menu of `[logs]` entries and `glob=` matches (with sizes), kennel's own log, Kernel (`kernel_log` output, reloaded every 2 s); a log opens as the last 256 KiB of the file, reloaded every second, following the end | files |
| Processes | PID, user, CPU % between samples, RSS, command line; sampled on open, after 0.5 s, then every 2 s; A on a row: Terminate (SIGTERM) or Kill (SIGKILL), result as a toast | `/proc/<pid>` |
| Library | Report (sources with entry and archive counts, categories with counts and images, parser messages with file and line, entries without own art per category, absolute command paths that do not exist, load time) and Rescan library | catalog |
| Sessions | up to 500 sessions, newest first: sequence, boot, length, exit, last played, title | statistics |
| Performance overlay | frame time, refresh, art textures, cached texts above the bottom bar (saved setting) | |
| HUD for next launch | prefixes the next launched command with `export <hud_env>;` (goodluckOS `GALLIUM_HUD_VISIBLE=true`); not saved | |
| Restart kennel | saves state, shuts down, `execv` itself | |
| Switch to `previous_launcher` | confirmation, then exec in place (stays in the launcher cgroup) | |

Text pages (logs, reports) use JetBrains Mono, strip ANSI escapes and control characters, expand tabs, and wrap by character count. Keys: up/down line, L1/R1 page, L2/R2 top/bottom, Y wrap on/off (off: left/right scroll 8 columns, UTF-8 safe), X follow on/off on reloading pages, B back.

Root operations are limited to what the platform permits (goodluckOS `doas.conf`: `poweroff`, `reboot`, `evsieve`, `appctl`, `umount`). On goodluckOS `dmesg_restrict` is 0, so the kernel log is readable as `player`.

## 11. Rendering and animation

Screen stack: Home, Details, Quick settings, Dialog, menu pages, text pages, Processes, Statistics. Every screen has a `shown` tween (0 to 1 in 200 ms on open, back to 0 in 160 ms on close); overlays (Quick settings, Dialog) draw over the screen below, and while an opaque screen is still opening the one below is drawn too, which gives a crossfade. A screen is removed when its close finishes; the one below then gets `resumed()` (Home rebuilds its rows).

### 11.1 Start-up

Measured on the device (goodluckOS, 2026-10-21) from process start:

| Step | Time |
|---|---|
| `SDL_Init` (video, events) | 41 ms |
| window and GLES2 renderer (KMSDRM, EGL on lima) | 175 ms |
| fonts | 22 ms |
| catalog (32 entries, 12 files) | 5 ms |
| ready (state restored) | about 250 ms |
| first frame presented | about 400 ms (masks, icons, stock cards, text of the first screen) |

The gamepad subsystem is initialized after the first frame (its device scan delayed the first frame by about 130 ms); `SDL_HINT_JOYSTICK_HIDAPI` is 0, since the GA36-MB pad is a plain evdev device. The first frame and its cost breakdown are written to the log.

### 11.2 Main loop

Tweens (`anim::Tween`) animate floats with `outCubic` or `inOutQuad`; durations are multiplied by the animation speed (0 jumps). Any tween still moving calls `anim::keepAlive()`; the main loop renders with vsync while that happens and otherwise waits in `waitEvent` (`input.cpp`) until input, a key repeat, a worker result or the next 1 s status poll. SDL3 has no event wait on KMSDRM (no `WaitEventTimeout` in the video backend), so `SDL_WaitEvent*` polls every 1 ms there: idle kennel woke about 730 times per second and used 12 % of one core on the GA36-MB. `waitEvent` polls every 10 ms on KMSDRM (100 ms in kennel while the screen is off) and calls `SDL_WaitEventTimeout` on other video drivers; kennel-read and kennel-term use it too.

| Motion | Properties | Duration |
|---|---|---|
| card change | carousel position, scale 0.68 to 1, alpha 0.55 to 1, focus ring | 220 ms |
| category change | rows and headers slide 46 px and crossfade | 240 ms |
| info line | fade, 6 px rise | 180 ms |
| page open/close | alpha, 24 px slide | 200 / 160 ms |
| quick settings | slide from the right, dim | 200 ms |
| dialog | scale 0.94 to 1, alpha, dim | 200 ms |
| image loaded, backdrop change | crossfade | 220 / 350 ms |
| toast | 12 px drop, alpha; 3.2 s visible | 200 ms |
| launch | dim, card scale 0.92 to 1 | 180 ms |

`KENNEL_AUTOMATE` (REFERENCE.md, "Testing") feeds logical keys and screenshot requests into the main loop for offscreen host runs.

## 12. Source layout

```
CMakeLists.txt, cmake/armhf-linux.cmake
src/                       kennelcore (static library shared with kennel-read and kennel-term): anim, util,
                           ini, paths, platform, settings, image, gfx, input, menu, glyphs, osk, zip,
                           tokenizer, cover
  main.cpp                 entry point
  app.*                    screen stack, main loop, bars, dock, toasts, launch, system actions
  anim.h                   frame clock, tweens
  gfx.*                    renderer, shapes, text cache, icons, theme
  input.*                  gamepad and keyboard to logical keys, repeat, stick
  util.*, ini.*            strings, files, time, INI parser
  paths.*, platform.*, settings.*
  catalog.*                .puppy parser, categories, art lookup
  stats.*                  journal, sessions, favorites
  image.*, art.*           decoding, scaling, QOI cache, worker, stock cards
  system.*, procs.*        battery, backlight, mixer, mounts, USB; /proc sampler
  menu.*                   menu widget, menu items, scrollbar, accent colour
  glyphs.*                 glyph atlas text engine (reader and terminal text)
  osk.*                    on-screen keyboard
  widgets.*                menu page, dialog, text input, background, page header
  battery.*                battery history, estimate
  backup.*                 backup and restore of settings and data
  files.*                  file browser, image viewer
  home.*, details.*, panels.*, menus.cpp, expert.*, statsview.cpp
  read/                    kennel-read: book model (book.*), format loaders (txt, epub, html, fb2, pdb),
                           text layout, reader screens
  term/                    kennel-term: escape sequence parser and screen (vt.*), pseudo terminal (pty.*),
                           suggestions (complete.*), terminal screen and modes (term.*)
third_party/               stb_image.h, qoi.h
assets/                    fonts (reading fonts in fonts/read), icons (svg sources, 20 and 32 px PNGs),
                           term/shrc (kennel-term prompt)
platforms/                 goodluckos.ini, goodluckos/{books,terminal}.puppy (Books and Terminal entries)
tools/                     kennel-run, launcher-glos.sh, padinject.c (device test input)
scripts/                   sysroot.sh, build.sh, icons.sh, fonts.sh, testroot.sh, stage-glos.sh, deploy-glos.sh,
                           testbooks.sh, mkbooks.py (FB2, PalmDOC and MOBI test books)
```

## 13. Device facts (goodluckOS, checked 2026-10-21)

| Item | Value |
|---|---|
| Power supplies | `axp20x-battery` (`capacity`, `status`, `voltage_now`, `current_now`, `charge_full_design` 3 Ah), `axp20x-usb` |
| Backlight | `/sys/class/backlight/backlight` |
| Clock | 1970 after every boot (no RTC persistence) |
| Kernel log | `dmesg_restrict` 0 |
| UDC | `musb-hdrc.2.auto`, `configured` with a PC attached |
| Launcher command | `/home/player/.local/launcher.sh`, run by appd as `player` via `su -l` |

## 14. kennel-read

E-book reader built from the same repository (`kennel-read`), sharing kennelcore: graphics helpers, input, menus and `kennel.ini` (accent colour, confirm button). Goals: one consistent user-set font, scrolling by lines, no images and no effects, progress, the common text formats.

### 14.1 Book model

`src/read/book.h`: a book is a list of paragraphs (`Body`, `Heading`, `Break`) of UTF-8 text with single spaces and optional style runs (bold, italic, byte ranges), plus chapters (title, first paragraph), title, author and format. `Book::finish()` normalizes whitespace, drops empty paragraphs, collapses repeated breaks, falls back to headings when a format has no table of contents, and computes the byte offset of every paragraph. A reading position is a byte offset into the concatenated text, so it survives changes of font, size, margins and alignment.

| Format | Source of structure |
|---|---|
| TXT | UTF-8 only (a BOM is skipped); paragraphs are blank-line separated blocks when the file has them, otherwise lines |
| EPUB | `META-INF/container.xml`, OPF spine and metadata, EPUB 3 nav or NCX table of contents; XHTML reduced to paragraphs |
| HTML, XHTML | block elements become paragraphs, `h1` to `h6` headings, `i`/`em` and `b`/`strong` style runs; `script`, `style`, images dropped |
| FB2 | `title-info` metadata, `section`/`title` chapters, `emphasis`/`strong`, `empty-line` breaks; `binary` skipped |
| PDB, MOBI | PDB records; PalmDOC (TEXtREAd) text, or MOBI (BOOKMOBI) HTML without DRM, PalmDOC-compressed or uncompressed, trailing record entries removed |

### 14.2 Reading

Text is laid out in lines on a uniform grid (line height times line spacing), so scrolling moves by whole lines: Up/Down one line (or one screen with "Up/Down: Full page") with the shared key repeat (350 ms delay, 60 ms, 30 ms after 2 s), Left/Right and L1/R1 one screen, L2/R2 to the previous or next chapter, SELECT the chapter list, START the settings panel, B saves and exits. Nothing animates. A thin status bar shows the chapter, the page and page count, the progress in percent, the reading time and the battery, each switchable. The page count needs the whole book laid out: the main loop lays out paragraphs in 20 ms slices while idle (`Layout::buildAll`) and shows the count once done. Reading time adds the gap between button presses, capped at 3 minutes; the status bar refreshes every 30 s for it and the battery.

| Setting | Values |
|---|---|
| Font | families found in `DATA/fonts` and `assets/fonts/read`: Literata, Inter, Atkinson; with `kennel-fonts.zip` also Alegreya, Bitter, Charis SIL, Crimson Pro, EB Garamond, Lora, Merriweather, PT Serif, Source Serif |
| Size | 14 to 36 |
| Line spacing | 1.0 to 2.0 |
| Margins | narrow, normal, wide |
| Alignment | left, justified |
| Formatting | plain (one style), styled (bold headings, italic and bold runs) |
| Paragraphs | first-line indent, spacing (blank line) |
| Theme | dark, sepia, light |
| Up/Down | line by line, full page |
| Status bar | on, off; chapter, pages, progress, reading time, battery each on or off |
| Brightness | platform backlight |

Files: `~/.config/kennel/read.ini` (settings), `~/.local/share/kennel/read/positions.tsv` (path, offset, size, wall time, reading seconds per book).

### 14.3 goodluckOS

`platforms/goodluckos/books.puppy` is installed to `~/.local/share/applications/`: an `[ARCHIVE]` named Books over `~/books` and `/media/external/books` with the extensions above, opening `ports/kennel/kennel-read %s`. kennel shows the books as the Books category (glyph `book-open`), records reading time like play time, and lists recently read books in Continue.

Test books: `scripts/testbooks.sh` (Project Gutenberg EPUB, TXT and HTML, including a Czech book; FB2, PalmDOC PDB and MOBI generated by `scripts/mkbooks.py`).

## 15. kennel-term

Separate binary, like kennel-read: launched through `launch_command`, so on goodluckOS it runs as `player` in the app cgroup and kennel restarts when it exits. kennel launches it with the id `tool:terminal`, which kennel-run receives as an empty id: no play session.

| Part | Implementation |
|---|---|
| Pseudo terminal | `posix_openpt`, child with `setsid`, `TIOCSCTTY`, `TERM=xterm-256color`, `COLORTERM=truecolor`, `ENV` (prompt), login shell or `sh -c COMMAND`; a reader thread buffers output and pushes one `SDL_EVENT_USER` per batch; `TIOCSWINSZ` on every layout change (keyboard shown or hidden, font size) |
| Parser | state machine: ground with UTF-8 decoding, escape, charset designation, CSI with private markers and intermediates, OSC (title), DCS/SOS/PM/APC ignored; C0 controls also inside sequences; replies (DSR, DA) written back to the terminal |
| Screen | main and alternate grids of cells (code point, fg, bg, attributes), scroll region, saved cursor per screen, pending wrap, insert mode, 2000 lines of scrollback from the main screen; on a smaller size the lines above the cursor move into the scrollback |
| Drawing | background runs per row with `gfx.fill`, glyphs through the `Glyphs` atlas (JetBrains Mono, bold synthesized), cell width the advance of `M`, cell height the line skip; cursor as an accent block with the glyph in the background colour; redraw only on output or input |
| Input | `Input::repeatAll` makes face buttons and triggers repeat while held (typing); the right stick is read directly for scrollback |
| Suggestions | line typed at the prompt tracked by kennel-term while `tcgetpgrp` is the shell; history in `term_history` (500 unique lines, ranked use counts per first word), commands scanned from `PATH` once at start |

Measured on the device: 13 px font, 8x18 px cells, 80x13 with the keyboard and 80x25 in keys mode.

## 16. File browser

`FileScreen` on kennel's screen stack. Listing with `opendir`/`stat` (links followed, flagged), sorted folders first. Opening goes through `Catalog::entriesForFile`, which reuses the `[ARCHIVE]` rules recorded while loading the catalog (extensions, expanded directories, command template); the launched entry has the id `rom:<path>`, so statistics of files outside the library directories are kept per path. Long operations (`cp -r`, `mv` across file systems, `rm -rf`) run on a thread with a modal panel; rename, same file system moves, single file deletes and `mkdir` are direct system calls. `App::saveState` appends `files<TAB>dir<TAB>name` for an open browser; start-up reopens it.

## 17. Backup

Settings > Backup. Location: `<external_mount>/kennel-backup` while the card is mounted (goodluckOS mounts it with `uid=1000`, writable by `player`), else `HOME/kennel-backup`. Back up now copies the config and data directories into `kennel-backup.tmp` with `cp -r`, writes `backup.ini` (version, wall time, boot id, uptime, session count), replaces `kennel-backup` with it and syncs. Restore (confirmation) replaces both directories with the backup's copies, syncs and restarts kennel (`App::restart`, `execv`). The art and wallpaper caches are rebuilt on demand and not backed up. "Last backup" shows the date when the clock was set at backup time and now, else the age within the same boot or "an earlier boot".
