# Task 0143: The voice of a Skyrim SE conversation (milestone M-S4, slice 3)

Status: built on branch `skyvoice` (Windows only; not pushed). Full loop in form: the pure parts were written tests first and
shown red against stubs; the same hand wrote tests and code; no fresh reviewer.

Design doc section: `docs/design/skyrim-se-implementation-plan.md` 4.22 (voice); task 0141 (the conversation this adds
sound to), task 0137 (dialogue records), decision D9 (decode xWMA with the engine's ffmpeg if the pinned build is LGPL-clean).

Goal: when a Skyrim NPC speaks an INFO line in a neutral session, the voice file is found through the VFS, its `.fuz` is
split, the xWMA audio plays from the speaker through the engine's sound manager, and the subtitle stays the message box text.

## 1. Facts settled by the user's data (aggregates only; nothing from the game is in the repository)

| Fact | How established |
| --- | --- |
| `.fuz` layout: `"FUZE"`, u32 version (1), u32 lip size, the `.lip` bytes, then a RIFF file whose form type is `XWMA`; its RIFF size + 8 equals the rest of the file | all 75,408 files of `Skyrim - Voices_en0.bsa` (Python walker, then the C++ reader in a gated test: 75,408 split, 75,408 xWMA, 0 with trailing bytes, 0 failed; largest lip part 32,199 bytes, largest audio 108,752) |
| The archive is a BSA v105, flags 0x3, not compressed, no embedded names; five folders under `sound\voice\`: skyrim.esm 61,811, dawnguard.esm 5,398, dragonborn.esm 7,111, hearthfires.esm 1,088 files (no update.esm folder) | Python BSA walker |
| The xWMA stream is tag 0x161 (WMA v2), mono, 44,100 Hz, average 4,000 bytes/s, with a `dpds` chunk giving the decoded size | chunk walk of the sampled files |
| File name: `<stem>_<INFO form id, 8 hex digits>_<response number>.fuz`, under `voice\<plugin file>\<voice type editor id>\` | matched against the INFO records of Skyrim.esm: 34,290 voice files belong to an INFO response; 34,252 (99.89 percent) follow the stem rule below; the other 38 carry the names of records renamed after the recording |
| Stem: `<quest editor id>_<topic editor id>` in lower case; when that is longer than 26 characters the quest is cut to 10 characters and the whole to 26. The quest is the TOPIC's quest (DIAL QNAM), not the INFO's | same match (the first guess, quest cut to 10 always, fitted 82 percent; "cut the whole to 26" alone 62 percent) |
| The INFO form id is the id as stored in the plugin whose folder holds the file (its own master index); the response number is the TRDT number | the same match |
| An INFO of Skyrim.esm that a later plugin overrides keeps its file in skyrim.esm's folder under the load order id; 12 other overrides are found under the overriding plugin's folder | all five masters, C++: of 44,033 responses 33,876 have a file under the winning plugin's folder; of the 10,157 without, 407 are overrides and all 407 have it under skyrim.esm. The pack looks in the origin folder first, then the winner's |
| Bannered Mare: 7 actors, 6 greet, 6 greetings have a voice line (6 files) | gated test through the real `SkyrimCellSource` / `DialogueSource` |

10,157 responses (23 percent) have no voice file under either folder: player lines, silent and text-only responses,
DLC lines in other language archives.

## 2. Decision D9: is the ffmpeg the build links clean, and can it decode wmav2?

Read from the pinned vcpkg build (`deps/vcpkg-x64-windows-2022-2026-02-24/installed/x64-windows`):

- `share/ffmpeg/copyright` is the LGPL 2.1 text; `vcpkg_abi_info.txt` lists features `avcodec;avdevice;avfilter;avformat;core;swresample;swscale` only (no `gpl`, no `nonfree`).
- The build configuration string in `avutil-60.dll` (also in `avcodec-62.dll`, `avformat-62.dll`) holds no `--enable-gpl`, `--enable-nonfree` or `--enable-version3`; every third-party library switch is `--disable-...`; the DLLs report `libavutil/libavcodec/libavformat license: LGPL version 2.1 or later`.
- `avcodec-62.dll` contains the `wmav2` decoder and `avformat-62.dll` the `xwma` demuxer; in game the pair decodes the files (log lines below).
- Linked dynamically (DLLs), so the LGPL's relinking condition holds as it already does for Morrowind's mp3 and video.

Verdict: clean and sufficient. No codec was written.

## 3. Design

- **Pure container reader** `components/fuz/fuz.{hpp,cpp}` (formats layer, standard library only): `Fuz::split(bytes, name, limits)` returns views of the lip part and the RIFF audio; refuses a wrong magic or version, a lip size past the end or over 4 MiB, audio that is not a RIFF file, a RIFF size larger than the bytes present; caps the whole file at 64 MiB; drops and counts bytes after the declared RIFF size. No exception, no allocation beyond the error text.
- **Pack** `game_skyrimse` `voice.{hpp,cpp}`: `voiceStem`, `voicePath` (pure). `DialogueIndex` now keeps the plugin file names, the VTYP editor ids and `voiceOriginOf(info)`; `FormIdResolver::ownStoredId`; `SkyrimConditionWorld::speakerVoiceType()` (the NPC's VTCK after templates, else its race's for its sex; `hasVoiceType` now shares it). `DialogueRuntime::setVoiceProbe` turns voice on; each response's file is named for the origin folder, then the winner's, and kept only if the probe says it exists. `SkyrimWorldView` gives the runtime a probe over its `FileSource` (the session's VFS: `sound/` + path). Counters: `Stats::mVoiceFound / mVoiceMissing / mVoiceUnnamed`.
- **SDK** `dialogue_source.hpp` 0.1 to 0.2: `DialogueTurn::voice` (trailing field; VFS paths relative to the sound folder). Additive, found by `dynamic_cast` as before; no `kSdkVersion` change.
- **Engine** `FFmpegDecoder::open` opens a `.fuz` by extension: reads it capped, splits it, hands ffmpeg the audio part through the same stream hooks (Morrowind's files take no new branch). `MWWorld::NeutralVoiceQueue` (pure) plays a turn's lines one after the other; `NeutralDialogueSession` keeps the speaker's `Ptr`, starts the first line when a turn is shown, the next when `SoundManager::sayActive` goes false, and stops the voice when a new turn is shown or the conversation closes. The speaker's head position (`World::getActorHeadPosition`, falling back to the reference position) is the 3D position; `SoundManager::say` is unchanged. The voice path is prefixed with `sound/` by `Misc::ResourceHelpers::correctSoundPath`, as the Morrowind dialogue does.
- No new setting: voice plays whenever `[Game] neutral actors` lets the conversation happen and the session's VFS holds the voice archive (`fallback-archive=Skyrim - Voices_en0.bsa`; without it `Stats::mVoiceMissing` counts every line and nothing plays). Off means unchanged: `[Game] neutral actors` off keeps actors inert; a Morrowind session never reaches this code.
- Lip sync is out of scope (the `.lip` part is only measured).

## 4. Acceptance

- [x] `Fuz::split` on generated bytes: 14 tests (10 red against a stub first; the 4 that passed at once test refusals), a 20,000-mutation fuzz, every truncation refused.
- [x] Voice name rules and runtime on generated plugins: `SkyrimVoice*` 14 tests (11 of the first 14 red against stubs; the 3 that passed test absence or the existing behaviour), `SkyrimVoiceOverride*` 3 (the origin-folder test red first).
- [x] Real data: 75,408 of 75,408 `.fuz` files split; stem rule 99.89 percent of the Skyrim.esm files; Bannered Mare 6 of 6 greetings have a voice file.
- [x] In game (below): Hulda's greeting voice file found, split, decoded with wmav2 and played; four turns with a two-line turn.
- [x] Morrowind unchanged (below).

## 5. Stage log

### Evidence

- **Reds first.** `Fuz.*` 14 tests: 10 failed against the stub (private MSVC /W4 /WX rig), 4 pass on a stub because they test refusals. `SkyrimVoice*` (names, index, runtime, override): 11 of the first 18 failed against stubs; the rest test absence or the behaviour the code already had (noted). The queue tests `NeutralVoiceQueue*` were written beside the stub but not run red (the stub was never built into `openmw-tests`); their mutations below prove they bite.
- **Mutations (one line each, built and run, then restored):** stem limit 26 to 25 (2 named tests fail); probe ignored (3); origin folder dropped (1); RIFF size unchecked in `Fuz::split` (2); queue ignores the playing line (1); queue cap removed (1); response number fixed to 1 (3).
- **Unit and integration suites:** `components-tests` 9,622 pass (`-*Slang*:*Complement*:*Property*:*Real*`), 15 skipped (data-dependent); `openmw-tests` 2,214 pass.
- **Real data** (`SkyrimVoiceRealData`, gated by `OPENEDEN_SKYRIMSE_DATA`; prints counts only): container 75,408 of 75,408; names: 44,033 responses, 33,876 with a file under the winner's folder, 10,157 without, of which 407 are overrides of Skyrim.esm records and all 407 have the file under skyrim.esm; Bannered Mare 7 actors, 6 greeted, 6 greetings with voice, 6 voice lines.
- **In game, Skyrim** (`tools/neutral_session_check/check.sh ... WhiterunBanneredMare 9000 n5talk`, `ALSOFT_DRIVERS=null`, `OPENEDEN_QUIET_WINDOW`, `OPENMW_DISABLE_CRASH_CATCHER=1` because the cold start tripped the 5 s freeze monitor once; `OPENEDEN_NEUTRAL_DIALOGUE_PRESS="150:0,3000:1"`): `NTALK voice: line 1 voice/skyrim.esm/femalenord/<stem>_000142c0_1.fuz` then `Voice file sound/...: lip 241 bytes, audio 3036 bytes (xWMA)` then `decoded 0.557506 s of audio with wmav2 at 44100 Hz` (the file's `dpds` chunk says 49,152 PCM bytes = 0.55728 s). Turn 2 has two lines: the second starts when the first ends (6.75 s later). The "decoded N s" figure is the decoder's pts counter and can overshoot by under a second on long lines (7.57 against 6.69 s from `dpds`); the wall-clock gaps between log lines are the real durations. 0 crash dumps, 0 unexpected error lines.
- **In game, Morrowind** (`tools/voice_check/check.sh`): a Balmora NPC says `Sound/Vo/a/f/Atk_AF001.mp3` through `core.sound.say`: active at once, ended 2.26 s later, 0 errors. `tools/worldobjects_check` against a build of the parent commit (`3094f37cd2`, `git archive` of it, built in `skyvoice/parent`): OK, 110 lines equal. Fixed-time Balmora frame 600 (`OPENEDEN_FIXED_FRAME_TIME=0.0166667`, seed 7): SHA-256 `1c5d47ad0b5dc32d...` for both builds, the value recorded in earlier tasks.

### Found on the way

- `SoundManager::say` takes the full VFS path (`sound/...`); the first version passed the pack's path without the prefix and the game logged `Resource '...mp3' not found` (the `correctSoundPath` fallback). Fixed with `Misc::ResourceHelpers::correctSoundPath`.
- A topic's voice file is named after the TOPIC's quest, not the INFO's QNAM; and the file of a later plugin's override of an INFO is in the folder of the plugin that made the INFO.
- Skyrim's `.lip` is not used; `fuzz` files hold nothing else.
- `tools/neutral_session_check` needs the voice archive as a `fallback-archive=` line (`CFG_EXTRA`); without it nothing plays and `Stats::mVoiceMissing` counts every line.
- Running this tree's executables from Git Bash needs the vcpkg `bin` and `bin/Release` folders (MyGUIEngine.dll) on `PATH`, else the loader reports a missing `api-ms-win-*.dll`.

### Not done / not run

The Linux gate and GCC `-Werror` (Docker not used), clang-format 14 (version 19 formatted the new files only; the existing files' edited lines are formatted by hand), clang-tidy, the Lua integration tests, sanitizers, a fresh reviewer, a real audio device (all runs on OpenAL's null device, so nobody has heard it), lip sync, the other language archives, voice for creatures and for Fallout 4 (`.wav` form type is split but not played in a game), 3D attenuation judged by ear (the parameters are `SoundManager::playVoice`'s Morrowind defaults), pausing a line when the box is closed by the window manager rather than by Goodbye (the session stops the voice on `close` and `abandon`).

### Decisions for toddhow

1. Add a setting (`[Game] neutral voice`, off by default) or keep voice as part of the conversation? Recommendation: keep; the conversation itself is behind `[Game] neutral actors` and a missing archive means silence.
2. The 38 Skyrim.esm files whose names carry a renamed record's editor id cannot be reached by the rule; accept (0.11 percent), or add a fallback that scans the folder by `<id>_<n>.fuz`? The VFS has no directory listing in the contract today.
3. Listen once on a real device (steps in `docs/toddhow-checks.md` section 3).
