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
  wait, wait for a pixel color, wait for a key, wait for a file, bring a window to the front, run a program,
  run another macro, show a notification or a message box, beep, and **Repeat** / **Go to** a labeled step.
- **Global hotkeys** work from any program: F9 records or stops, F10 plays or pauses, Esc stops, and more you
  can set (new recording, play from the selected step, play the selection, capture a position).
- **Safe to stop:** moving the mouse or pressing a key stops playback (each can be turned off), held buttons
  or keys are always released, and on Windows you can block the keyboard while a macro plays.
- **When Finished:** close the app, lock the computer, sleep, log off, restart or shut down (after a 30-second
  countdown you can cancel).
- 38 color themes, including popular editor schemes (see the credits below).

## Download

Get the latest version from the [Releases page](https://github.com/cincottash/MacroRecorder/releases/latest):

| System | File |
|---|---|
| Windows 10 or 11 | `MacroRecorder.exe`: download and double-click. |
| Linux (x86-64) | `MacroRecorder-x86_64.AppImage`: download, make it executable (`chmod +x MacroRecorder-x86_64.AppImage`), run it. |

Windows may warn that the app is from an unknown publisher (it isn't code-signed). Choose **More info → Run
anyway**. Programs that watch the keyboard and mouse sometimes get flagged by antivirus tools for the same reason.

On Linux the app checks your session when it starts and uses the best method available; the Activity log says
which one. See [Linux setup](#linux-setup).

## Linux setup

| Session | Recording | Playback |
|---|---|---|
| **X11** ("Xorg" at the login screen) | every program | every program |
| **Wayland**, with access to `/dev/uinput` | programs running through XWayland | every program |
| **Wayland**, without that access | programs running through XWayland | programs running through XWayland |

Wayland doesn't let one program watch the mouse and keyboard of others, so **for recording, log in with an X11
session** (most distributions offer "Xorg" or "X11" on the login screen's settings button).

To play back into every program on Wayland, give your user access to `/dev/uinput` once, then log out and back in:

```
echo 'KERNEL=="uinput", GROUP="input", MODE="0660", OPTIONS+="static_node=uinput"' | sudo tee /etc/udev/rules.d/60-macrorecorder-uinput.rules
sudo usermod -aG input "$USER"
sudo udevadm control --reload-rules && sudo udevadm trigger
```

The first line lets members of the `input` group use `/dev/uinput`; the second adds you to that group. On Wayland,
playback through `/dev/uinput` types plain US-keyboard characters, and "Window Focus", "Wait for Pixel Color",
"Wait for Key" and positions relative to a window aren't available (Wayland keeps that information private); the
Activity log says when a step was skipped. Blocking the keyboard during playback is Windows-only.

## First run

The app asks where to keep its settings (theme, hotkeys, playback options, recent files). Pick any folder, or
choose **Don't Keep App Data** to start fresh every time and leave nothing behind. You can change this later in
**File ▸ Settings ▸ Change App Data Folder…**.

Your macros are saved wherever you choose when you save them.

## Using the app

### Record
1. Press **● Record** (or **F9**).
2. Do what you want to automate. Steps appear in the list as you go.
3. Press **Esc**, **F9** or **■ Stop Recording**.

Clicks on the app's own window and the hotkeys themselves are never recorded. **Macro ▸ Record After Selected
Step** inserts the new steps there instead of at the end; **Macro ▸ New Recording** starts over.

### Play
Press **▶ Play** (or **F10**). With **Countdown Before Playing** on, you get a few seconds to switch to the right
window. **F10** pauses and resumes; **Esc** or **■ Stop** stops. Select some steps and press **▶ Play Selection**
to play just those. The **Macro** menu can also play from the selected step, up to it, or just that one step
(once, to try it out).

### Selecting steps
Click a step to select it, Ctrl+click to add or remove one, Shift+click for a range. Or press anywhere in the list,
including the empty space below the steps, and drag a selection box over the steps you want, as on the Windows
desktop or in File Explorer. Hold Ctrl to add to what's already selected; drag past the top or bottom edge to
scroll. Clicking empty space clears the selection.

### Previewing a step
The row under the pointer is lightly highlighted. Point at the **Value** of a mouse step (a move, click, scroll or
pixel check) and a small animated picture appears in the list's right-hand side: the move's path traced at its
recorded speed, or the spot where it clicks or scrolls, zoomed in, with a small map of your screens in the corner.
Turn it off with **View ▸ Preview Steps on Hover**.

### The cards
Each card folds to its title with **Hide ▾** / **Show ▴**.

- **Playback**
  - **Speed**: 100 % plays at the recorded speed, 200 % twice as fast.
  - **Repeat**: a number of times, for a number of seconds, or until stopped.
  - **Mouse Path**: *As Recorded* follows the exact recorded movement; *Straight Line* and *Smooth Curve* go
    directly to each point; *Human-Like* wobbles and slows down near the target, for programs that notice robotic
    movement.
  - **Typing**: *As Recorded* keeps the typing rhythm (some programs miss keys typed too fast); *Instant* types
    everything at once.
  - **When Finished**: do nothing, close the app, lock the computer, sleep, log off, restart or shut down. Only
    after the macro finishes on its own, and the system actions wait 30 seconds so you can cancel.
  - **Stop on Key Press** and **Stop on Mouse Move**.
  - **Block Keyboard While Playing** (Windows): key presses don't reach other programs while a macro plays; the
    Stop and Pause hotkeys still work.
  - **Restore Mouse Position** afterwards, **Show Notifications**, **Minimize While Playing**.
- **Recording**
  - **Mouse Moves** on or off, and the **Smallest** movement kept (in pixels).
  - **Path Detail**: 100 % keeps every point of a mouse path; lower leaves out points that barely change its
    shape.
  - **Keyboard** on or off, and **Merge Typing Into Text** (typed characters become one Text step).
  - **Show Pauses From**: pauses at least this long become their own *Wait* steps you can edit. Shorter pauses
    are kept with the next step (shown as "+120 ms before"), so timing stays exact.
  - **Positions Relative to Window**: clicks are recorded from the window's corner instead of the screen's, so
    the macro still works when the window is somewhere else.
  - **Minimize While Recording**.
- **Hotkeys**: click a box and press the key you want, or clear it. If another program already uses a key, or
  two hotkeys share one, the card says so. **Capture Position** (F8) works while a step's editor is open: point
  at a spot and press it to fill in the position (and the color, for Wait for Pixel Color).
- **Actions**: the steps. Double-click a step to edit it, or the **Label** / **Comment** column to type there.
  **Show:** hides kinds of steps from the list (hidden steps still play). The selected step's position is
  marked on the screen with a red ring (**View ▸ Mark Selected Step on Screen**).
- **Activity**: what happened and when. **View ▸ Detailed Activity Log** also lists every step played.

### Repeating part of a macro
Give the first step of the part a **Label** (for example `start`), then add **Flow ▸ Repeat** after the part and
pick that label. "3 times" plays the part three times in total.

### Menus
- **File**: New, Open, Open Recent, Save, Save As, Export to CSV, Copy Command Line (plays the saved macro and
  closes; for a shortcut or Windows Task Scheduler), Settings, Quit.
  - **Settings**: Change App Data Folder, Reset Settings to Defaults, Confirm Before Deleting Steps, Start
    Minimized, Start at Login, and on Windows: Send Keys as Scan Codes (for games that ignore normal key presses),
    Restart as Administrator (needed to control programs running as administrator).
- **Macro**: Record, Record After Selected Step, New Recording, Play, Play Selection, Play From Selected Step,
  Play Up to Selected Step, Play This Step Only, Pause / Resume, Stop, Add Action, Clean Up (merges back-to-back
  waits and moves).
- **Edit**: Undo, Redo, Cut, Copy, Paste, Delete, Select All, Edit Step, Enable / Disable, Move Up / Down,
  Combine Into Click or Key Press (a button down + up, or key down + up), Find and Replace.
- **View**: Theme (hover over a theme to preview it), Show Activity Log, Detailed Activity Log, Always on Top,
  Mark Selected Step on Screen, Preview Steps on Hover, Reset View Options.
- **Help**: Project Website, Open App Data Folder, Report a Problem, About.

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
| `--record FILE` | Starts minimized and recording at once; when you stop (Stop hotkey, Esc by default), saves to FILE and quits. |
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

For the single self-contained exe, also install `mingw-w64-ucrt-x86_64-qt6-static`, plus the libraries it links
(`libtiff libwebp harfbuzz freetype libpng zlib pcre2 openssl libb2 brotli bzip2`, each with the
`mingw-w64-ucrt-x86_64-` prefix), and configure with
`-DSTATIC_BUILD=ON -DCMAKE_PREFIX_PATH=/ucrt64/qt6-static`.

**Linux:** install Qt 6 (with the SVG module), CMake, Ninja and the X11 development files (`libx11-dev
libxtst-dev` on Debian and Ubuntu), then run the same two `cmake` commands.

### Changing the icon
Edit `resources/icon.svg`, then rebuild `packaging/icon.ico` with the small tool in `packaging/make_icon.cpp`
(build it against Qt Gui and Qt Svg, then run `make_icon resources/icon.svg packaging/icon.ico`).

## Automatic releases

GitHub Actions (`.github/workflows/build-apps.yml`) builds the Windows exe and the Linux AppImage and checks that
both start. On Linux it also records and plays back for real on a virtual X11 display (`.github/ci/x11_check.sh`).

- **Pull requests:** the built files are kept with the run for 14 days (open the run, then **Artifacts**).
- **Every merge into `main`:** a new release is published with both files, numbered automatically
  (v0.1.1 → v0.1.2). Put `[minor]` or `[major]` in the pull request's **title** for v0.2.0 or v1.0.0.

## Code layout

| Folder | What's there |
|---|---|
| `src/core` | Macro steps and file format (`Action`), recording logic (`Recorder`), playback (`Player`), mouse paths (`Paths`). No operating-system code. |
| `src/platform` | The operating-system layer: `win/` (hooks, input, hotkeys, windows), `linux/` (X11 with XRecord and XTest, `/dev/uinput` for Wayland, and the choice between them). |
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
