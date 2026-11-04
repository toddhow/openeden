# Research note 0034: NVIDIA's speech-to-face animation (Audio2Face-3D) for Eden Studio, and what OpenEden needs on its own side

Date: 2026-11-03. Author: Claude (research only; no code was written, no model or SDK was downloaded, no game was run).
Status: findings for toddhow. Not legal advice: every licence point below is a question for a lawyer, not a conclusion.

How to read it: **[V]** = read on the page named, on 2026-11-03. **[S]** = seen only in a search-result summary, not on the primary page. **[A]** = assumption or general knowledge, not checked. **[repo]** = read in this repository or in `I:\Projects\CreativeMotion`.

## 1. What toddhow means

toddhow wrote "Face2Animation". No NVIDIA product of that name turned up in any search **[V: no hit in three searches]**. The match is **NVIDIA Audio2Face** (the current open release is **Audio2Face-3D**, part of what NVIDIA calls the "Audio2X SDK"), with an optional second model, **Audio2Emotion**, and a hosted form, the **Audio2Face-3D NIM** microservice (NVIDIA ACE). CreativeMotion's own design doc already names it that way: `I:\Projects\CreativeMotion\docs\Open Procedural Animation System — Living Design Doc.md`, section "Facial animation layer" **[repo]**.

## 2. What exists today (sources)

| Piece | What it is | Licence as stated | Source |
| --- | --- | --- | --- |
| Audio2Face-3D SDK | C++/CUDA library, `audio2x-sdk` shared library, samples, tests, benchmarks; Audio2Emotion + Audio2Face | **MIT**, Copyright NVIDIA Corporation 2025, plain MIT text with no extra terms **[V: raw `LICENSE.txt`]** | https://github.com/NVIDIA/Audio2Face-3D-SDK and https://raw.githubusercontent.com/NVIDIA/Audio2Face-3D-SDK/main/LICENSE.txt |
| Audio2Face-3D models v3.0, v2.3 (Mark, Claire, James) | Neural networks; v3.0 transformer + diffusion with a HuBERT audio encoder, about 180 million parameters, published on Hugging Face 2025-09-24; v2.3 "Mark" regression network, about 18.6 million parameters | **NVIDIA Open Model License** (model cards say "ready for commercial/non-commercial use") **[V: model cards]** | https://huggingface.co/nvidia/Audio2Face-3D-v3.0 , https://huggingface.co/nvidia/Audio2Face-3D-v2.3-Mark |
| Audio2Emotion v2.2 | Wav2Vec2-based classifier, about 310 million parameters, outputs six emotion probabilities (anger, disgust, fear, joy, neutral, sadness); access is gated (log in and accept terms) | **Separate** "License Agreement for NVIDIA Audio2Emotion Model for Use with Audio2Face Project": only in connection with Audio2Face; **may not be used for emotion recognition**. The card's licence link points at the v3.0 text, so the exact terms for v2.2 are unconfirmed **[V]** | https://huggingface.co/nvidia/Audio2Emotion-v2.2 |
| Training framework and example dataset | Open-sourced according to the paper | Training framework Apache per a third-party summary **[S]**; sample dataset "evaluation only" per the same summary **[S]**, unverified | https://arxiv.org/abs/2508.16401 (statement that networks, SDK, training framework and example dataset are open-sourced **[V]**), https://www.sourcepulse.org/projects/29194877 **[S]** |
| Audio2Face-3D NIM (microservice container) | Hosted/containerised form | Container: NVIDIA Software License plus Product-Specific Terms for AI Products; models: NVIDIA Open Model License; Audio2Emotion: its own licence. One version page says the container is "ready for non-commercial use" while the main page does not: **conflict, unresolved** **[V: governing-terms page; conflict seen in search summary]** | https://catalog.ngc.nvidia.com/orgs/nim/nvidia/containers/audio2face-3d/-/governing-terms |
| Maya-ACE, Unreal plugin | DCC integrations | MIT per a third-party summary **[S]** | https://www.sourcepulse.org/projects/29194877 |

Runtime facts **[V: SDK README and model cards]**:

