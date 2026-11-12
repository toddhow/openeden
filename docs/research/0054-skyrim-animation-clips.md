# Research note 0054: Skyrim SE animation clips (`.hkx`): can they be used, legally and technically

2026-11-11. Docs only. Item 7 of section 8 and decision D4 of `docs/design/skyrim-se-implementation-plan.md` (sections 4.11,
4.12, 6), and decision 0017 (neutral actors and items), whose stage N5-4 needs a clip request interface. Nothing was built,
committed or run in the repository; the measurements below used a throwaway Python script outside the repository
(scratchpad) that reads the user's own `Skyrim - Animations.bsa` and decompresses entries in memory. **Aggregates only:**
counts, sizes, header fields and class-name presence. No game file was copied anywhere, no clip content is quoted, no path
list is kept.

Provenance marks: **MEASURED** = read from the user's installed data this session. **READ** = read in a repository file this
session. **RECALLED-VERIFY** = from memory of public descriptions; unverified, and must be checked before a design depends on
it. No web page was fetched. I read no Havok SDK header and no decompiled code, and none of the format descriptions below
come from either (they come from memory of third-party community documentation, which I cannot cite file by file; that is
exactly why they are marked).

## 1. Recommendation (short)

Do a time-boxed spike of option (a), a converter that the user runs on their own files and that writes CreativeMotion clips
outside the repository, but write it as our own code in the repository (the converter's source is ours, its output is not
committed), built from the data and from third-party format descriptions only. Do not take any tool that links the Havok SDK
as a dependency, and do not copy any reader whose provenance cannot be shown as SDK-free. Keep option (c), original
CreativeMotion clips, as the fallback and as the only route for anything the spike cannot prove clean.

## 2. Formats

### 2.1 What the installed data contains (MEASURED)

`Skyrim - Animations.bsa` (BSA v105, LZ4 frames; 8,979 entries) holds **7,699 `.hkx` files**. The only other archive of
the installed set with any `.hkx` is a Creation Club one (`ccbgssse001-fish`: 6). The two Meshes archives hold none. Also in
the animations archive: 1,221 `.txt` files (section 2.3) and 10 `.hkt`; 44 `.nif`.

Header of every one of the 7,699 files (all decompressed and read): **Havok packfile, magic `57E0E057 10C0C010`, file version
8, pointer size 8, little endian, content version string `hk_2010.2.0-r1`.** So the Special Edition files are the 64-bit
variant of the 2010.2 packfile, not tagfiles and not the 32-bit layout of Skyrim LE (RECALLED-VERIFY: LE uses pointer size 4
with the same version string; the plan's section 4.12 said the same, now measured for SE). The 6 Creation Club files start
with a different 8-byte signature (`1E0DB0CA CEFA11D0`) that I did not identify; whether it is another Havok container is
unknown (open item; 6 files, ignorable for the spike).

By role, from class names present in each file's class table plus its folder (a file lists a class only if it has an
instance of it; counts are files, not instances):

| Role | Files | Notes |
| --- | --- | --- |
| clips under an `animations` folder | 5,938 | 5,935 contain `hkaSplineCompressedAnimation`; 2 contain `hkaInterleavedUncompressedAnimation`; 5,937 contain `hkaAnimationBinding` |
| clips elsewhere (auxbones, dungeons, traps, clutter, magic, furniture...) | about 190 | 191 more spline-compressed animations in files not under `animations` |
| behaviour graphs | 423 (role by folder) | 428 files contain `hkbBehaviorGraph`, 425 `hkbStateMachine`, 300 `hkbClipGenerator`, 30 `BSSynchronizedClipGenerator` |
| character definitions | 311 | `hkbCharacterData`: names the skeleton, behaviour and animation lists |
| project files | about 450 | `hkbProjectData` |
| skeleton / ragdoll rigs (`character assets`) | 61 | 59 `hkaSkeleton`, 47 `hkaRagdollInstance` (the ragdoll rig) |
| default reference frame (root motion) | 13 clip files | `hkaDefaultAnimatedReferenceFrame` |
| annotation track class | 2 clip files | annotations are not mainly stored this way; see 2.3 |

Where the clips belong, by actor folder (files): `actors/character` 2,654 (the humanoid, player and NPCs, including 1st
person), dlc02 (Dragonborn) about 600, dungeons 382, dlc01 (Dawnguard) about 390, auxbones 327, traps 280, draugr 225,
dragon 183, kill moves 187, werewolf 125, falmer 121, dragon priest 117. A typical clip is small: **median 5.0 KB
compressed in the archive (about 14 to 21 KB for a full-body humanoid locomotion or idle clip), largest 256 KB.**

