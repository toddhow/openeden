# Task: Facial speech animation, generated ahead of time and played in the game (DRAFT)

Status: draft for toddhow. Nothing is built. Evidence and sources: `docs/research/0034-nvidia-speech-to-face-animation.md` (read it first; its section 3 and 4 hold the licence findings and the open legal questions).
Design doc section: `docs/design/eden-studio-plan.md` (panel 5, "Facial Expression & Speech Timeline"); CreativeMotion's `Open Procedural Animation System — Living Design Doc.md`, "Facial animation layer" (`I:\Projects\CreativeMotion\docs\`); `docs/design/advancedbodies-in-openeden.md` section 7.2 item 5 (head part, face morphs later); task 0070 (face bones fold to `Head` for now).
Goal: let a mod author produce facial speech animation offline (NVIDIA Audio2Face-3D being one generator among several), store it as an OpenEden-owned curve format, play it in the game in sync with the voice line, and fall back to today's loudness drive when no curves exist.
Inherited code touched: `apps/openmw/mwdialogue/dialoguemanagerimp.cpp` (`DialogueManager::say`, line 625), `apps/openmw/mwsound/soundmanagerimp.cpp` (`SoundManager::say`, 316; `playVoice`, 202), `apps/openmw/mwrender/npc.cpp` (`Npc::setFacialState`, 967), `components/mwanimation/face.*` (`Face::update`, `Face::loadTags`), `apps/openmw/mwsound/loudness.*`.
Inputs / outputs: new format `openeden.facecurves` v1 (canonical JSON for authoring, binary Eden Archive entry for shipping); an importer for generator outputs; an Eden Studio panel; a face layer in the game; a new `[Face]` settings category (off by default).
Constraints:
- **Off by default and unchanged when off:** with `[Face] speech curves = false` the existing loudness drive and random blinks run exactly as today.
- No NVIDIA code, weights, CUDA or TensorRT in this repository (architecture B and C of note 0034; guardrail "no proprietary code or SDKs"). Audio2Face is reached only as an external tool installed by the author.
- Readers handle hostile input (caps on channels, frames, string lengths, file size), per the container-reader rule.
- Generated curves are authored data: stored with the generator name, version and seed and never regenerated at run time (the generators are not known to be deterministic across GPUs and drivers).
- A curve file never contains audio. It records the audio's name, duration and a hash of the user's file and is ignored if they do not match.
- Playback runs on the main thread; no heap allocation per frame in the face layer.
- Curves derived from Bethesda audio are derived from game assets: by the project's rule for bakes they are generated on the user's machine and never shipped (see "Shipping rule" below) until a lawyer says otherwise.

## Stages

- **a. Decisions and the face rig contract (no code).** Settle the open questions below. Write the canonical channel set (proposal: the 52 ARKit names, a small viseme set of our own, gaze and blink channels), the per-head support list and mapping (Morrowind heads: read the real head meshes' text keys and morph controllers to find what exists: **verify**; Skyrim `.tri` phoneme and expression morphs; bone-based faces and how this interacts with 0070's fold to `Head`), and the `openeden.facecurves` v1 specification (fields, units, frame rate, quantisation, size caps). Acceptance: reviewed spec in `docs/spec/`, a table "channel x game head" with each cell marked verified or assumed.
- **b. The format and its library.** `components/face` (or the name the spec chooses): reader, canonical writer, binary packing as an Eden Archive entry, validator with located errors, and caps. Full loop (hostile input). Acceptance: JSON round-trips byte for byte; binary round-trips; every cap has a failing and a passing fixture; a deterministic fuzz test over the readers (as for the container); synthetic data only.
- **c. Importers and baseline generator.** (1) A generic importer: per-frame channel weights from a documented simple input (CSV/JSON) into `openeden.facecurves`; (2) an Audio2Face importer for whatever the author's installed SDK or tool exports (decided in stage a from the SDK's API documentation, which note 0034 did not read); (3) a Rhubarb Lip Sync importer (MIT, English-oriented) as the free baseline for authors without an NVIDIA GPU. Nothing from NVIDIA is added to the repository. Acceptance: round trip tests on synthetic and tiny hand-made inputs; the importer rejects malformed files with located errors.
- **d. Eden Studio panel** (editor module in the style of the other modules): waveform, channel curves, import, "run external generator" with a configured command line (the author installs the tool; Studio only starts a process and reads its output), preview on a head mesh using the mesh viewer (task 0043). Acceptance: scripted open, import, edit and save of a curve file; the preview shows mouth movement on a stock head.
- **e. Runtime playback.** A face layer (`[Face] speech curves`, off by default) that samples curves at the voice stream's position and drives the head's channels through the mapping; fallback order: curves, then viseme curves from a baseline generator, then the existing loudness drive. Full loop (renderer/animation). Acceptance: with curves the mouth follows the file (measured by a trace of channel values against the curve at the stream clock, within one frame); with no curves or the setting off the frame trace equals the commit before; frame-time cost of 20 channels on 4 talking NPCs measured and recorded.
- **f. Packaging and the local bake.** The compat pack's layout for face data: channel mappings (our own data) always; curves only for audio we may ship (see below); a command `openmw --bake-face` that generates curves for the user's installed voice lines with the author-installed generator and caches them under `openeden/face/` as derived data (staleness digest, status file, same pattern as the LOD bakes). Acceptance: byte-identical re-run of the importer stage; a curve for changed audio is ignored; no curve file in the repository except synthetic fixtures.

