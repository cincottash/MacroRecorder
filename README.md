# MacroRecorder

Record what you do with the mouse and keyboard, edit it step by step, and play it back as many times as you like.
Free and open source (GPL-3.0). One file to download: nothing to install.

![MacroRecorder's main window](.github/screenshot.png)

## What it does

- **Records** mouse moves, clicks, double-clicks, drags, scrolling, key presses, shortcuts and typed text, plus
  which window you were working in.
- **Shows each step in a list** you can edit: change a position, a key or a pause, add steps by hand, reorder,
  copy and paste, disable steps, undo and redo.
- **Plays it back** exactly: clicks land on the same pixel, and the timing of your clicks, typing and pauses is
  kept. Play faster or slower, repeat a number of times, for a while, or until you stop it.
- **Steps you can add yourself:** click, move, scroll, button down/up (drags), key press or shortcut, text,
  wait, wait for a pixel color, wait for a key, bring a window to the front, run a program, show a
  notification, and **Repeat** / **Go to** a labeled step.
- **Global hotkeys** work from any program: F9 records or stops, F10 plays or pauses, Esc stops.
- **Safe to stop:** moving the mouse or pressing a key stops playback (you can turn this off), and held
  buttons or keys are always released.
- 38 color themes, including popular editor schemes (see the credits below).

## Download

Get the latest version from the [Releases page](https://github.com/cincottash/MacroRecorder/releases/latest):

| System | File |
|---|---|
| Windows 10 or 11 | `MacroRecorder.exe`: download and double-click. |
| Linux (x86-64) | `MacroRecorder-x86_64.AppImage`: download, make it executable (`chmod +x MacroRecorder-x86_64.AppImage`), run it. |

Windows may warn that the app is from an unknown publisher (it isn't code-signed). Choose **More info → Run
anyway**. Programs that watch the keyboard and mouse sometimes get flagged by antivirus tools for the same reason.

> **Linux status:** recording and playback on Linux (X11 and Wayland) are being built. The Linux version can open,
> edit and save macros now. The app detects your session type and tells you what's available.

## First run

The app asks where to keep its settings (theme, hotkeys, playback options, recent files). Pick any folder, or
choose **Don't keep app data** to start fresh every time and leave nothing behind. You can change this later in
**File ▸ Settings ▸ Change app data folder…**.

Your macros are saved wherever you choose when you save them.

## Using the app

### Record
1. Press **● Record** (or **F9**).
2. Do what you want to automate. Steps appear in the list as you go.
3. Press **Esc**, **F9** or **■ Stop recording**.

Clicks on the app's own window and the hotkeys themselves are never recorded.

### Play
Press **▶ Play** (or **F10**). With **Countdown before playing** on, you get a few seconds to switch to the right
window. **F10** pauses and resumes; **Esc** or **■ Stop** stops. Select some steps and press **▶ Play selection**
to play just those, or use **Macro ▸ Play from selected step**.

### The cards
Each card folds to its title with **Hide ▾** / **Show ▴**.

- **Playback**
  - **Speed**: 100 % plays at the recorded speed, 200 % twice as fast.
  - **Repeat**: a number of times, for a number of seconds, or until stopped.
  - **Mouse path**: *As recorded* follows your exact movement; *Straight line* and *Smooth curve* go directly
    to each point; *Human-like* wobbles and slows down near the target, for programs that notice robotic
    movement.
  - **Typing**: *As recorded* keeps your typing rhythm (some programs miss keys typed too fast); *Instant*
    types everything at once.
  - **Stop if I move the mouse or press a key**, and **Put the mouse back where it was afterwards**.
- **Recording**
  - **Mouse moves** on or off, and the smallest movement kept (in pixels).
  - **Keyboard** on or off, and **Merge typing into text** (typed characters become one Text step).
  - **Show pauses from**: pauses at least this long become their own *Wait* steps you can edit. Shorter
    pauses are kept with the next step (shown as "+120 ms before"), so timing stays exact.
- **Hotkeys**: click a box and press the key you want, or clear it. If another program already uses a key,
  the card says so.
- **Actions**: the steps. Double-click a step to edit it, or the **Label** / **Comment** column to type there.
  **Show:** hides kinds of steps from the list (hidden steps still play).
- **Activity**: what happened and when. **View ▸ Detailed activity log** also lists every step played.

### Repeating part of a macro
Give the first step of the part a **Label** (for example `start`), then add **Flow ▸ Repeat** after the part and
pick that label. "3 times" plays the part three times in total.

### Menus
- **File**: New, Open, Open recent, Save, Save as, Settings, Quit.
- **Macro**: Record, Play, Play selection, Play from selected step, Pause, Stop, Add action, Clean up
  (merges back-to-back waits and moves).
- **Edit**: Undo, Redo, Cut, Copy, Paste, Delete, Select all, Edit step, Enable / disable, Move up / down.
- **View**: Theme (hover over a theme to preview it), activity log, detailed log, Always on top, Reset view
  options.
- **Help**: project website, app data folder, report a problem, About.

## Command line

```
MacroRecorder.exe [file] [--play] [--repeat N] [--quit] [--minimized] [--data-folder FOLDER]
```

| Option | What it does |
|---|---|
| `file` | Opens this macro. |
| `--play` | Plays it right away (no countdown). |
| `--repeat N` | Plays it N times (with `--play`). |
| `--quit` | Closes the app when playback ends (with `--play`). Handy for Windows Task Scheduler. |
| `--minimized` | Starts minimized. |
| `--data-folder FOLDER` | Keeps settings in FOLDER for this run, for example to run from a USB stick. |
| `--screenshot FILE.png` | Saves a picture of the window and exits (used by the automatic build checks). |

Example: `MacroRecorder.exe "C:\path\to\daily-report.macro" --play --repeat 3 --quit`

## Macro files

Macros are saved as `.macro` files: plain JSON you can read, compare and fix in any text editor. Each step is
one object with a `type` (`mouse_click`, `text`, `wait`, …) and its values. Positions are screen pixels.

## Building from source

The app is C++20 with Qt 6 (6.5 or newer) and CMake.

**Windows** (with [MSYS2](https://www.msys2.org), UCRT64 environment):

```
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-ninja ^
          mingw-w64-ucrt-x86_64-pkgconf mingw-w64-ucrt-x86_64-qt6-base mingw-w64-ucrt-x86_64-qt6-svg
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

For the single self-contained exe, also install `mingw-w64-ucrt-x86_64-qt6-static`,
`mingw-w64-ucrt-x86_64-libtiff` and `mingw-w64-ucrt-x86_64-libwebp`, and configure with
`-DSTATIC_BUILD=ON -DCMAKE_PREFIX_PATH=/ucrt64/qt6-static`.

**Linux:** install Qt 6 (with the SVG module), CMake and Ninja, then run the same two `cmake` commands.

### Changing the icon
Edit `resources/icon.svg`, then rebuild `packaging/icon.ico` with the small tool in `packaging/make_icon.cpp`
(build it against Qt Gui and Qt Svg, then run `make_icon resources/icon.svg packaging/icon.ico`).

## Automatic releases

GitHub Actions (`.github/workflows/build-apps.yml`) builds the Windows exe and the Linux AppImage and checks that
both start.

- **Pull requests:** the built files are kept with the run for 14 days (open the run, then **Artifacts**).
- **Every merge into `main`:** a new release is published with both files, numbered automatically
  (v0.1.1 → v0.1.2). Put `[minor]` or `[major]` in the pull request's **title** for v0.2.0 or v1.0.0.

## Code layout

| Folder | What's there |
|---|---|
| `src/core` | Macro steps and file format (`Action`), recording logic (`Recorder`), playback (`Player`), mouse paths (`Paths`). No operating-system code. |
| `src/platform` | The operating-system layer: `win/` (hooks, input, hotkeys, windows), `linux/` (X11 / Wayland, in progress). |
| `src/app` | The window, cards, menus, step editor, themes, settings and app data folder. |
| `resources` | Icon and small images used by the themes. |
| `packaging` | Windows icon and version info, the icon tool, the Linux desktop entry. |

## License and credits

Copyright © 2026 cincottash. Licensed under the [GNU General Public License v3.0](LICENSE).

Built with [Qt](https://www.qt.io) (LGPL-3.0).

The Dark themes and Light themes use colors from these open-source editor color schemes, adjusted for
readability as app themes: Nord, Dracula, Solarized, Gruvbox, One Dark / One Light, Tokyo Night, Catppuccin
(Mocha, Macchiato, Frappé, Latte), Rosé Pine (main, Moon, Dawn), Everforest, Kanagawa (Wave, Lotus), Ayu (Dark,
Mirage, Light), Palenight, Night Owl, Cobalt2, Synthwave '84, Tomorrow (Night and Day), Monokai and Zenburn.
Thanks to their authors.