The humanoid skeleton is `character assets/skeleton.hkx` (24 KB) next to the NIF skeleton (`skeleton.nif`) that note 0036
measured. They describe the same bones twice: the NIF for rendering and skinning, the HKX for animation and ragdoll.

### 2.2 What is known publicly about the container (RECALLED-VERIFY)

A packfile is a header (magic, file version, layout rules: pointer size, endianness, padding and base-class options, then
a section count and a content-version string), a table of sections (`__classnames__`, `__types__`, `__data__`), and per
section three fix-up tables (local, global, virtual) that turn stored offsets into pointers and name each object's class.
Objects are plain structs laid out by the class definitions of the SDK version; a reader needs the layout of each class it
wants (field offsets and sizes for the 64-bit layout). `hk_2010.2.0-r1` has no embedded type information that describes
layouts, so the layouts must come from somewhere else; this is the legal crux (section 3). The newer tagfile format (2012+)
is self-describing, which is why open readers support it more easily; Skyrim does not use it.

Classes a clip reader needs: `hkaSkeleton` (bone names, parent indices, reference pose), `hkaSplineCompressedAnimation`,
`hkaAnimationBinding` (which animation track maps to which skeleton bone; also float tracks), and optionally
`hkaDefaultAnimatedReferenceFrame`. A skeleton reader needs only `hkaSkeleton`.

### 2.3 Text sidecars: root motion and annotations (MEASURED, structure RECALLED-VERIFY)

`meshes/animationdata/` and `meshes/animationsetdata/` hold plain text (324 files in `animationdata`, 304 + 304 + 103 in the
default male, female and first-person set folders, 39 in `boundanims`). I looked at the first bytes of three: a project
list (`dirlist.txt`), a per-project file that names the behaviour, character and skeleton `.hkx` and then lists each clip
with its length and its annotation events as `name:time` text (for example a `clipEnd:6.65767` and a `SoundPlay.xxx:0.333`
style line), and a `boundanims` file of per-clip duration and root-motion samples (rows of `time x y z`, and a
rotation row `time x y z w`). Meaning of each field is inferred from three samples, not verified. This matters: **the
root motion and the annotation events of a Skyrim clip are available as text with no Havok involvement at all**, which
removes two of the plan's listed risks (root-motion extraction and annotation tracks) for the clips this data covers.

## 3. Clip compression: what is public, from where, and under what terms

### 3.1 The scheme (RECALLED-VERIFY, all of it)

Skyrim stores clips as `hkaSplineCompressedAnimation`: the clip is cut into blocks of up to 256 frames; per block each
transform track (translation, rotation, scale) has a small mask header saying, per component, whether it is static
(one stored value), absent (identity) or animated; animated components are uniform-knot-vector cubic B-splines with 8-bit
knots and control points quantised by type. Rotations use one of a few quantisation widths (a 32-bit polar, a 40-bit and a
48-bit smallest-three style, a 16-bit), translations and scales 8 or 16 bits per axis scaled by a stored min/max. The
decoder needs the block table, the per-block track offsets, the knot/degree/evaluation rules and the exact bit layouts.
I am confident of the overall shape and not of any bit layout; each bit layout must be established from a source and
confirmed against data.

### 3.2 Where the description comes from, and why that is the legal question

The authoritative description is the Havok SDK's headers and source (`hkaSplineCompressedAnimation`, its decompression
routines). Those are proprietary, covered by a licence that forbids the redistribution and, in the usual reading, the
disclosure of the SDK; **the guardrails forbid us from using them**. All public knowledge of the scheme descends from one of:
(1) the SDK itself, read by tool authors who held a licence (many Skyrim tools were written by people who had the SDK
through Bethesda's modding programme or the freely distributed older `Havok Content Tools`; provenance of that
knowledge in the derived tools is generally undocumented); (2) reverse engineering of the game executable's decoder or of the
data files; (3) clean-room analysis of the data alone. I cannot tell which a given public description is from. That is the
core risk of option (b): a reader written from a community description may be derived from the SDK and we could not know.
It can be reduced by (a) writing our own code, (b) validating it against the data rather than trusting any source, (c)
keeping the log of what was learned from the data versus from a named public description, and (d) preferring
descriptions that are themselves reverse engineered from data.

### 3.3 Existing tools (licence and provenance; none could be verified this session)

