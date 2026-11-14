#ifndef OPENEDEN_GAME_SKYRIMSE_ANIM_MOTION_H
#define OPENEDEN_GAME_SKYRIMSE_ANIM_MOTION_H

// Skyrim SE root motion of a character clip (task 0135 stage S6f: the enter and exit clips of furniture). Pure code. The
// clip files (.hkx) keep the root bone still; the motion of the figure through the world is in the animation data text
// files of the actor's project: `meshes/animationdata/<project>.txt` names each clip and gives it a number, and
// `meshes/animationdata/boundanims/anims_<project>.txt` holds, for each number, the clip's duration and its CUMULATIVE
// displacement and rotation since the start. Layout, VERIFIED on the five masters' `defaultmale` and `defaultfemale`
// projects and by reading the numbers against the clips (docs/research/0054-provenance.md F44 to F53 describe the
// sidecars; this reader repeats only what it needs):
//   - a clip entry in the project file: its name, its number, then the lines "1", "0", "0", an event count and the events;
//   - a motion block in the boundanims file: number, duration, a row count and rows `t x y z`, then a row count and rows
//     `t qx qy qz qw`. x is to the actor's right and y forward in the actor's frame at the start of the clip; the
//     rotation is about Z, counter-clockwise seen from above (Havok), a quaternion with x = y = 0.
// Measured (male, game units): chair_frontentervar1 2.83 s, 63.5 forward, 180 degrees; chair_leftenter 1.77 s, 52.1
// forward, +90; chair_frontexit 1.5 s, 47.7 forward, 180; chair_leftexit 1.5 s, 52.1 to the left, +90; bed_leftenter 6.83
// s, (2.5, 74.8), +90; bed_leftexit 8.07 s, (-67.2, 2.7), +90.
//
// A name that two entries share keeps the first; a number that two motion blocks share keeps the first. A block that does
// not parse is skipped (the scan resumes one line later), so hostile text gives a table with fewer clips, never an error.

#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace game_skyrimse
{
    struct ClipMotion
    {
        float mDuration = 0.f;
        struct Row
        {
            float mT = 0.f, mX = 0.f, mY = 0.f;
        };
        struct Turn
        {
            float mT = 0.f, mYaw = 0.f; ///< counter-clockwise radians, unwrapped along the rows
        };
        std::vector<Row> mTranslation;
        std::vector<Turn> mRotation;

        /// The cumulative displacement and yaw at `time` (seconds from the start of the clip), linear between rows, from
        /// zero at time 0, held after the last row. A time that is not finite or is negative reads as 0.
        void sample(float time, double& x, double& y, double& yaw) const;
        /// The values at the end of the clip.
        void total(double& x, double& y, double& yaw) const { sample(mDuration, x, y, yaw); }
    };

    /// Caps on what a table keeps (hostile text).
    inline constexpr std::size_t kMaxMotionRows = 4096;
    inline constexpr std::size_t kMaxMotionClips = 8192;

    class MotionTable
    {
    public:
        /// Reads a project file's clip names and a boundanims file's blocks and joins them. Returns the clips joined.
        /// Replaces nothing: clips already in the table stay (the first reading of a name wins).
        std::size_t add(std::string_view projectText, std::string_view boundText);

        /// By clip name, case-insensitively (the name in the file has its own capitals); null when unknown.
        [[nodiscard]] const ClipMotion* find(std::string_view clipName) const;
        [[nodiscard]] std::size_t size() const { return mClips.size(); }

    private:
        std::map<std::string, std::unique_ptr<const ClipMotion>> mClips; ///< lower-case name; nodes never move
    };

    /// The pieces, for tests.
    [[nodiscard]] std::map<std::string, long> parseClipNumbers(std::string_view projectText);
    [[nodiscard]] std::map<long, ClipMotion> parseMotionBlocks(std::string_view boundText);
}

#endif
