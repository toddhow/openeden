# components/render

Renderer pieces with no game types: `Render::Engine` (wraps `vsg::Viewer`), render passes and attachments, screenshots, and the small scene helpers shared by the game and the editor. Read the root `CLAUDE.md` first. `components/` never includes `apps/`, so nothing here names `MWWorld`, `MWBase` or a game visibility mask.

## Shared with the editor (task 0014, B2)

- `debuggeometry.*`: lines and triangles submitted as data. The rules (threading, fixed capacity, one geometry per producer) are in its header and in `apps/openmw/mwrender/CLAUDE.md`.
- `simplewater.hpp`: the flat water plane; reads `Water_SurfaceTexture` and `Water_Map_Alpha` from `Fallback::Map`.
- `intersect.hpp`: `nearestIntersection` sorts an intersector's hits and returns the nearest as point, normal, ratio and first `Anim::Transform`. Turning that transform into a game object or an editor instance is the caller's job (`apps/openmw/mwrender/intersect.hpp` in the game).