- Platforms: Windows 10/11 (Visual Studio 2022+) and Linux (Ubuntu 20.04+). The models list Pascal, Turing, Ampere, Lovelace, Hopper and Blackwell GPUs.
- Needs an **NVIDIA GPU**: CUDA at least 12.8 and below 13.0 (12.9 recommended), **TensorRT** at least 10.13 and below 11.0; 4 GB or more GPU memory and 8 GB or more RAM recommended; Python 3.8 to 3.10 for the model download and test-data scripts; models are downloaded with scripts and converted to TensorRT engines on the user's machine.
- Input: mono audio resampled to 16 kHz; v3.0 also takes an emotion label. Output: v3.0 and v2.3 cards describe 2D/1D float arrays for skin, tongue, jaw and eyeball motion; they do **not** say whether the output is vertex positions or blendshapes **[V]**.
- The SDK's own samples produce vertex positions exportable to a `.bin` file **[V: README]**. A blendshape solve (GPU or CPU thread pool) and offline batch use with multi-track executors are described in the SDK documentation summary **[S]**; the paper says blendshape decomposition after post-processing produces ARKit blendshape weights usable on custom blendshape rigs **[S via search; the arXiv page itself did not state it]**. An NVIDIA forum reply says the blendshape solve is based on the number of blendshapes the target has **[S]**. **I did not read the SDK's API documentation**, so the exact way to get per-frame blendshape weights out, and the channel naming, are unverified.
- Determinism and latency: the model cards give **no determinism claim and no latency numbers** (the metrics are listed without values) **[V]**. The diffusion model (v3.0) is probably not bit-identical between runs or between GPUs/drivers **[A]**. Therefore generated animation must be treated as authored data that is saved, never regenerated at run time.
- Training data statement (v3.0 and v2.3): under 10,000 hours of audio paired with 3D face motion captured from humans with consent, collected by NVIDIA **[V]**. The v2.3 actor-named models ("Mark" and so on): whether a real person's likeness is embedded is **not stated** **[V]**.
- Limitation stated by NVIDIA: poor-quality audio may produce inaccurate lip poses **[V]**. Morrowind's voice files are old `.mp3` recordings **[A: quality not measured]**.

## 3. Licences: what each architecture means for this repository

### 3.1 What the licences say

