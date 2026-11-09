#include "ownedsandbox.hpp"

#include <utility>

namespace CSMPhysics
{
    OwnedSandbox::OwnedSandbox(openeden::editor::SimulationSandbox& inner, OwnerFunction currentOwner)
        : mInner(inner)
        , mCurrentOwner(std::move(currentOwner))
    {
    }

    bool OwnedSandbox::build(const std::filesystem::path& rigPath, const std::filesystem::path& profilePath,
        const openeden::editor::Vec3& offsetM, double yawRadians, std::string& error)
    {
        const bool ok = mInner.build(rigPath, profilePath, offsetM, yawRadians, error);
        // A failed build leaves nothing in the sandbox (the interface's contract), so nothing is owned either.
        mOwner = ok ? (mCurrentOwner ? mCurrentOwner() : std::string()) : std::string();
        return ok;
    }

    bool OwnedSandbox::built() const
    {
        return mInner.built();
    }

    std::vector<std::string> OwnedSandbox::bodyNames() const
    {
        return mInner.bodyNames();
    }

    bool OwnedSandbox::push(
        std::string_view bone, const openeden::editor::Vec3& impulseNs, const openeden::editor::Vec3& worldPointM)
    {
        return mInner.push(bone, impulseNs, worldPointM);
    }

    std::size_t OwnedSandbox::pendingPushes() const
    {
        return mInner.pendingPushes();
    }

    bool OwnedSandbox::bodyPose(std::string_view bone, openeden::editor::SandboxBodyPose& out) const
    {
        return mInner.bodyPose(bone, out);
    }

    bool OwnedSandbox::reset(std::string& error)
    {
        return mInner.reset(error);
    }

    void OwnedSandbox::release()
    {
        mInner.release();
        mOwner.clear();
    }

    void OwnedSandbox::releaseIfOwnedBy(const std::string& moduleId)
    {
        if (!moduleId.empty() && moduleId == mOwner)
            release();
    }
}
