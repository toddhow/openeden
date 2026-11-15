#include "skyrimragdoll.hpp"

#include <cstdlib>

#include <components/animation/bones.hpp>
#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/misc/vsgmath.hpp>
#include <components/vsgutil/name.hpp>

#include "bonepose.hpp"

namespace MWRender::SkyrimRagdoll
{
    namespace
    {
        // The driver's source: the controller's snapshot, as the physics-backed one of the Morrowind driver reads the
        // game's world.
        class SnapshotSource final : public RagdollSource
        {
        public:
            explicit SnapshotSource(std::shared_ptr<Snapshot> snapshot)
                : mSnapshot(std::move(snapshot))
            {
            }
            std::span<const std::string> boneNames() const override { return mSnapshot->names(); }
            bool read(std::span<MWPhysics::RagdollBoneState> out) const override { return mSnapshot->get(out); }

        private:
            std::shared_ptr<Snapshot> mSnapshot;
        };
    }

    bool Snapshot::set(std::span<const MWPhysics::RagdollBoneState> states)
    {
        if (states.size() < mNames.size())
            return false;
        std::scoped_lock lock(mMutex);
        for (std::size_t i = 0; i < mStates.size(); ++i)
            mStates[i] = states[i];
        return true;
    }

    bool Snapshot::get(std::span<MWPhysics::RagdollBoneState> out) const
    {
        std::scoped_lock lock(mMutex);
        if (mEnded || out.size() < mStates.size())
            return false;
        for (std::size_t i = 0; i < mStates.size(); ++i)
            out[i] = mStates[i];
        return true;
    }

    void Snapshot::end()
    {
        std::scoped_lock lock(mMutex);
        mEnded = true;
    }

    Controller::Controller(std::shared_ptr<const BoneTable> table, Anim::Bones& bones, Anim::Transform* root)
        : mTable(std::move(table))
        , mBones(bones)
        , mRoot(root)
        , mRest(bones, mTable->set())
    {
        // The object runs a controller that is not auto-play only when an animation plays, which nothing does for a
        // neutral actor (found in game: the first version never drew the ragdoll). Like SkyrimPose and SkyrimIdle.
        hints.autoPlay = true;
    }

    bool Controller::startPose(MWPhysics::ActorStartPose& out) const
    {
        return makeActorStartPose(mBones, mRoot, mRest, mTable->set(), out);
    }

    bool Controller::arm(std::span<const std::string> names, std::span<const MWPhysics::RagdollBoneState> initial,
        std::string& reason)
    {
        if (initial.size() < names.size())
        {
            reason = "too few ragdoll states";
            return false;
        }
        auto snapshot = std::make_shared<Snapshot>(std::vector<std::string>(names.begin(), names.end()));
        snapshot->set(initial);
        auto driver = std::make_unique<ActorRagdollDriver>(
            mBones, mRoot, std::make_unique<SnapshotSource>(snapshot), mTable->set());
        if (!driver->isValid())
        {
            reason = driver->reason();
            return false;
        }
        std::scoped_lock lock(mMutex);
        if (mSnapshot)
            mSnapshot->end();
        mSnapshot = std::move(snapshot);
        mDriver = std::move(driver);
        mArmed = true;
        return true;
    }

    void Controller::setStates(std::span<const MWPhysics::RagdollBoneState> states)
    {
        std::shared_ptr<Snapshot> snapshot;
        {
            std::scoped_lock lock(mMutex);
            snapshot = mSnapshot;
        }
        if (snapshot)
            snapshot->set(states);
        // OPENEDEN_RAGDOLL_TRACE: about every two seconds, where the DRAWN pelvis and head are against the ragdoll's
        // (the bones as the previous frame's update left them), so a drawing that does not follow the physics shows.
        static const bool trace = std::getenv("OPENEDEN_RAGDOLL_TRACE") != nullptr;
        if (trace && snapshot && ++mTraceCount % 120 == 1)
        {
            const BonePose pose(mBones, mRoot);
            const std::span<const std::string> names = snapshot->names();
            for (const char* rig :
                { "Pelvis", "Spine", "Chest", "Head", "LeftUpperArm", "LeftHand", "LeftThigh", "LeftFoot" })
            {
                const std::span<const RagdollBoneMapping> mappings = mTable->set().mMappings;
                for (std::size_t i = 0; i < mappings.size() && i < names.size(); ++i)
                {
                    if (std::string(mappings[i].mRigBone) != rig)
                        continue;
                    const BonePose::Bone bone = pose.find(mappings[i].mEngineBone);
                    std::size_t state = names.size();
                    for (std::size_t k = 0; k < names.size(); ++k)
                        if (names[k] == rig)
                            state = k;
                    if (!bone || state == names.size() || state >= states.size())
                        continue;
                    if (mTraceCount == 1)
                    {
                        std::string path;
                        for (const Anim::Transform* node : bone.mPath)
                            path += (path.empty() ? "" : " > ") + vsgUtil::getName(*node);
                        Log(Debug::Info) << "RAGDOLLPATH " << rig << ": " << path;
                    }
                    const vsg::dvec3 drawn = Misc::VsgMath::getTrans(pose.getWorld(bone));
                    Log(Debug::Info) << "RAGDOLLDRAW " << rig << " drawn " << drawn.x << " " << drawn.y << " "
                                     << drawn.z << " ragdoll " << states[state].mPosition[0] << " "
                                     << states[state].mPosition[1] << " " << states[state].mPosition[2];
                }
            }
        }
    }

    void Controller::disarm()
    {
        std::scoped_lock lock(mMutex);
        mArmed = false;
        if (mSnapshot)
            mSnapshot->end();
    }

    bool Controller::armed() const
    {
        std::scoped_lock lock(mMutex);
        return mArmed;
    }

    void Controller::run(vsg::Object&, float) const
    {
        std::scoped_lock lock(mMutex);
        if (!mArmed || !mDriver)
            return;
        if (!mDriver->apply())
            mArmed = false; // the snapshot ended: the bones keep what they were given
    }
}
