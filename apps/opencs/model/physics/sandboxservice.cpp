#include "sandboxservice.hpp"

#include <components/debug/debuglog.hpp>

#include <opas/rig/ProfileFile.hpp>
#include <opas/rig/RigFile.hpp>

#include <cmath>
#include <exception>
#include <utility>

namespace CSMPhysics
{
    namespace
    {
        bool finite(const openeden::editor::Vec3& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        opas::math::Vec3 toMath(const openeden::editor::Vec3& v)
        {
            return opas::math::Vec3{ v.x, v.y, v.z };
        }
    }

    SandboxService::SandboxService(StudioWorld& world)
        : mWorld(world)
    {
    }

    SandboxService::~SandboxService()
    {
        release();
    }

    bool SandboxService::build(const std::filesystem::path& rigPath, const std::filesystem::path& profilePath,
        const openeden::editor::Vec3& offsetM, double yawRadians, std::string& error)
    {
        release();
        if (!finite(offsetM) || !std::isfinite(yawRadians))
        {
            error = "the sandbox placement is not finite";
            return false;
        }
        const opas::rig::RigLoadResult rig = opas::rig::loadRigFile(rigPath);
        if (!rig.ok())
        {
            error = rig.error.toString();
            return false;
        }
        const opas::rig::ProfileLoadResult profile = opas::rig::loadProfileFile(profilePath);
        if (!profile.ok())
        {
            error = profile.error.toString();
            return false;
        }

        // The ground first: its top face is the plane z = 0.
        if (!addGround(error))
            return false;

        mScene = PushScene::create(
            mWorld, *rig.value, *profile.value, PushScene::Placement{ toMath(offsetM), yawRadians }, &error);
        if (mScene == nullptr)
        {
            mWorld.removeBody(mGround);
            mGround = JPH::BodyID();
            return false;
        }
        return true;
    }

    bool SandboxService::addGround(std::string& error)
    {
        constexpr double units = StudioWorld::sUnitsPerMeter;
        try
        {
            mGround = mWorld.addStaticBox(JPH::RVec3(0, 0, -sGroundHalfThicknessM * units),
                JPH::Vec3(static_cast<float>(sGroundHalfExtentM * units),
                    static_cast<float>(sGroundHalfExtentM * units), static_cast<float>(sGroundHalfThicknessM * units)));
            return true;
        }
        catch (const std::exception& e)
        {
            error = std::string("the sandbox ground could not be made: ") + e.what();
            return false;
        }
    }

    bool SandboxService::built() const
    {
        return mScene != nullptr && mScene->present();
    }

    std::vector<std::string> SandboxService::bodyNames() const
    {
        return mScene != nullptr ? mScene->boneNames() : std::vector<std::string>();
    }

    bool SandboxService::push(
        std::string_view bone, const openeden::editor::Vec3& impulseNs, const openeden::editor::Vec3& worldPointM)
    {
        return mScene != nullptr && mScene->push(bone, toMath(impulseNs), toMath(worldPointM));
    }

    std::size_t SandboxService::pendingPushes() const
    {
        return mScene != nullptr ? mScene->pending() : 0;
    }

    bool SandboxService::bodyPose(std::string_view bone, openeden::editor::SandboxBodyPose& out) const
    {
        opas::physics::BodyState state;
        if (mScene == nullptr || !mScene->boneState(bone, state))
            return false;
        out.positionM = openeden::editor::Vec3{ state.positionM.x, state.positionM.y, state.positionM.z };
        out.qx = state.rotation.x;
        out.qy = state.rotation.y;
        out.qz = state.rotation.z;
        out.qw = state.rotation.w;
        return true;
    }

    bool SandboxService::reset(std::string& error)
    {
        if (mScene == nullptr)
        {
            error = "nothing is built in the sandbox";
            return false;
        }
        // StudioWorld::clear() removes the ground behind our back. A ground that is still there is kept (taking it
        // away first and failing to make it again would leave the ragdoll with nothing to stand on); a stale one is
        // made again, and if that fails nothing else was touched.
        if (!mWorld.hasBody(mGround))
        {
            mGround = JPH::BodyID();
            if (!addGround(error))
                return false;
        }
        return mScene->reset(&error);
    }

    void SandboxService::release()
    {
        // The scene first: it removes its ragdoll and its hook; the ground goes after (a body id the world forgot after
        // clear() is ignored).
        mScene.reset();
        if (mGround != JPH::BodyID())
            mWorld.removeBody(mGround);
        mGround = JPH::BodyID();
    }
}
