#ifndef OPENMW_MWWORLD_NEUTRALCORPSEPOSES_H
#define OPENMW_MWWORLD_NEUTRALCORPSEPOSES_H

#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "../mwphysics/ragdolltypes.hpp"
#include "neutralsave.hpp"

// Task 0263: the poses that settled death ragdolls of a neutral session ended in, kept by (game id, placement id) so
// that they can go into the neutral save (version 3) and, after a load, draw a corpse in the pose it lay in instead
// of letting it fall again. Pure data: the world holds one, fills it when a ragdoll settles, reads it when the pack
// puts a saved corpse back, and copies it to and from the save state. Main thread only. Bounded: at most
// kNeutralSaveMaxPoses poses of at most kNeutralSaveMaxPoseBones bones.
//
// Ownership: plain values; nothing points into the actor or the ragdoll.

namespace MWWorld
{
    class NeutralCorpsePoses
    {
    public:
        /// Keeps (or replaces) the pose of one actor: `names[i]` is the rig bone of `states[i]`, the rotation is the
        /// bone's world rotation as drawn and the position the world position (the pelvis' matters). False, and
        /// nothing kept, for an empty game id, no bones, more than kNeutralSaveMaxPoseBones, a name that is empty or
        /// longer than kNeutralSaveMaxPoseName, a value that is not finite, a size mismatch, or a new actor beyond
        /// kNeutralSaveMaxPoses.
        bool put(const std::string& gameId, std::uint32_t placement, std::span<const std::string> names,
            std::span<const MWPhysics::RagdollBoneState> states);

        /// The pose of one actor or nullptr. Valid until the next put, erase or load.
        const NeutralCorpsePose* find(const std::string& gameId, std::uint32_t placement) const;

        /// True when there was one.
        bool erase(const std::string& gameId, std::uint32_t placement);

        void clear() { mPoses.clear(); }
        std::size_t size() const { return mPoses.size(); }

        /// Replaces everything with the poses of a decoded save (already validated by the decoder).
        void load(std::span<const NeutralCorpsePose> poses);

        /// Every pose, in key order (what the save writes).
        std::vector<NeutralCorpsePose> collect() const;

        /// The names and states of a saved pose, in its bone order (what the driver takes). False for a pose with no
        /// bones.
        static bool toStates(const NeutralCorpsePose& pose, std::vector<std::string>& names,
            std::vector<MWPhysics::RagdollBoneState>& states);

    private:
        std::map<std::pair<std::string, std::uint32_t>, NeutralCorpsePose> mPoses;
    };
}

#endif