| Tool | What it is | Licence and provenance (all RECALLED-VERIFY) | Use |
| --- | --- | --- | --- |
| **hkxcmd** (figment) | Command-line converter HKX <-> XML/KF/etc. for Skyrim LE | Built on the Havok SDK (links Havok libraries); the source was published under an unclear/permissive notice but cannot be built without the SDK | Not a dependency (needs SDK). Not a reference we may read for layouts (derived from the SDK) |
| **ck-cmd** (figment) | Successor, bundles hkxcmd functions, SSE support | As above; GPL-flavoured source with Havok parts | Same |
| **HKX2 / HKX2E** (C# libraries) | Havok class definitions generated from Havok XML class dumps, pack/tag reader and writer; HKX2E is the fork used by Pandora | Open source (MIT or GPL, uncertain); the class definitions come from the SDK's XML reflection dumps, i.e. SDK-derived material | Reference of the container only at best; SDK-derived layouts make it unsafe to copy |
| **HavokLib** (Lukas Cone, "PredatorCZ") | C++ library reading Havok packfiles and decoding spline-compressed animation without the Havok SDK | GPL-3.0 as I recall (GPL-compatible with OpenEden); written, per its README, without the SDK, but how its layouts were obtained is unrecorded | The best candidate as a *cross-check oracle* of a decoded clip; as source reference only after reading its licence and history; not a dependency without a provenance review |
| **DSAnimStudio and SoulsFormats** (FromSoftware tooling) | Decode Havok 2010-2014 (Souls games) spline animation in C# | MIT/GPL-style, derived from reverse engineering of those games | Reference for the scheme, same Havok version family (2010 vs 2014 differences unknown) |
| **hkanno / "hkanno64"** | Edit annotation tracks in clips | Calls Havok libraries via hkxcmd-style code | Not usable; annotations here are text anyway (2.3) |
| **Nemesis, Pandora Behaviour Engine, FNIS** | Behaviour-graph patchers (Nemesis: closed/mixed, Pandora: open, FNIS: closed) | Pandora is open (GPL-3 as I recall) | Not clip tools; relevant to section 6 only |
| **Open Animation Replacer / Dynamic Animation Replacer** | Condition layers that choose which clip file plays in a slot | OAR is an SKSE plugin using CommonLibSSE; it reads JSON conditions, not Havok data | Section 6; its config schema is data we can read without Havok |
| **OpenMW** (`components/esm4`, upstream audit note 0051) | ESM4 records, NIF reader, no `.hkx` | Note 0051 found no animation work in the audited changes; I know of none | Nothing to reuse here |
| **Blender / NifSkope exporters** | Community Blender add-ons that import HKX via hkxcmd; the fo76utils NifSkope fork (note 0044) is a NIF tool and does not read `.hkx` | Add-ons are GPL, they call hkxcmd | Could serve as an *oracle workflow*: the user's own tool turns a clip into a format we read |

Where I cannot verify a licence I say so; do not rely on this table for anything beyond picking what to read first. A
licence-reading step (READMEs and `LICENSE` files only, no source) is part of the spike (section 5, step 1).

## 4. Options

| | (a) User-run converter, our own code, output outside the repo | (b) Clean-room reader inside the engine | (c) Original CreativeMotion clips only | (d1) Use an existing tool for the user's one-time conversion | (d2) Ship no animation: procedural only |
| --- | --- | --- | --- | --- | --- |
| Gives | Skyrim's actual idle/walk/run/attack/magic/death clips on our graph | The same, live from the game's archives, no conversion step | Own clip set, no Skyrim feel | Same clips, conversion delegated to a tool the user installs | Breathing/rest pose only (the plan's N5-4 placeholder) |
| Legal risk | Medium-low: we write the decoder (see 3.2), output is derived from the user's files and stays in their folder; same pattern as the bakers | Medium, higher than (a): the decoder ships in the engine, so any provenance flaw ships too | None (our own work) | Lowest for us: we do not touch Havok knowledge; the user takes the tool's licence | None |
| Effort (estimates, plus or minus 50 percent) | Spike 6 to 10 days; full 25 to 45 (plan's figure) | The same code plus runtime integration, caching and fuzzing: +10 to 20 days over (a) | Large: dozens of clips per skeleton to author, and creature, dragon and mount sets; weeks to months of animator time | 2 to 4 days for an importer of the tool's output (glTF/KF) | 0 |
| Fit | Matches our bakers; allows user-side fixes | Allows reading mod `.hkx` replacers live | Fits CreativeMotion's style layer and clip sidecars (`opas.clipsidecar`) | Depends on a closed/SDK-linked tool being installed | Not a game |
| Weak point | Needs the decoder anyway, so it inherits the whole of risk 3.2 | Same risk, plus shipping it | No community-animation compatibility; heavy cost | Fragile toolchain; hkxcmd needs the SDK; SSE support partial | Not acceptable past the first slice |

(a) and (b) are the same decoder; the difference is where it runs and what ships. The recommendation is to write the decoder as a
library with no engine dependency (as `components/nifscene` was), first used by the converter (a), and only afterwards
decide whether the engine may link the library (b) once provenance is judged. (d1) is the cheapest clean way to learn if
the pipeline works at all, and is a sensible *oracle step* in the spike (the same method as the NifSkope glTF export in task
0082): toddhow, who has these tools, converts one clip with his own tool and gives us the result to compare against.

## 5. Spike plan

Goal: decide between (a)/(b), (c) and (d1) with one clip. Everything below stays in the work folder
(`I:\Projects\OpenEden-work\hkxspike`); nothing is committed except the research notes and a clean-room log.

1. **Licence reading (half a day).** Read the `LICENSE` and README of HavokLib, HKX2/HKX2E and DSAnimStudio only. Record, per
   tool, licence text, stated provenance, and whether the author says the SDK was used. Result: a table in this note's
   follow-up, with a verdict on each as reference, oracle, or off-limits.
2. **Container reader (1 to 2 days).** Our own code, in a library target with no engine dependency (proposed
   `components/havokpack`, layer `formats`). Reads header, section table and the three fix-up kinds; locates objects and
   their class names. Tests on generated packfiles plus a fuzz over the user's 7,699 headers (aggregate-only results). Hostile
   input rules like the BSA reader (caps, located errors). Success: all 7,699 files parse to an object list without error and
   the class counts match section 2.1.
3. **Skeleton (1 day).** Read `hkaSkeleton` (bone names, parents, reference pose) from `character assets/skeleton.hkx`.
   Class layout learned from the data (offsets of name strings, parent indices, pose array) and checked by structure
   (names resolve, the parent array is a tree, poses are unit quaternions). Compare the bone names to the NIF skeleton of
   note 0036: success when every Havok bone has a NIF node with the same name (or the mismatches are explained).
4. **Spline decoder (3 to 5 days).** One clip: a humanoid `mt_idle` (about 21 KB, measured). Establish the block header and
   mask layout from data and the recalled description, then decode; validate by: first and last frame close to each other
   for a loop; rotations unit length; bones stay within plausible lengths relative to the skeleton; a smooth derivative (no
   spikes); and equality against the oracle (step 6). The 2 `hkaInterleavedUncompressedAnimation` clips need no decoding and
   give a ground truth for the binding and track layout independent of the spline code.
5. **Export (1 day).** Write a CreativeMotion `Clip` as glTF (read through `anim/gltf`) with the root-motion row from the text
   sidecar (2.3) and the annotation events as an `opas.clipsidecar` JSON.
6. **Oracle (toddhow, 1 hour).** He converts the same clip with a tool of his choice and hands over the result. We compare per
   bone per frame (angle error in degrees).
7. **View (1 to 2 days).** Success: **one idle clip of the vanilla Skyrim skeleton plays in Eden Studio's mesh viewer** on the
   bind-posed body (`malebody_0.nif` as in task 0086 stage e), looping, viewed in frames. Whether the viewer can take a
   per-frame pose is not known (its Skyrim path is a static bind-pose drawing, plan section 3); if it cannot, the spike
   adds a minimal pose hook or renders a strip of frames as lines (decision: spike-local, not API).
8. **Report.** Per option: did the decode match the oracle (max rotation error), time taken, provenance log complete,
   and a go/no-go.

**Success criteria:** the idle clip plays and its per-bone error against the oracle is under 0.5 degrees and 1 mm
(target), the provenance log can name the source of every layout fact, and the licence table has no unresolved row for a
tool we read.

**Failure criteria (switch to (c)):** a layout fact cannot be established from the data or from a source of known
provenance within the box, or the decoded motion cannot be made to match the oracle.

**Estimate:** 10 to 15 working days with steps 1 to 8 (my number, plus or minus 50 percent), against the plan's 25 to
45 for the full converter. Split in two: steps 1 to 4 (about 6 to 9 days) are the go/no-go; 5 to 7 only after a go.

## 6. Behaviour graphs

The plan (4.12, D4) recommends our own graph. Reasons: (1) `hkbBehaviorGraph` semantics (events, variables, transitions,
blend trees, generators, modifiers such as the hand IK and ragdoll drivers) are defined by the Havok runtime, and a faithful
implementation is a large reverse-engineering job whose provenance is hard to prove clean (plan risk 1). I measured
the scale only coarsely: 428 files contain a behaviour graph, 425 contain a state machine, 300 a clip generator; a
single humanoid graph is a few hundred nodes (RECALLED-VERIFY). (2) CreativeMotion already has a graph
(`components/creativemotion/anim/graph`, task 0020 stage C3d), a style layer, transition rules and clip sidecars (READ:
`anim/CLAUDE.md`), so the engine's job is hosting one graph for the Skyrim skeleton. (3) The three layers are separable:
clips (data), the graph (selection and blending), the events (text, 2.3).

**What is lost.** Mods that replace or add *behaviour graphs* work only through the Havok runtime: Nemesis, Pandora
and FNIS-generated behaviours, custom weapon types and their state machines, the behaviour-based combat and
movement overhauls, and anything that adds new graph variables or events and expects scripts and SKSE plugins to
drive them. Mods that replace only clips (the large body of "animation replacer" mods) can work, provided our graph
asks for slots by the names the vanilla graph uses (`IdleStart`, `attackStart`, ...; RECALLED-VERIFY). Animations that
depend on graph-driven IK (attached hand/foot IK modifiers) become CreativeMotion's own foot planting and look-at
(task 0006, already built).

**What an OAR-style condition layer could look like later (not designed).** Open Animation Replacer picks, per animation
slot, the highest-priority replacement whose conditions hold (weapon type, keyword, perk, race, faction, location...)
(RECALLED-VERIFY; the OAR 3.0.0 config schema is JSON, read without Havok, and a separate skill in this workspace documents
it; I did not load it). In our design: the graph asks for a *slot* (a name, plus context); a data layer maps
(slot, conditions) to a clip path in the VFS; conditions are evaluated by the pack's condition engine (plan 4.20 / the
CTDA functions the Papyrus work already needs); priorities are data. DAR `_conditions.txt` folders convert to the same
form. Clips found this way are converted on first use by the same decoder (option (b)), or pre-converted by the user's
run of (a). This needs: the decoder, the condition engine, a VFS rule for replacer folders, and the graph's slot
names; none of it needs a behaviour graph. A mod that ships clips for an existing slot works without any graph reading.

## 7. Open questions for toddhow

1. **Is a converter that the user runs on their own files acceptable under the guardrails?** Reading: the guardrails
   forbid the Havok SDK, proprietary SDKs, decompiled code and game assets in the repository; a converter we write from
   inspection of the user's own data and from third-party descriptions, outputting to the user's folder, breaks none of
   them. The residual risk is provenance of the format knowledge (3.2), not the act of converting. This is not legal
   advice, and Bethesda's and Havok's terms for the user's installed copy are not something I read. *Recommendation:*
   yes, with a clean-room log per format fact and no SDK-derived material, as for `nif.xml`.
2. **May the decoder ship in the engine (option (b)) or must conversion stay a separate user step (option (a))?**
   *Recommendation:* start as (a); revisit only after the provenance log is complete.
3. **May the spike read the licence files (READMEs and `LICENSE` only) of HavokLib, HKX2 and DSAnimStudio, and must it
   avoid their source?** *Recommendation:* read licences and READMEs; read no source of anything that links the SDK; read
   HavokLib source only if its licence and stated provenance are clean, and then only to cross-check, not to copy.
4. **Will you run an oracle conversion on one clip with your own tool (step 6)?** *Recommendation:* yes; it is the
   only independent check available here.
5. **Placeholder policy.** Until the spike decides, N5-4 uses a generated idle (plan decision, 0017 question 6).
   *Recommendation:* keep it, labelled a placeholder, and do not build the locomotion graph before the go/no-go.
6. **Behaviour-graph mods are out (plan D4).** Confirm. *Recommendation:* confirm; record that clip-replacer mods are the
   supported kind, and put the OAR-style layer on the list after the Papyrus condition functions exist.
7. **The 6 Creation Club files with a different signature.** *Recommendation:* ignore until the main path works.
8. **Where does the decoder library live** (`components/havokpack`, layer `formats`, versus inside the Skyrim pack)?
   *Recommendation:* in `components`, game-neutral (Fallout 4 uses another Havok version, so the name should not promise
   more than the 2010 packfile), consumed by `compat_skyrimse`.

## 8. Not verified

Every bit layout of the spline format, the Havok class layouts for the 64-bit 2010.2 build, the meaning of the sidecar
text fields beyond three samples, all tool licences and provenance in 3.3, the Havok unit scale, the event names, the
OAR condition set, whether Eden Studio's mesh viewer can take a per-frame pose, and the identity of the 6 unrecognised
Creation Club files. The measurements of section 2.1 are exact for the installed copy (one machine, one version of the
game, the Creation Club content the user owns, no mods).
