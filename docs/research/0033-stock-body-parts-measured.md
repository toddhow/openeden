# Research note 0033: Morrowind's stock body parts, measured (task 0069, stage a)

Date: 2026-11-03. Status: measurement and a recommendation; toddhow reviews the go/no-go table before stage b starts.

This note holds **aggregate numbers only**. The raw report (every record, model path and per-part figure) is derived
from the user's own installed game data and stays out of the repository, under
`I:\Projects\OpenEden-work\bodies\analysis\` (`bodies.json`, `bodies.txt`). The tool that makes it is
`openmw --analyse-bodies` (`apps/openmw/bodyanalyser.*` over `components/bodynif`); it reads everything and changes
nothing.

## 1. In plain language

Morrowind builds a person from about twenty small meshes. The question for a merge is how those meshes meet. The
answer is mixed:

- At **two** joints (forearm to wrist, knee to ankle) the two parts share their ring of vertices exactly, to within
  a thousandth of a unit, on every race and both sexes. Welding there is trivial and safe.
- At the **other** joints the parts are closed shells that overlap, or leave a small gap, so nothing coincides.
  There is nothing to weld; the overlap is what hides the joint in the stock game.
- Most parts are **not skinned at all**. They are rigid and hang on one bone each; only the chest and the hands are
  skinned (to a few bones). So the "weight policy" is mostly about turning rigid parts into skinned ones.

So the plan of note 0032 works, but its picture of "weld every seam" does not match the data. The recommendation in
section 6 changes stage b's scope.

## 2. How it was measured

- One run over the full Morrowind + Tribunal + Bloodmoon data (no other plugins), 20 units: 10 races, male and
  female; the two beast races are Argonian and Khajiit. The report is byte-identical on a second run.
- Parts are the ones `MWRender::getBodyParts` selects (the engine's own function), third person, no werewolf.
- Each part's NIF is read with the engine's skin filter (`components/bodynif/skinnedshape.*`, which mirrors
  `nif.cpp` `handleSkin` and `filterMatches`; the one difference is that equal weights are ordered by bone index,
  because the engine's `std::sort` leaves their order unspecified).
- **One space.** A rigid part's vertices are in its attach bone's space, so each is brought into the skeleton's
  space by the bone's world matrix at rest (`meshes/base_anim.nif`, `base_anim_female.nif` or `base_animkna.nif`:
  `Context::pickSkeleton`), after mirroring in x for a bone whose name contains "Left", which is what the engine does
  (`npc.cpp` `insertBoundedPart`, `decorate`: scale (-1, 1, 1)). A skinned part's vertices go to their bind pose:
  the weighted sum over the kept influences of (bone world matrix x the bone's skin transform x the skin's own
  transform) applied to the stored vertex. **This formula is read from the NIF format, not proved**: it must be
  confirmed in stage b by skinning the stock part at the bind pose with `Anim::SoftwareSkin` and comparing. Its
  plausibility check is that the placed chest and hands land where their neighbours are (chest z 81 to 119, neck 113
  to 125, groin 66 to 87, hands 63 to 77, wrists 75 to 87).
- A **boundary vertex** is a vertex on an edge that one triangle uses, after welding the part's own vertices that
  share a position (so a UV split is not a boundary). Two parts form a **seam**; a boundary vertex is on the seam
  when the other part has a boundary vertex within a window of 6 units. The report also pairs **all** vertices
  (a closed part has no boundary), inside the same window. The first run used a window of 2 and found overlaps
  larger than that.
- Distances are in Morrowind units (about 70 to the metre; a human is about 130 units tall).

## 3. The numbers

### 3.1 Bodies

| | Humanoid (16 units) | Beast (4 units) |
| --- | --- | --- |
| Vertices per body | 1,483 to 1,544 | 1,862 to 1,922 |
| Triangles per body | 2,234 to 2,338 | 2,502 to 2,610 |
| Distinct bones | 28 | 38 |
| Material keys | 12 | 12 to 13 |
| Part files | 10 | 9 |
| Bytes of part files | 66.7 to 70.0 kB | 88.7 to 92.3 kB |
| Parts read | 19 of 19 slots | 20 (the tail is one of them) |
| Skinned parts | 3 (chest, two hands) | 6 |
| Rigid parts | 16 | 14 |
| Head records per race and sex | 3 to 18 (Dark Elf male 18, Nord 13, Orc female 3) | 3 to 4 |

Every part was read; no file failed; no vertex has more than four influences before the cut (so the cut loses
nothing); every skinned vertex's kept weights sum to 1 within 1e-3 (0 exceptions). 111 of the 384 placed parts
have a non-identity node transform (the merger must fold them in, as the reader does); 136 parts are mirrored left
parts. A single `skins.nif` holds seven shapes: the chest and three shapes for each hand, selected by the skin filter (a
beast race's file holds ten: it adds the tail and two more shapes, not examined).

### 3.2 Seams, humanoid units (16 units; both sides counted: 32 seams per limb joint)

| Seam | Seams that meet | What the parts do | Distances (units) |
| --- | --- | --- | --- |
| forearm to wrist | 32 of 32 | **Share the ring exactly** | boundary pairs 640: all within 0.0011; 320 within 1e-4; none overlap or gap; UVs differ at 64 pairs, normals up to 19 degrees |
| knee to ankle | 32 of 32 | **Share the ring exactly** | 448 pairs: all within 0.0010; 224 within 1e-4; UVs differ at 256 pairs, normals up to 27 degrees |
| neck to chest | 12 of 16 | Overlap (140 pairs) and gap (42): the neck's ring lies 1.2 to 6 units from the chest's | signed gap -2.8 to +0.9 |
| chest to groin | 8 of 16 | Overlap (156 pairs) | 4.3 to 5.9 apart; signed gap -5.5 to -3.2 |
| groin to upper leg | 28 of 32 | Gap (145 pairs) | 2.8 to 5.8 apart; gap +2.1 to +5.6 |
| chest to upper arm | 0 | **Closed shells**: no boundary on the arm | nearest vertex pair 0.26, median 2.6 |
| upper arm to forearm | 0 | Closed upper arm | nearest 0.13, median 1.2 |
| wrist to hand | 0 | Closed hand | nearest 0.15, median 2.1 |
| upper leg to knee | 0 | Closed upper leg | nearest 0.23, median 2.2 |
| ankle to foot | 0 | Closed shells | nearest 0.24, median 2.3 |
| head to neck | 0 of 128 | Closed head shells | nearest 0.17, median 3.9 |

"Meet" means the boundary pairing found pairs. A part with no open edge (upper arm, hand, upper leg and so on) is a
closed shell: its end is capped, and the neighbour's end overlaps the cap.

### 3.3 Beast units (4 units)

The same two exact seams (forearm to wrist: 160 pairs within 1e-5; knee to ankle: 112 pairs within 2e-5), the same
closed shells at the other joints, and a tail seam (groin to tail) where 2 of 4 seams touch and the tail
lies 0.4 to 4.2 units from the groin (a gap, never a meeting). The tail is a skinned part (65 vertices, 4 bones for an
Argonian male). So the beast races follow the same structure; the tail is one extra part.

### 3.4 Weights, UVs and normals

- **Weight disagreement across seams cannot be measured:** no stock seam has skin on both sides. The only skinned
  parts are the chest and the hands, and each is adjacent to rigid parts. Every vertex of a rigid part is weighted
  1.0 to its attach bone in effect.
- The two exact seams are rigid to rigid on **different** bones (forearm bone, wrist bone; knee, ankle). A weld
  there gives each shared vertex two parent bones, which the equal-weight mean of decision H17 turns into 0.5 and 0.5.
- UVs and normals at the exact seams differ at many pairs (section 3.2): a seam vertex must stay as a weld group
  with its split UV or normal, not collapse into one vertex.
- Duplicate positions inside a part are common (UV splits): for example 10 of 59 vertices of a neck part.

## 4. The go/no-go table

| Fact (research note 0032 section 8) | Result | Verdict |
| --- | --- | --- |
| 1 Skinned, rigid or mixed? | 3 skinned and 16 rigid (humanoid); 6 and 14 (beast) | Go: rigid becomes weight 1.0 on the attach bone |
| 2 Left and right in one file? | Yes: one file serves both sides of every limb part, and the engine mirrors the left one in x in the bone's space; the skins file holds seven shapes selected by the skin filter | Go: the merger mirrors left rigid parts and flips their winding |
| 3 Shared, overlap or gap? | Shared at forearm-wrist and knee-ankle (all 40 units); overlap or gap at neck-chest, chest-groin, groin-leg; closed overlapping shells at six joints | **Partial**: only two joint types can be welded as planned |
| 4 Weights equal across seams? | Not measurable (no seam with skin on both sides) | N/A: the H17 mean applies only at the exact seams (0.5 / 0.5 across two bones) |
| 5 UV and normals equal across seams? | UV differs at 64 of 640 and 256 of 448 pairs; normals up to 19 and 27 degrees | Go: weld groups with split vertices are required |
| 6 Counts per body | 1,483 to 1,922 vertices, 2,234 to 2,610 triangles, 12 to 13 materials, 28 or 38 bones | Go: small enough for one cached mesh per body |
| 7 Head records | 3 to 18 per race and sex | Go: heads stay parts (H14) |
| 8 Beast races | Same structure plus a tail | Go (humans first, H18) |
| 9 File sizes | 66.7 to 92.3 kB and 9 to 10 files per body | Go: hashing at bake time costs nothing |

**Overall: go, with a changed scope for stage b.**

## 5. What the data says about the weld rule

- **Tolerance.** At the two exact seams every pair is within 0.0011; at every other joint the nearest cross-part
  vertex pair is at least 0.054 (beast head-neck; 0.084 humanoid chest-groin). There is a clean gap of a factor of
  50 between the two groups, so any tolerance from about 0.002 to 0.05 gives the same welds. **Recommendation: 0.01
  units**, a named constant with this note as its reason. (The rule in note 0032, "the largest gap below a fraction
  of the median boundary edge length", was not needed: the gap is obvious. The boundary edge lengths were not
  computed.)
- **Weight policy.** Stock vertices need no renormalising (every skinned vertex sums to 1 after the cut, none has more
  than four influences). Rigid parts become skinned with weight 1.0 on their attach bone; welded vertices get the
  equal-weight mean of their members (decision H17), 0.5 and 0.5 at the two exact seams. The tie order of influences
  is by bone index.

## 6. Recommendation for stage b (toddhow decides)

Stage b as written (weld every seam by tolerance) would weld only two joint types. The rest are overlapping closed
shells. Options:

1. **(Recommended first)** Merge **without** welding at the overlapping joints: one mesh and one draw per material,
   rigid parts turned into skinned parts with weight 1.0, the two exact seams welded at 0.01 as weld groups, the
   other parts left as overlapping regions of the same mesh. This reproduces the stock look exactly (the overlap is
   what hides the joint in the game today), delivers the cache and the draw-call saving, and leaves the surface
   morphs of tier 2 working inside each region; a morph that moves one region against a neighbour can open a gap at
   an overlapping joint, which the overlap hides up to its depth (about 1 to 6 units).
2. Add **cap removal** later: detect and drop the flat cap triangles at the closed ends so rings can be welded. This
   needs a per-joint rule and a visual check, and it is the way to a truly seamless body. It belongs after option 1
   has proved the pipeline.
3. Do not merge: keep stock parts and use only tier 1 (proportions by bone scale). Rejected here: it does not give
   the merged mesh the design wants.

Open for stage b whichever is chosen: confirm the skinned bind-pose formula numerically (section 2), and decide
whether the "weld group" of a split seam vertex moves as one under a morph (design 7.2).

## 7. What is not verified

- The skinned bind-pose formula (section 2) is untested against the engine's own skinning.
- The signed gap is measured along the line between the two parts' centres, not along each limb's axis; the raw 3D
  distances do not depend on it. Overlap depth is therefore approximate.
- The seam window is 6 units; a joint whose overlap is deeper would show fewer pairs. The all-vertex nearest
  distances (median 1 to 4 units at overlapping joints) suggest none is.
- Only vanilla Morrowind + Tribunal + Bloodmoon was measured. A body replacer mod changes everything above; the tool
  is meant to be run on it (and stage c's staleness key includes the model bytes).
- Beast races: only the figures above; nothing about their tails' skinning.
- Not measured: first person, werewolves, vampire heads, creatures, clothing and armour parts.
- The 32 Lua integration tests and the Linux gate were not run for this stage.