## Shipping rule (proposal, awaiting toddhow)

1. Curves for audio we have the rights to ship (our own recordings, text-to-speech from a permissively licensed engine, volunteer or mod-author lines under a permissive licence): may ship in a compatibility pack or a mod.
2. Curves for Bethesda voice lines: not shipped; the player generates them locally from their own installed audio (stage f).
3. Any change to rule 2 waits for a lawyer's answer to the questions in note 0034 section 4.

## Acceptance criteria

- [ ] Stage a: spec and table reviewed by toddhow; the legal questions are listed in the spec with the decision recorded.
- [ ] With every new setting at its default the game behaves as before; compared by a recorded frame and talking-head trace (stage e).
- [ ] No NVIDIA code, weights or SDK files in the repository; the licence of every new dependency recorded in `docs/dependencies.md`.
- [ ] `edenvalidate`-style validation of every curve file; hostile-input tests pass.
- [ ] Linux gate and the Lua integration tests pass for the code stages.

Out of scope: training or fine-tuning any model; real-time (live) generation in the game; gaze and body coordination (CreativeMotion's face layer, other stages); face morph sculpting and head replacers (AdvancedBodies, later); languages other than those the chosen generator handles; text-to-speech.

## Open questions for toddhow (recommendation first)

1. **Is "Face2Animation" NVIDIA Audio2Face-3D (the Audio2X SDK)?** I found no product of the first name. Recommend: yes, treat it as Audio2Face-3D, with Audio2Emotion optional.
2. **Architecture:** C (our format and importer) as the contract, B (Studio starts an author-installed external tool) as the convenience, never A (bundling). Recommend: yes.
3. **Shipping rule:** curves for Bethesda audio are generated locally, never shipped, until a lawyer decides; curves for audio we have rights to may ship. This contradicts "ship them because they are original": generated curves are original only if the audio is. Recommend: accept the rule now, ask a lawyer later.
4. **Free baseline:** add the Rhubarb importer so authors without an NVIDIA GPU get visemes. Recommend: yes.
5. **Channel set:** ARKit 52 plus our own visemes plus gaze and blink, each head declaring what it supports. Recommend: yes.
6. **The face design document:** the section "Facial animation layer" in CreativeMotion's Living Design Doc is the only one I found; there is no separate facial format doc or Studio module spec. Is there another document?
7. **Emotion input:** Audio2Emotion's licence limits it to Audio2Face and forbids emotion recognition. Recommend: do not depend on it; allow an author-set emotion per line.
8. **NVIDIA GPU, CUDA 12.8 to 12.9 and TensorRT 10.13+ required for authors who use Audio2Face.** Acceptable? Recommend: yes, since it is an authoring tool only.
9. **Voice audio quality:** NVIDIA warns poor audio gives inaccurate lips; Morrowind's `.mp3` lines may be poor. Recommend: measure in stage c on a few lines before promising quality.
10. **Order against other work:** after 0070 stage a (bone measurement) and before face bones are folded for any head that uses them. Recommend: yes.
