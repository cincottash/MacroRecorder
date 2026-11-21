#!/usr/bin/env bash
# Real recording and playback on a virtual X display (Xvfb + openbox), run by the Linux CI job.
# usage: x11_check.sh <path to MacroRecorder AppImage or executable>
set -euo pipefail
APP="$1"
HERE="$(cd "$(dirname "$0")" && pwd)"
WORK="$(mktemp -d)"
LOG="$WORK/target.jsonl"
export DISPLAY=:99 QT_QPA_PLATFORM=xcb APPIMAGE_EXTRACT_AND_RUN=1 XDG_SESSION_TYPE=x11
unset WAYLAND_DISPLAY || true

Xvfb :99 -screen 0 1600x1000x24 >/dev/null 2>&1 &
sleep 1
openbox >/dev/null 2>&1 &
sleep 1
python3 "$HERE/target.py" "$LOG" &
TARGET=$!
sleep 2
cleanup() { kill "$TARGET" 2>/dev/null || true; }
trap cleanup EXIT

fail() { echo "FAIL: $*"; exit 1; }

# ---- 1. playback: click, text, a held key, scroll, right click
mkdir -p "$WORK/play"
cat > "$WORK/play/settings.json" <<'EOF'
{"countdown": false, "stop_on_user_input": false, "restore_mouse": false, "typing": "instant",
 "hotkey_record": "", "hotkey_play": "", "hotkey_stop": "Esc"}
EOF
cat > "$WORK/play/test.macro" <<'EOF'
{"format": "macro-recorder/macro", "version": 1, "actions": [
 {"type": "window_focus", "exe": "", "title": "MR Test Target", "title_match": "exact", "timeout_ms": 5000},
 {"type": "mouse_click", "at": [400, 150], "button": "left"},
 {"type": "text", "text": "Hello X11 abcdefghijklmnopqrstuvwxyz"},
 {"type": "key_down", "key": "Left"}, {"type": "wait", "ms": 1000}, {"type": "key_up", "key": "Left"},
 {"type": "text", "text": "Z"},
 {"type": "wheel", "at": [500, 500], "notches": -2},
 {"type": "mouse_click", "at": [600, 550], "button": "right"}
]}
EOF
T0=$(date +%s.%N)
timeout 60 "$APP" "$WORK/play/test.macro" --play --quit --data-folder "$WORK/play"
python3 - "$LOG" "$T0" <<'EOF'
import json, sys
ev = [json.loads(l) for l in open(sys.argv[1]) if json.loads(l)['t'] >= float(sys.argv[2])]
downs = [(e['x'], e['y'], e['b']) for e in ev if e['ev'] == 'down']
lefts = sum(1 for e in ev if e['ev'] == 'key' and e['k'] == 'Left')
text = [e['text'] for e in ev if e['ev'] == 'text'][-1]
base = 'Hello X11 abcdefghijklmnopqrstuvwxyz'
print('clicks:', downs, '| Left presses:', lefts, '| text:', repr(text))
assert (400, 150, 1) in downs, 'left click not at 400,150'
assert (500, 500, 5) in downs, 'scroll down not at 500,500'        # X11 wheel down = button 5
assert (600, 550, 3) in downs, 'right click not at 600,550'
assert 8 <= lefts <= 40, f'held Left: {lefts} presses (expected about 1 + repeats in 1 s, not doubled)'
assert text == base[:len(base) - lefts] + 'Z' + base[len(base) - lefts:], 'Z not inserted where the held Left key left the cursor'
print('PASS playback')
EOF

# ---- 2. recording: --record, xdotool clicks and types, the Esc hotkey stops and saves
mkdir -p "$WORK/rec"
echo '{"countdown": false, "hotkey_record": "", "hotkey_play": "", "hotkey_stop": "Esc"}' > "$WORK/rec/settings.json"
"$APP" --record "$WORK/rec/out.macro" --data-folder "$WORK/rec" &
APPPID=$!
sleep 4
# where every visible window is (helps if a click lands on the wrong one)
for w in $(xdotool search --onlyvisible --name . 2>/dev/null); do
  echo "window: $(xdotool getwindowname "$w") | $(xdotool getwindowgeometry "$w" | tail -n +2 | tr -s ' \n' ' ')"
done || true
T1=$(date +%s.%N)
xdotool mousemove 420 160 click 1
sleep 0.3
xdotool type --delay 80 "rec ok"
sleep 0.7
xdotool mousemove 650 500 click 3
sleep 0.6
xdotool key Escape
wait "$APPPID" || fail "the app exited with an error after recording"
python3 - "$LOG" "$T1" <<'EOF'
import json, sys
ev = [json.loads(l) for l in open(sys.argv[1]) if json.loads(l)['t'] >= float(sys.argv[2])]
print('the target window got:', [(e['ev'], e.get('x'), e.get('y'), e.get('b'), e.get('text')) for e in ev if e['ev'] in ('down', 'text')])
EOF
python3 - "$WORK/rec/out.macro" <<'EOF'
import json, sys
acts = json.load(open(sys.argv[1]))['actions']
print('recorded:', [a['type'] for a in acts])
clicks = [(tuple(a['at']), a['button']) for a in acts if a['type'] == 'mouse_click']
texts = [a['text'] for a in acts if a['type'] == 'text']
assert ((420, 160), 'left') in clicks, f'left click at 420,160 not recorded: {clicks}'
assert ((650, 500), 'right') in clicks, f'right click at 650,500 not recorded: {clicks}'
assert 'rec ok' in texts, f'typing not recorded as "rec ok": {texts}'
assert not any(a['type'] == 'key_press' and a['key'] == 'Escape' for a in acts), 'the Stop hotkey was recorded'
print('PASS recording')
EOF
echo "X11 checks passed"
