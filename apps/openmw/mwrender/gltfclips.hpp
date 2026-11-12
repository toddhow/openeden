#ifndef GAME_RENDER_GLTFCLIPS_H
#define GAME_RENDER_GLTFCLIPS_H

#include <cstddef>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <vsg/core/ref_ptr.h>

#include <opas/anim/GltfReader.hpp>
#include <opas/anim/SkeletonProfile.hpp>

namespace Anim
{
    class Bones;
    class ControllerMap;
}

namespace MWAnim
{
    class Context;
}

namespace MWRender
{
    struct RigBones;

    /// CreativeMotion's own authored clips, played by the engine's animation system (task 0123).
    ///
    /// The clips are glTF (`opas::anim::readGltf`) on CreativeMotion's humanoid rig. They are retargeted onto the
    /// actor's skeleton with a skeleton profile (`ClipBinding` for timing, root motion and leg scaling, plus a rest
    /// relative transfer in model space, because the two rigs' local axes differ) and baked into the engine's
    /// `Anim::ControllerMap`, whose text keys name the groups `[Game] inertial locomotion` looks for. Nothing here
    /// touches a bone: `MWAnim::Play` plays the result like a .kf source.
    namespace GltfClips
    {
        /// The groups taken from a clip file. A clip of any other name is ignored on purpose: CreativeMotion's file
        /// also holds `idle`, which is a Morrowind group name, and a later source wins over an earlier one.
        inline constexpr std::string_view sLocomotionGroups[]
            = { "walkstart", "runstart", "walkstop", "runstop", "runskid", "turnleft90", "turnright90" };

        /// Where the engine finds the shipped clips and the profile in the VFS (files/data/CMakeLists.txt copies them
        /// from components/creativemotion/data).
        inline constexpr std::string_view sClipFile = "animations/humanoid-locomotion.glb";
        inline constexpr std::string_view sProfileFile = "animations/humanoid-to-eden.skeletonprofile.json";

        /// Seconds between baked keys (the bake samples the clip, so this bounds the error of the rest relative
        /// transfer between keys; a 0.58 s clip gets 18 keys).
        inline constexpr double sBakeRateHz = 30.0;
        /// Seconds between two clips on the baked timeline, so that no text key query reaches into a neighbour.
        inline constexpr float sClipGapSeconds = 0.5f;

        struct Source
        {
            opas::anim::GltfAnimations mClips;
            opas::anim::SkeletonProfile mProfile;
        };

        /// Reads the glTF clips and the profile. Empty with a located message in `error`. Never throws.
        std::optional<Source> parse(std::span<const std::byte> glb, std::string_view glbName,
            std::string_view profileText, std::string_view profileName, std::string& error);

        /// Bakes the clips named in `groups` (compared in lower case with the clip's own name, so `WalkStart` is
        /// `walkstart` and `WalkStart.001` is not) for `target`. `unitsPerMeter` is the game's length unit. Groups the
        /// file lacks are left out; null with `error` set when nothing could be baked.
        vsg::ref_ptr<Anim::ControllerMap> retarget(const Source& source, const RigBones& target, double unitsPerMeter,
            std::span<const std::string_view> groups, std::string& error);

        /// The engine's entry: reads (once per process) the shipped clips through the context's VFS and bakes the
        /// locomotion groups for `bones`. Null when `[Game] inertial locomotion` is off, the files are missing or the
        /// skeleton does not take them; the reason is logged once. Main or loader thread: reads are cached under a
        /// lock.
        vsg::ref_ptr<Anim::ControllerMap> loadLocomotionClips(const MWAnim::Context& context, Anim::Bones& bones);
    }
}

#endif
