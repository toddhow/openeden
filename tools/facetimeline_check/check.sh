#!/bin/bash
# Editor windows open off-screen, out of the taskbar and without taking focus (apps/opencs/view/doc/quietwindows.hpp); run with OPENEDEN_QUIET_WINDOW=0 to see them.
export OPENEDEN_QUIET_WINDOW="${OPENEDEN_QUIET_WINDOW-1}"
# The face timeline in the real editor (task 0147, editor_modules/face_editor, Editor Module API 1.4). With a voice line it
# makes a curve file through the module's "New Curves File" command, fills three channels with keys from the audio's
# loudness, and in OPENEDEN_CS_FACECHECK (runFaceCheck in apps/opencs/editor.cpp) drags a key with synthetic mouse events
# on the canvas (ONE undo step), adds a key with a double click, undoes both through Edit > Undo, clicks the ruler, plays
# the audio, shows the Generate dialog, edits again and saves. Frames of the canvas and the dialog are written.
#
# Usage: tools/facetimeline_check/check.sh <build dir> <empty work dir> [<voice line .mp3/.wav/.fuz>]
# Without a voice line a SYNTHETIC speech-like 16 kHz WAV is generated here (python must be on PATH): no game data is
# needed. A given voice line is COPIED into the work folder (the editor writes the .facecurves.json beside its audio).
# The work folder must be under I:/Projects/OpenEden-work; the editor runs with a scratch config and user folder.
#
# Pass/fail lines:
#   1. the editor exits with code 0
#   2. the curve file was made and opened, with one curve track and one waveform track
#   3. a drag of a key is ONE module undo step (none during the drag) and moves the key
#   4. a double click adds a key; Edit > Undo twice restores the keys from before the drag
#   5. a click on the ruler moves the playhead; playing moves it on and Stop stops it
#   6. the Generate dialog was shown and accepted; the command says the ONNX runtime is not there yet
#   7. Save writes a file that reads back: the recorded audio SHA-256 equals python's hashlib on the audio, the channels
#      the check edited are non-zero, the unedited ones are zero
set -u
[ $# -ge 2 ] || { sed -n 14,15p "$0"; exit 2; }
BUILD=$(cd "$1" && { pwd -W 2>/dev/null || pwd; })
WORK=$2
VOICE=${3:-}
native() { cygpath -m "$1" 2>/dev/null || echo "$1"; }
case "$(native "$WORK")" in
    I:/Projects/OpenEden-work/*) ;;
    *) echo "refused: $WORK must be under I:/Projects/OpenEden-work"; exit 2 ;;
esac
[ -z "$(ls -A "$WORK" 2>/dev/null)" ] || { echo "refused: $WORK is not empty"; exit 2; }
FAILED=0
pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; FAILED=1; }

mkdir -p "$WORK/config" "$WORK/user" "$WORK/empty-data" "$WORK/audio" "$WORK/frames"
perl -e 'my $hedr = pack("fVa32a256V", 1.3, 0, "generated", "empty content file for the face timeline check", 0);
    my $sub = "HEDR" . pack("V", length $hedr) . $hedr;
    binmode STDOUT; print "TES3" . pack("VVV", length $sub, 0, 0) . $sub;' > "$WORK/empty-data/empty.esp"
W=$(native "$WORK")
{
    echo "data=\"$W/empty-data\""
    echo "content=empty.esp"
} > "$WORK/config/openmw.cfg"

if [ -n "$VOICE" ]; then
    EXT="${VOICE##*.}"
    cp "$VOICE" "$WORK/audio/line.$EXT"
    AUDIO="$W/audio/line.$EXT"
else
    python - "$WORK/audio/line.wav" <<'PY'
import math, struct, sys, wave
rate = 16000
seconds = 3.2
frames = bytearray()
for i in range(int(rate * seconds)):
    t = i / rate
    syllable = max(0.0, math.sin(2 * math.pi * 3.5 * t)) ** 1.5      # about 3.5 syllables a second
    pause = 0.0 if 1.4 < t < 1.8 else 1.0                              # a gap in the middle
    voice = (math.sin(2 * math.pi * 130 * t) * 0.5 + math.sin(2 * math.pi * 520 * t) * 0.25
             + math.sin(2 * math.pi * 1500 * t) * 0.12)
    frames += struct.pack('<h', int(32767 * 0.8 * syllable * pause * voice))
with wave.open(sys.argv[1], 'wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(rate)
    w.writeframes(bytes(frames))
PY
    AUDIO="$W/audio/line.wav"
fi

(cd "$BUILD" && OPENEDEN_CS_UI_CHECK=";40000" OPENEDEN_CS_FACECHECK="$W/frames/face;$AUDIO" \
    timeout 300 ./eden-studio.exe --replace=config --config "$W/config" --user-data "$W/user" -- "$W/empty-data/empty.esp" \
    > "$WORK/run.out" 2>&1)
rc=$?
LOG="$WORK/config/eden-studio.log"
[ $rc = 0 ] && pass "the editor exited with code 0" || fail "the editor exited with code $rc (see $WORK/run.out)"
grep "UI check: face" "$LOG" | sed 's/^.*UI check: face/face/' > "$WORK/face.txt"

if grep -q "face new curves command ran, file written" "$WORK/face.txt" && grep -q "face opened yes" "$WORK/face.txt" \
    && grep -q "face tracks: curves 1, waveforms 1" "$WORK/face.txt"; then
    pass "curve file made and opened, one curve track and one waveform track ($(grep 'face waveform' "$WORK/face.txt"))"
else
    fail "open: $(grep -E 'face (new curves|opened|tracks)' "$WORK/face.txt" | tr '\n' ' ')"
fi
drag_start=$(sed -n 's/^face drag key [0-9]* from pixel [0-9,]*, undo steps \([0-9]*\)$/\1/p' "$WORK/face.txt")
drag_during=$(sed -n 's/^face during drag: keys .*, undo steps \([0-9]*\)$/\1/p' "$WORK/face.txt")
drag_after=$(sed -n 's/^face drag released: undo steps \([0-9]*\),.*$/\1/p' "$WORK/face.txt")
if [ -n "$drag_start" ] && [ "$drag_during" = "$drag_start" ] && [ "$drag_after" = "$((drag_start + 1))" ]; then
    pass "the drag is ONE undo step: $drag_start during, $drag_after after"
else
    fail "drag steps: start '$drag_start' during '$drag_during' after '$drag_after'"
fi
if grep -q "face drag released:.*document modified 0, asset dirty yes" "$WORK/face.txt"; then
    pass "a key edit marks the asset dirty and leaves the content document unmodified"
else
    fail "drag release: $(grep 'face drag released' "$WORK/face.txt")"
fi
before_keys=$(sed -n 's/^face jawopen keys before \([0-9]*\) .*$/\1/p' "$WORK/face.txt")
if grep -qE "face double click add: keys ([0-9]+) -> " "$WORK/face.txt" && \
   [ "$(sed -n 's/^face double click add: keys \([0-9]*\) -> \([0-9]*\),.*$/\2 \1/p' "$WORK/face.txt" | awk '{print $1-$2}')" = 1 ]; then
    pass "a double click added one key"
else
    fail "double click: $(grep 'face double click' "$WORK/face.txt")"
fi
undo_line=$(grep "face Edit > Undo twice" "$WORK/face.txt")
before_text=$(sed -n 's/^face jawopen keys before [0-9]* \(.*\)$/\1/p' "$WORK/face.txt")
now_text=$(echo "$undo_line" | sed -n 's/^.*, keys now \(.*\)$/\1/p')
if echo "$undo_line" | grep -qE "keys ([0-9]+) -> ([0-9]+) -> $before_keys," && [ -n "$before_text" ] && [ "$before_text" = "$now_text" ]; then
    pass "Edit > Undo twice restores exactly the $before_keys keys (times and values) from before the drag: $(echo "$undo_line" | sed 's/, keys now.*//')"
else
    fail "undo: $undo_line (expected to end at the $before_keys keys: $before_text)"
fi
if grep -qE "face playhead after a click on the ruler at 0.6 s: 0\.[56]" "$WORK/face.txt" && grep -q "face play started yes, playing yes" "$WORK/face.txt" \
   && grep -qE "face playhead while playing: (0\.[3-9]|[1-9])" "$WORK/face.txt" && grep -q "face playing after stop no" "$WORK/face.txt"; then
    pass "ruler click moved the playhead; playing moved it on; Stop stopped it ($(grep 'while playing' "$WORK/face.txt"))"
else
    fail "playhead/play: $(grep -E 'face (playhead|play)' "$WORK/face.txt" | tr '\n' ' ')"
fi
if grep -q "face dialog 'Generate Curves': written" "$WORK/face.txt" && grep -q "face generate command ran yes" "$WORK/face.txt" \
   && grep -q "ONNX runtime" "$LOG"; then
    pass "the Generate dialog was shown, grabbed and accepted; the module said the ONNX runtime is not there yet"
else
    fail "dialog: $(grep -E 'face (dialog|generate)' "$WORK/face.txt" | tr '\n' ' ')"
fi
if grep -q "face saved yes" "$WORK/face.txt" && grep -q "dirty before yes, dirty after no" "$WORK/face.txt"; then
    python - "$WORK/audio" <<'PY' > "$WORK/readback.txt"
import hashlib, json, sys, glob, os
d = sys.argv[1]
curves = glob.glob(os.path.join(d, 'line.facecurves.json'))[0]
audio = [p for p in glob.glob(os.path.join(d, 'line.*')) if not p.endswith('.json')][0]
c = json.load(open(curves))
sha = hashlib.sha256(open(audio, 'rb').read()).hexdigest()
nonzero = {ch['name']: any(v != 0 for v in ch['values']) for ch in c['channels']}
print('sha_match', c['audio']['sha256'] == sha)
print('frames', c['frameCount'], 'channels', len(c['channels']))
print('edited_nonzero', all(nonzero[n] for n in ('jawopen', 'mouthfunnel', 'eyeblinkleft')))
print('others_zero', all(not v for n, v in nonzero.items() if n not in ('jawopen', 'mouthfunnel', 'eyeblinkleft')))
PY
    if grep -q "sha_match True" "$WORK/readback.txt" && grep -q "edited_nonzero True" "$WORK/readback.txt" \
       && grep -q "others_zero True" "$WORK/readback.txt"; then
        pass "the saved file reads back: $(tr '\n' ' ' < "$WORK/readback.txt")"
    else
        fail "read back: $(tr '\n' ' ' < "$WORK/readback.txt")"
    fi
else
    fail "save: $(grep 'face saved' "$WORK/face.txt")"
fi
ls "$WORK/frames"/*.png > /dev/null 2>&1 && pass "frames written: $(ls "$WORK/frames" | tr '\n' ' ')" || fail "no frames"
grep "face models row" "$WORK/face.txt" | sed 's/^/models: /'
[ $FAILED = 0 ] && echo "FACE TIMELINE CHECK PASSED" || echo "FACE TIMELINE CHECK FAILED"
exit $FAILED
