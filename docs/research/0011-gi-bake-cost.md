# 0011 - GI probe bake cost

Date: 2026-11-01 - Task 0028 (stage c) - Status: measurement.

Machine: the RTX 4070 box of note 0009 (the baker is CPU only; Jolt ray casts on the main thread), `build-jolt`, Release, 256 rays, 2 bounces, spacing 128, a 20 ms per frame budget (so the figures include the window being drawn).

| Cell | Probes | Lights | Time |
| --- | --- | --- | --- |
| `Balmora, Council Club` | 1440 | 17 | about 0.25 s |
| `Caldera, Guild of Mages` | 2400 | 13 | about 0.6 s |
| `Seyda Neen, Arrille's Tradehouse` | 5733 | 16 | about 0.4 s |
| `Caldera, Guild of Mages`, 32768 rays | 2400 | 13 | 72 s (128 times the rays: it scales with the ray count) |

- The volume covers the box of every body in the Jolt world (`PhysicsSystem::getWorldBounds`), which is larger than the rooms (Caldera's guild gets a 10 x 15 x 16 lattice), so many probes sit outside the building and read open sky. The count is capped at 8192 by raising the spacing.
- A bake is deterministic: two runs of the same cell give byte-identical files, and a run killed after its 30 second save and resumed gives the same file as one uninterrupted run (checked with 32768 rays, `cmp`).
