# core/debug — debug overlays as data

Headers only, standard library only (plus `core/math`). Spec: `docs/tasks/0016`.

## Rules
- The core never renders (CLAUDE.md): modules produce `Overlay` data (points, lines, labels, RGBA colours) and only `sandbox/viewer` draws it. Never include graphics headers here.
- Positions are world space, metres, Z up. One `Overlay` is one named layer the viewer can toggle.
