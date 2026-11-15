#ifndef OPENMW_MWRENDER_SKYRIMRAGDOLL_HPP
#define OPENMW_MWRENDER_SKYRIMRAGDOLL_HPP

// Task 0174 (Skyrim combat S5-5, death ragdolls): the part of a neutral Skyrim actor that lets a CreativeMotion
// ragdoll draw its skeleton after it dies. One controller per drawn actor, added AFTER the idle, locomotion and pose
// controllers (so its bone writes win), only when `[Game] neutral combat` and `[Physics] creativemotion ragdolls`
// are on and the skeleton has a bone table (skyrimragdollmap.hpp). It does nothing until armed (one mutex lock and a
// flag test a frame), and it never touches the physics world: the main thread copies the ragdoll's bodies into it
// once a frame (setStates), and run() draws from that copy through MWRender::ActorRagdollDriver (the Morrowind
// driver, given this skeleton's bone set).
//
// Ownership: the controller owns its rest snapshot and driver and shares the table; the Anim::Bones and the root
// transform belong to the actor's object, which keeps the controller alive (Object::keepAlive) and outlives it.
// Threads: startPose, arm, setStates, disarm: main thread. run(): an update thread (or the main thread). The few
// members both touch are behind one mutex; run() allocates nothing.

#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <vector>

#include <components/animation/controller.hpp>

#include "../mwphysics/ragdolltypes.hpp"
#include "actorragdolldriver.hpp"
#include "skyrimragdollmap.hpp"

namespace Anim
{
    class Bones;
    class Transform;
}

namespace MWRender::SkyrimRagdoll
{
    /// What the main thread hands the update thread: one state per rig body, newest wins. Shared by the controller's
    /// RagdollSource so that the driver reads it without knowing the controller.
    class Snapshot
    {
    public:
        explicit Snapshot(std::vector<std::string> names)
            : mNames(std::move(names))
            , mStates(mNames.size())
        {
        }
        [[nodiscard]] std::span<const std::string> names() const { return mNames; }
        /// False when `states` is shorter than the names.
        bool set(std::span<const MWPhysics::RagdollBoneState> states);
        /// Copies into out (at least names().size()). False once ended.
        bool get(std::span<MWPhysics::RagdollBoneState> out) const;
        void end();

    private:
        std::vector<std::string> mNames;
        mutable std::mutex mMutex;
        std::vector<MWPhysics::RagdollBoneState> mStates;
        bool mEnded = false;
    };

    class Controller : public Anim::Controller
    {
    public:
        /// Takes the actor's rest pose NOW: make it before any other controller has run (the object was just made).
        Controller(std::shared_ptr<const BoneTable> table, Anim::Bones& bones, Anim::Transform* root);

        /// Main thread. The actor's mapped bones now, against the rest pose, as the ragdoll's start pose. False when
        /// the rest or the pelvis is missing.
        bool startPose(MWPhysics::ActorStartPose& out) const;

        /// Main thread. Starts drawing from the ragdoll: `names` are the rig bone each of `initial` belongs to
        /// (RagdollHost::actorBodyBones order); the driver captures its offsets from `initial` and the bones as they
        /// are now. False (with reason) when the driver is not valid; then nothing is armed.
        bool arm(std::span<const std::string> names, std::span<const MWPhysics::RagdollBoneState> initial,
            std::string& reason);

        /// Main thread. The ragdoll's bodies for this frame.
        void setStates(std::span<const MWPhysics::RagdollBoneState> states);

        /// Main thread. Stops drawing; the bones keep the last pose written until the clips write them again.
        void disarm();

        [[nodiscard]] bool armed() const;

        void run(vsg::Object& target, float time) const override;

    private:
        std::shared_ptr<const BoneTable> mTable;
        Anim::Bones& mBones;
        Anim::Transform* mRoot;
        ActorRagdollRest mRest;

        mutable std::mutex mMutex;
        std::shared_ptr<Snapshot> mSnapshot;
        mutable std::unique_ptr<ActorRagdollDriver> mDriver;
        mutable bool mArmed = false;
        unsigned mTraceCount = 0; // setStates calls, for the developer trace only
    };
}

#endif
