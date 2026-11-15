#ifndef GAME_RENDERING_INTERFACE_H
#define GAME_RENDERING_INTERFACE_H

#include <array>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <components/havokpack/clipevents.hpp>

#include <openeden/game/actor_appearance.hpp>

#include "../mwphysics/ragdolltypes.hpp"
#include "../mwworld/ptr.hpp"

namespace MWRender
{
    struct RenderingInterface
    {
        virtual void insertModel(const MWWorld::Ptr& ptr, const std::string& model, bool allowLight = true) = 0;
        virtual void insertNPC(const MWWorld::Ptr& ptr) = 0;
        virtual void insertCreature(const MWWorld::Ptr& ptr, const std::string& model, bool weaponsShields) = 0;
        RenderingInterface& getObjects() { return *this; } // vsgopenmw-delete-me
        virtual ~RenderingInterface() {}

        // Declared after the destructor on purpose: a virtual added before it would move the destructor's slot.
        /// Draws a game pack's actor (task 0121, decision 0017): its skeleton and skinned parts in the rest pose. The
        /// default does nothing, so an implementation that has no neutral actors needs no change.
        virtual void insertNeutralActor(
            const MWWorld::Ptr& /*ptr*/, const openeden::sdk::game::ActorAppearance& /*appearance*/)
        {
        }

        /// Task 0146 (`[Game] neutral lip sync`): moves the mouth of a drawn neutral actor: the 16 viseme weights in
        /// 0..1 (the head's morphs Aah, BigAah, BMP, ChJSh, DST, Eee, Eh, FV, I, K, N, Oh, OohQ, R, Th, W). Does
        /// nothing for an object that has no head morph. Main thread.
        virtual void setNeutralMouth(const MWWorld::Ptr& /*ptr*/, const std::array<float, 16>& /*visemes*/) {}

        /// Task 0148 (`[Game] neutral actor locomotion`): the measured ground speed (units a second) of a drawn neutral
        /// actor, from the physics side. Does nothing for an object without a locomotion controller. Main thread; no
        /// allocation.
        virtual void setNeutralActorSpeed(const MWWorld::Ptr& /*ptr*/, float /*unitsPerSecond*/) {}

        /// Task 0135 stage S6b: the pose of a drawn neutral actor on furniture: loops the named clip (a file under the
        /// actor's animations folder, no extension; `perSex` when it is in the male/female sub-folder) over the idle,
        /// an empty name takes the pose off. Does nothing for an object without a pose controller. Main thread.
        virtual void setNeutralActorPose(const MWWorld::Ptr& /*ptr*/, std::string_view /*clip*/, bool /*perSex*/) {}

        /// Task 0170 (combat): plays the named clip ONCE on a drawn neutral actor, over the idle, and holds its last
        /// pose until it is stopped; an empty name stops it (the idle shows again). Does nothing without a pose
        /// controller.
        virtual void playNeutralActorClip(const MWWorld::Ptr& /*ptr*/, std::string_view /*clip*/, bool /*perSex*/) {}

        /// Task 0170: the meta of the clip playNeutralActorClip started and how far its sampling has got (empty for an
        /// actor with no such clip).
        virtual HavokPack::ClipProgress neutralActorClipProgress(const MWWorld::Ptr& /*ptr*/) { return {}; }

        /// Task 0171 stage S5-3b: the sidecar meta of a character clip for a swing that has no actor (the player's: no
        /// first-person arms). Null until the clip and the sidecars have been read on a worker; the first call for a
        /// clip starts the read. Main thread.
        virtual std::shared_ptr<const HavokPack::ClipMeta> neutralClipMeta(std::string_view /*clip*/, bool /*perSex*/)
        {
            return {};
        }

        // Task 0174 (S5-5, death ragdolls). All main thread. The defaults do nothing, so an implementation with no
        // neutral actors needs no change.
        /// The actor's mapped bones now against their rest, as the start pose of its ragdoll. False for an actor with
        /// no ragdoll controller (a creature, the setting off) or no pelvis.
        virtual bool neutralRagdollStartPose(const MWWorld::Ptr& /*ptr*/, MWPhysics::ActorStartPose& /*out*/)
        {
            return false;
        }
        /// Starts drawing the actor's skeleton from its ragdoll (`names`: the rig bone of each state). False with a
        /// reason when the driver is refused; the actor then keeps its pose.
        virtual bool beginNeutralRagdoll(const MWWorld::Ptr& /*ptr*/, std::span<const std::string> /*names*/,
            std::span<const MWPhysics::RagdollBoneState> /*initial*/, std::string& /*reason*/)
        {
            return false;
        }
        /// The ragdoll's bodies for this frame.
        virtual void setNeutralRagdollStates(
            const MWWorld::Ptr& /*ptr*/, std::span<const MWPhysics::RagdollBoneState> /*states*/)
        {
        }
        /// Stops drawing from the ragdoll (the ragdoll is gone).
        virtual void endNeutralRagdoll(const MWWorld::Ptr& /*ptr*/) {}
    };
}

#endif
