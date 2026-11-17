#include "neutralprojectiles.hpp"

#include <cmath>
#include <cstdlib>
#include <exception>
#include <optional>

#include <components/debug/debuglog.hpp>
#include <components/misc/vsgmath.hpp>

#include "../mwrender/projectiles.hpp"
#include "../mwrender/rendermanager.hpp"

namespace MWWorld
{
    vsg::quat neutralProjectileAttitude(const vsg::vec3& direction)
    {
        const float length = vsg::length(direction);
        if (!(length > 1e-4f))
            return vsg::quat();
        const vsg::vec3 dir = Misc::VsgMath::divide(direction, length);
        vsg::vec3 right = vsg::cross(dir, vsg::vec3(0, 0, 1));
        if (Misc::VsgMath::normalizeInPlace(right) < 1e-4f)
            right = vsg::vec3(1, 0, 0);
        const vsg::vec3 up = vsg::cross(right, dir);
        const vsg::mat4 mat(
            right.x, right.y, right.z, 0.f, dir.x, dir.y, dir.z, 0.f, up.x, up.y, up.z, 0.f, 0.f, 0.f, 0.f, 1.f);
        const vsg::dquat q = Misc::VsgMath::rotationOf(mat);
        return vsg::quat(
            static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w));
    }

    NeutralProjectiles::NeutralProjectiles() = default;
    NeutralProjectiles::~NeutralProjectiles() = default;

    void NeutralProjectiles::clear()
    {
        for (auto& entry : mHandles)
            if (entry.second)
                entry.second->remove = true;
        mHandles.clear();
    }

    void NeutralProjectiles::apply(
        std::span<const MWPhysics::PhysicsSystem::NeutralProjectileOp> ops, MWRender::RenderManager& rendering)
    {
        using Kind = MWPhysics::PhysicsSystem::NeutralProjectileOp::Kind;
        static const bool trace = std::getenv("OPENEDEN_NEUTRAL_COMBAT_TRACE") != nullptr;
        for (const auto& op : ops)
        {
            if (trace)
                Log(Debug::Info) << "NPROJ "
                                 << (op.mKind == Kind::Spawn         ? "spawn"
                                            : op.mKind == Kind::Move ? "move"
                                                                     : "remove")
                                 << " id " << op.mId << " at " << op.mPose.mX << " " << op.mPose.mY << " "
                                 << op.mPose.mZ << " drawn " << mHandles.size();
            switch (op.mKind)
            {
                case Kind::Spawn:
                {
                    MWRender::Projectiles* projectiles = rendering.getProjectiles();
                    if (projectiles == nullptr || op.mModel.empty())
                        break;
                    const vsg::vec3 position(static_cast<float>(op.mPose.mX), static_cast<float>(op.mPose.mY),
                        static_cast<float>(op.mPose.mZ));
                    const vsg::quat attitude
                        = neutralProjectileAttitude(vsg::vec3(op.mPose.mDirX, op.mPose.mDirY, op.mPose.mDirZ));
                    try
                    {
                        mHandles[op.mId] = projectiles->add(
                            op.mModel, position, attitude, false, std::nullopt, std::nullopt, std::string(), {});
                    }
                    catch (const std::exception& e)
                    {
                        if (mWarned.insert(op.mModel).second)
                            Log(Debug::Warning) << "Projectile model " << op.mModel << " not drawn: " << e.what();
                    }
                    break;
                }
                case Kind::Move:
                {
                    const auto found = mHandles.find(op.mId);
                    if (found == mHandles.end())
                        break;
                    found->second->position = vsg::vec3(static_cast<float>(op.mPose.mX),
                        static_cast<float>(op.mPose.mY), static_cast<float>(op.mPose.mZ));
                    found->second->attitude
                        = neutralProjectileAttitude(vsg::vec3(op.mPose.mDirX, op.mPose.mDirY, op.mPose.mDirZ));
                    break;
                }
                case Kind::Remove:
                {
                    const auto found = mHandles.find(op.mId);
                    if (found == mHandles.end())
                        break;
                    found->second->remove = true;
                    mHandles.erase(found);
                    break;
                }
            }
        }
    }
}
