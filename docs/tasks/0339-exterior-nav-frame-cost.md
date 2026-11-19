# Task 0339: why the p99 frame time looked worse with task 0336's two settings

Branch `navcost-0339` from local `main` `f8768f232f`, rebased onto `eb7e9353a1`. Measurement task; the only code
change is three trace scopes (below). Same hand measured and wrote; no fresh reviewer; Windows only; local commit, not pushed.

## 1. Short answer

**The settings do not cost the frame time. The p99 in task 0336's table is a threshold artefact of the navigator wait.**
Task 0323's cold walk has about 55 frames over 25 ms whatever the settings (46 of them are `world.navigator.wait` stalls of
100 ms to 2 s). A run has 5,578 frames, so its p99 is the 56th slowest frame: with 55 or fewer slow frames the p99 is a
13 to 25 ms frame, with 56 or more it is a stall of 25 to 45 ms. The count straddles 56 in every configuration, settings
off included (off: 57, 57, 59, 59, 59 slow frames, p99 27 to 43 ms; the first run of this task, off, gave p99 40.8). 0336's
pair "off 12.8 / 23.9, on 38.2 / 43.1" was 53 and 55 slow frames against 56 and 58: a difference of 3 frames at the edge of
the cutoff, not a cost.

## 2. Method

`tools/streaming_check/navcost.sh <name> walk <off|W|L|B> [navdb]` (new) runs task 0323's `walk` scenario (play profile,
`incremental cell build` off, fixed frame time, quiet window, volume 0) with `[Game] neutral actor walk through` (W),
`neutral actor long paths` (L), both (B) or neither (off), and records the number of other `openmw`, `cl`, `link` and
`ninja` processes and the free virtual memory before and after. `tools/streaming_check/frames.py` (new) prints frame time
by band (count and time) and the totals of the NPC scopes in the frames. New trace scopes (one atomic read when
`[Cells] load trace` is off): `neutral.pathfind` (the path-finder lambda of `World`, value = the navigator's status),
`neutral.follow` (`followNeutralGoal`) and `neutral.walk` (`walkNeutralActor`); `physics.neutral.actors` (task 0323) holds
all of them. Cold = a fresh `navmesh.db`; warm = the db a cold B run wrote. Rounds are interleaved (off, W, L, B, off...).
Machine: shared; other agents ran 1 to 4 `openmw` and sometimes `cl`/`link` during the runs (recorded per run in
`I:\Projects\OpenEden-work\followers\nav339\<run>.txt`); one early run (c2_W) overlapped a build and has a 4.4 ms median and
25 s of navigator wait, and the round was repeated on the rebased build (c4, c5, w4), which overlapped less.

## 3. Numbers (5,578 frames per run; "slow" = over 25 ms; means over the whole run)

Cold navmesh, 5 runs each (3 before the rebase, 2 after):

| config | p99 (ms), sorted | slow frames | `physics.neutral.actors` | `neutral.walk` (calls) | `neutral.pathfind` (ms) | `world.navigator.wait` |
| --- | --- | --- | --- | --- | --- | --- |
| off | 27.5 31.2 34.3 41.1 42.7 | 57 57 59 59 59 | 255 ms | 46 ms (1,446) | 8.3 | 20.6 s |
| walk through | 26.1 28.5 37.1 41.2 43.7 | 56 56 56 59 60 | 269 ms | 60 ms (1,965) | 7.5 | 21.1 s |
| long paths | 20.5 20.7 22.1 38.5 43.3 | 54 55 55 58 59 | 243 ms | 49 ms (1,458) | 9.4 | 20.1 s |
| both | 23.3 23.9 38.6 42.4 43.2 | 55 55 58 58 59 | 250 ms | 62 ms (2,122) | 8.3 | 19.7 s |

Warm navmesh, 4 runs each: p99 off 5.4 to 6.8, walk through 5.4 to 7.1, long paths 5.4 to 7.2, both 5.5 to 9.4 (the 9.4 ms
run overlapped a build); slow frames off 19 19 20 21, W 17 18 19 21, L 18 18 20 21, B 16 17 18 21;
`physics.neutral.actors` 285 / 318 / 313 / 349 ms; `neutral.walk` 94 / 101 / 89 / 118 ms.

With the settings on (cold and warm) the slow-frame count equals the off count within the spread between repeats of the
same setting (cold 54 to 60 everywhere, warm 16 to 21 everywhere), the median is equal, and the whole NPC update (path
asks, follow, steps) costs 0.04 to 0.06 ms a frame in every row. The most the settings add is about +30 to +60 ms of NPC
update over a warm run (0.005 to 0.011 ms a frame) and about +15 ms of `neutral.walk` cold: NPCs walk farther (cold
`neutral.walk` calls rise by 35 to 45 percent with walk through, because fewer walks end at a fern), which is the only
measurable effect. Path asks (about 1,500 a run, 8 ms in all, 5 microseconds each) do not change: no re-ask storm.
0336's suspicion that walking NPCs load more cells is not seen: `neutral.cell.build` and `neutralactor.create` counts are
50 and 43 to 45 in every run.

## 4. What the slow frames are

In every configuration the frames over 100 ms (43 to 48 cold, 9 warm) are `world.navigator.wait` (the main thread waiting
for navmesh tiles after a grid change; task 0323 section 4; 19 to 22 s of it per cold run) and the frames of 25 to
100 ms (7 to 16 cold) are first-time cell builds (`obj.render`, `neutralactor.create`). None of the NPC scopes is in the
worst frame of any run. The second navmesh agent (the r22 cylinder) is added when the first NPC asks for a path, also in
the off configuration (the profile's `pathfind half extents` differ from the capsule), so the navigator work is the same
in all four configurations.

## 5. Fix

None: it is not a cost of these settings, so nothing in the engine changed beyond the three trace scopes, which are
inert when `[Cells] load trace` is off. No test was written (no behaviour changed). The p99 of a run with about 55 slow
frames must not be used to compare configurations; use the slow-frame count and `frames.py`'s bands.

## 6. Morrowind unchanged

Fixed Balmora frame `1c5d47ad0b5dc32d` (the recorded value of earlier builds); `tools/worldobjects_check` against the
integ2 build: 110 objects, 111 result lines each, OK. Only trace scopes and tools changed.

## 7. Not run / decisions for toddhow

Not run: the Linux gate, clang-format 14, clang-tidy, the Lua tests, the unit tests (no behaviour changed), a quiet machine
(shared in every run), the fly (`fly`) or real-movement (`rwalk`) variants, the world with LOD on. Decisions: none needed;
suggest 0336's cost paragraph says "no measurable cost; the p99 of that run is a threshold artefact". No existing test edited.