NVIDIA Open Model License Agreement, version dated 2025-10-24 **[V: https://www.nvidia.com/en-us/agreements/enterprise-software/nvidia-open-model-license]**, in plain words:

- Grants a worldwide, royalty-free right to use, reproduce, modify, create derivative works from, sell, distribute and import the model. The grant is revocable under certain conditions.
- Redistribution needs: a copy of the agreement and a notice text ("Licensed by NVIDIA Corporation under the NVIDIA Open Model License"). You may add your own terms for your modifications.
- Automatic termination if guardrails are bypassed or weakened without a similar replacement, and if you sue alleging that the model infringes copyright or patents. Use must also fit NVIDIA's Trustworthy AI terms.
- **NVIDIA claims no ownership of outputs; the user is responsible for outputs.** NVIDIA owns the model and its own derivatives; you own yours, subject to NVIDIA's underlying rights.
- NVIDIA may update the agreement; if you do not accept the update you must stop using and distributing the model and derivatives. You indemnify NVIDIA against third-party claims from your use or distribution. Governing law: Delaware, courts in Santa Clara County, California.

### 3.2 Against this project's guardrails (`CLAUDE.md`)

- GPL-3.0-or-later project; "no proprietary code or SDKs"; licence checked before adding a dependency; compatibility packs are GPL-3.0-or-later.
- The SDK source is MIT, which is GPL-compatible **[V]**. But running it needs **CUDA and TensorRT**, which are NVIDIA proprietary SDKs **[A: their redistribution terms were not read]**. Bundling them in Eden Studio would break the "no proprietary SDKs" guardrail and force an NVIDIA GPU on every Studio user.
- The model weights are not GPL: the Open Model License has its own conditions (notice file, termination clauses, update clause, indemnity). They would have to stay a separate file under their own licence. Whether that counts as an aggregate or a combined work with a GPL program is a lawyer question **[A]**.

### 3.3 The three architecture options

| Option | What it means | Licence consequence | Verdict |
| --- | --- | --- | --- |
| **A. Bundle it in Eden Studio** | Studio contains the SDK, CUDA/TensorRT, and ships or downloads the models | NVIDIA proprietary runtime inside a GPL tool; model licence conditions imposed on every Studio distribution; hard NVIDIA-GPU and CUDA 12.8-12.9 pin | **Do not.** Breaks a guardrail. |
| **B. External tool the author installs; Studio runs it as a separate process and imports its output** | No NVIDIA code or weights in our repo; Studio has a documented command-line contract and a "run generator" button | The author installs NVIDIA's pieces under NVIDIA's terms; our repository contains only our importer | **Recommended as the convenience path.** Needs an NVIDIA GPU on the author's machine only. |
| **C. Import-only** | We define our own curve format (`openeden.facecurves`) and a documented importer; Audio2Face is one producer among several (Rhubarb, hand animation, a future model) | Our format and importer are our own work under GPL; any generator is outside the repository | **Recommended as the contract.** B is built on top of C. |

## 4. The "original animations" claim, thought through

toddhow's plan: ship generated facial animation with the compatibility packs "since they would be original".

- The curves are generated from an **audio file**. If that audio is Bethesda's voice acting, the curves are derived from a copyrighted work, so "original" holds only if the audio is ours or properly licensed. NVIDIA's terms do not decide this: they say NVIDIA claims nothing in the outputs and the user is responsible for them **[V]**.
- The project's own rule for bakes already says the opposite for derived data: "Bakes of Morrowind cells are derived from the user's game data and are never committed" (`CLAUDE.md`, changelog 2026-11-01), and "No game assets" is a guardrail. A curve file keyed to a specific Bethesda voice line is derived from a game asset. **By the same rule it would not ship**; it would be **generated on the user's machine** from the user's installed audio and cached like the LOD bakes. CreativeMotion's own design doc keeps this question open ("confirm shipping generated curves (never Bethesda audio) is acceptable", line 507 of the Living Design Doc **[repo]**).
- What a lawyer should be asked (plainly, not as legal advice):
  1. Is a time series of mouth-shape weights computed from a copyrighted recording a derivative work of that recording?
  2. Does it matter that the curves cannot reproduce the audio, and that the spoken words are separately in the game's data files?
  3. Does Bethesda's mod/EULA policy for each game say anything about derived data?
  4. Are purely machine-generated curves copyrightable at all (if not, "original" is the wrong word, and others could copy them)?
  5. Does the NVIDIA Open Model License add any obligation for curves shipped in a GPL package (notice, attribution)?
- A safe-by-construction design (my suggestion, [A]): (1) curves for voice lines we have rights to (our own recorded or text-to-speech samples, mod authors' own voice lines, volunteer lines under a permissive licence) can ship; (2) for Bethesda lines the pack ships **no curves**, only the mapping and the generator profile, and the player runs a local "generate for my game" bake, exactly like the LOD bakes; (3) the curve file records the audio's name, length and a hash, so a pack never contains audio and a curve is ignored if the user's audio differs.

## 5. What OpenEden has and needs on its own side

### 5.1 What exists

- **Voice playback:** `DialogueManager::say` (`apps/openmw/mwdialogue/dialoguemanagerimp.cpp:625`) calls `SoundManager::say(actor, correctSoundPath(info->mSound))` (line 658; `apps/openmw/mwsound/soundmanagerimp.cpp:316`) which plays the file through a decoder stream (`playVoice`, line 202). `sayActive` tells whether an actor is speaking **[repo]**. Morrowind voice lines are `.mp3` (the path comes from the INFO record) **[A for the codec]**.
- **Current lip movement:** a loudness drive. `MWSound` computes 20 loudness values per second (`sLoudnessFPS` in `openaloutput.cpp:39`, `mwsound/loudness.*`); `MWRender::Npc::setFacialState(enable, talking, loudness)` (`apps/openmw/mwrender/npc.cpp:967`) feeds `MWAnim::Face` (`components/mwanimation/face.cpp`), which moves one "talk" range of the head's animation by `loudness * 2` and plays blinks on a random timer **[repo]**. Morrowind heads therefore have, as far as this code shows, only talk and blink channels (reading text-key tags with `Face::loadTags`) **[A: not checked on the real head meshes; verify]**.
- **CreativeMotion's facial design:** found: section "Facial animation layer" (lines 215 to 252) of `I:\Projects\CreativeMotion\docs\Open Procedural Animation System — Living Design Doc.md` **[repo]**. It sets the priority "procedural face life first, offline speech curves second, better shapes third", describes Skyrim faces as `.tri` morph targets, plans "run every voice line through the generator once, offline; convert output to weights for Skyrim's existing morph shapes; ship only the generated curves, never the audio; an SKSE plugin plays the curves", and notes that a richer rig "touches every NPC". **Not found:** a separate facial format document (`docs/formats/` has none), a task spec for the face layer, or an Eden Studio face-module spec. OpenEden's `docs/design/eden-studio-plan.md` has only the panel idea (item 5 "Facial Expression & Speech Timeline": phoneme and FACS curves, gaze, blinks, emotion blends) **[repo]**. If toddhow means another document, it is not in the two repositories I read.
- **Studio and body plans:** the Studio plan expects CreativeMotion to own the face layer (section 5.1); AdvancedBodies puts face morphs later, on the head part only, welded to the neck (`advancedbodies-in-openeden.md` section 7.2 item 5); task 0070 folds face bones to `Head` for now.

### 5.2 What is needed

1. **Face rig contract.** A head declares which channels it supports. A canonical channel set, so that different producers map into it: my proposal is the 52 ARKit blendshape names (the common interchange for Audio2Face-style output **[S]**) plus a small viseme set of our own, plus gaze and blink channels. Each head package maps channels to its own morph targets or bones, and a validator rule reports unmapped channels. For Morrowind heads (two real channels) the mapping layer reduces many channels to "talk" and "blink"; for Skyrim `.tri` heads it maps to phoneme and expression morphs; for bone-based faces it maps to the bones 0070 now folds.
2. **Curve format.** `openeden.facecurves` v1: canonical JSON for authoring (round-trips byte for byte, as `CLAUDE.md` requires for authored data) and a compact binary entry for shipping in an Eden Archive. Metadata: audio name, duration, audio hash, generator name and version, seed, channel set, frame rate. A rough size estimate **[A]**: a 3 s line at 30 fps with 20 active channels at 8 bits is about 1.8 KB; 100,000 lines about 180 MB before zstd. So the shipping form must be binary and sparse.
3. **Runtime playback.** A face layer that reads curves keyed to the voice stream's clock and falls back, in order, to viseme curves from a permissive generator and then to the present loudness drive. Cost: sampling 20 channels per talking NPC per frame is small **[A, measure in the stage]**. Off by default, with the vanilla loudness drive unchanged when off.
4. **Studio panel.** The "Facial Expression & Speech Timeline" (plan item 5): waveform, curves, a "run external generator" button (option B), import (option C), preview on a head mesh using the mesh viewer (task 0043).

## 6. Alternatives to compare

| Option | Licence | What it gives | Notes |
| --- | --- | --- | --- |
| **Rhubarb Lip Sync** (Daniel Wolf) | MIT; third-party parts under Boost, BSD and MIT licences (CMU Sphinx acoustic model, PocketSphinx, Flite, libogg/libvorbis, WebRTC, and so on) **[V: LICENSE.md]**. The author says generated data belongs to you (non-binding) **[V]** | Mouth shapes from speech; TSV, XML, JSON output; CLI on Windows, macOS, Linux **[V]** | Default recogniser is English only (PocketSphinx); a language-independent phonetic recogniser is less precise **[V]**. Made for 2D mouth shapes, so it gives about 9 shapes, not a full face. Good free baseline. |
| **Papagayo-NG** | GPL (a `gpl.txt` file is in the repo; version not stated) **[V]** | Manual phoneme alignment editor | Exports only to Moho; maintenance unclear **[V]**. A tool for hand work, not a generator. |
| **OVRLipSync** (Meta) | Proprietary; no licence text found **[V: only forum posts]** | 15 language-agnostic visemes from audio, offline precompute exists in the Unity plugin | Plugins for Unity and Unreal only; Meta marks sibling audio SDK parts end-of-life. Fails the "no proprietary SDKs" guardrail. Not usable. |
| **Montreal Forced Aligner** | MIT per package listings **[S]**; its acoustic models and dictionaries carry their own licences **[A, not checked]** | Phone-level timing from audio plus a transcript | Morrowind and Skyrim dialogue lines have transcripts in the game data, so alignment plus a phoneme-to-viseme table is a strong rule-based generator **[A]**. Heavy dependency (Kaldi). |
| **NVIDIA Audio2Face-3D** | SDK MIT, models Open Model License, CUDA and TensorRT proprietary | Neural full-face animation incl. tongue, jaw, eyes; emotion input | See section 3. Needs an NVIDIA GPU. Best quality of the group **[A]**. |
| Other ML lip-sync models | Not evaluated | | Out of scope for this note: each needs its own licence check. |

FaceFX / Bethesda `.lip` data in Skyrim and Fallout: not researched.

## 7. How this fits what exists

- **Task 0070 (skeleton mapping):** face bones fold to `Head` for now (toddhow's answer 6). Face curves drive morph targets or face bones through the head's channel map; the bone-folding stage must not remove bones that the face channel map needs, so the face stage has to be done before face bones are folded for any head that uses them.
- **AdvancedBodies:** the head is its own part with a weld at the neck; the face rig contract belongs to the head package. Face morphs later.
- **Compatibility packs:** a pack ships the channel mappings of its game's heads (our own data) and, only where allowed (section 4), curves.

## 8. Verified versus assumed

Verified from the pages named (2026-11-03): the SDK's MIT licence text; the SDK's platform and CUDA/TensorRT requirements; the model cards' licence name, parameter counts, input and output descriptions, GPU families and training-data statement; the Open Model License's main terms; Audio2Emotion's separate licence terms; Rhubarb's and Papagayo-NG's licence information; this repository's voice and face code paths; the CreativeMotion design section.

Seen only in search summaries or aggregators: the training framework's Apache licence; plugin licences; the SDK's blendshape solve and batch mode; the paper's ARKit statement; Montreal Forced Aligner's licence; the NIM's commercial-use conflict.

Assumed or not checked: CUDA and TensorRT redistribution terms; whether the output can be exported as ARKit weights in the SDK (I did not read the API docs); determinism across runs and GPUs; real Morrowind head channels; voice file quality; size estimates; the legal questions in section 4; Meta's current OVRLipSync terms; FaceFX `.lip` formats.

## 9. Addendum of 2026-11-03: does it run off NVIDIA hardware? (toddhow's decision "any GPU", background building)

Nothing above is rewritten. Sections 2 and 3 said the runtime needs an NVIDIA GPU, CUDA 12.8 to 12.9 and TensorRT 10.13 or later; that is still true **of NVIDIA's SDK**. New findings (read 2026-11-03; **[V]** = primary page, **[S]** = third-party or search summary, **[A]** = assumption):

- **The weights are published as ONNX, ungated, for the Audio2Face-3D models.** v3.0: `network.onnx` 725 MB in a 777 MB repository, plus per-identity `.npz` data for Claire, James and Mark, a config set and `trt_info.json` **[V: huggingface.co/nvidia/Audio2Face-3D-v3.0, file list and /api/models listing; tags: onnx, license: other, nvidia-open-model-license]**. v2.3 Mark: `network.onnx` 75.8 MB, repository 321 MB **[V]**. Audio2Emotion v2.2: `network.onnx` 1.27 GB, gated, own licence **[V]**.
- **What the SDK does with it:** `audio2x-common/scripts/audio2x/trt.py` copies the ONNX file and calls `trtexec --onnx=...` per GPU (`--device`, optional `--hardwareCompatibilityLevel=ampere+`, extra arguments from `trt_info.json`, no plugin libraries, no custom-operator handling, default precision) **[V: raw.githubusercontent.com/NVIDIA/Audio2Face-3D-SDK/main/audio2x-common/scripts/audio2x/trt.py]**. So an NVIDIA GPU is needed for the SDK and for the TensorRT engine build, not for obtaining the ONNX.
- **Network facts from the repositories' JSON files [V]:** v3.0 `network_info.json`: type diffusion, output "geometry" (skin 72,006 floats, tongue 16,806, jaw 15, eyes 4), 2 diffusion steps, 2 GRU layers of latent size 256, 16 kHz audio with a 16,000-sample buffer and 16,000 samples of padding each side, 30 centre frames with 15 truncated each side, ten emotion labels (amazement, anger, cheekiness, disgust, fear, grief, joy, outofbreath, pain, sadness); `trt_info.json` names the inputs `window`, `identity`, `emotion`, `input_latents` and `noise` (batch 1 to 8, optimum 3). v2.3 Mark: regression, 8,320-sample buffer with a 4,160-sample offset, 272 skin shapes over 61,520 vertices, 16-value implicit emotion plus the ten labels. The blendshape solve uses 52 poses per identity (config: L2 0.1, L1 0.1, temporal smoothing 0.05, symmetry 100; no pose names in the config) **[V]**.
- **Third-party non-NVIDIA evidence [S]:** the `a2f-rs` / `a2f-mlx` 0.2.0 project runs the v2.3.1 James network from NVIDIA's ONNX through ONNX Runtime's CPU provider and through an MLX (Apple Metal) graph; reported on an M1 Max for 8.03 s of speech: 18.8 s on the CPU provider and 2.8 s on Metal; CoreML and CUDA providers not implemented (docs.rs/crate/a2f-mlx/0.2.0). One NVIDIA developer-forum reply says the original Audio2Face application only runs on RTX GPUs (a different product, an older thread). A forum thread on an ARM64 machine found only the ONNX Runtime CPU provider available there.
- **Licence text read [V: nvidia.com/en-us/agreements/enterprise-software/nvidia-open-model-license/]:** grant to "reproduce, use, create derivative works of, make, have made, sell, offer for sale"; "Model" includes checkpoints and learnt weights; "An output is not a Derivative Model"; "You are and will be the owner of Your Derivative Models"; "NVIDIA claims no ownership rights in outputs"; a copy of the Agreement must be given to recipients if you distribute the Model; rights end automatically if you "bypass, disable, reduce the efficacy of, or circumvent any technical limitation, safety guardrail"; NVIDIA may update the Agreement; use must be consistent with NVIDIA's Trustworthy AI terms. **No text on hardware, platforms, format conversion or other vendors' systems was found.** The fetched text contains a "Built on NVIDIA Cosmos" notice line that looks specific to another NVIDIA model family; whether it applies to Audio2Face is unclear **[A]**; a lawyer should read the full text.
- **Not verified:** ONNX Runtime operator and opset support for v3.0 (HuBERT encoder, diffusion, GRU) on any provider, especially DirectML; numerical agreement with NVIDIA's TensorRT output; speed per second of audio on any non-NVIDIA provider for v3.0 (v2.3 CPU: one third-party sample, Apple silicon); ONNX Runtime and DirectML licences and redistribution terms (to be read before `docs/dependencies.md` is updated); whether the SDK's blendshape solve source can be ported as is.
- **Consequence for sections 3.3 and 4:** the recommendation in 3.3 (never bundle; C as contract; B as convenience) stays correct for what NVIDIA ships. toddhow's decision adds architecture **D**: our own ONNX Runtime front end that runs NVIDIA's published ONNX weights, which the user downloads under NVIDIA's licence (nothing bundled or hosted by us), subject to a hands-on feasibility gate. Task 0071 records it. Counts on toddhow's Morrowind install (read-only, 2026-11-03): 6,447 loose voice files, 128.4 MiB, 64 kbit/s mono (about 4.7 hours); the three BSA files contain no sound **[V counts; durations [A]]**.
