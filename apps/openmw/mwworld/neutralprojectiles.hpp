#ifndef OPENMW_MWWORLD_NEUTRALPROJECTILES_H
#define OPENMW_MWWORLD_NEUTRALPROJECTILES_H

// Task 0178 (combat stage S5-6): draws the projectiles the combat bridge flies. The physics side (NeutralCombatBridge)
// owns each shot and tells the world, through PhysicsSystem::getNeutralProjectileOps(), when one starts, moves and
// ends; this class turns those ops into the renderer's own projectile objects (MWRender::Projectiles, the objects the
// Morrowind spell bolts and arrows use), keyed by the bridge's number. A model that cannot be read is logged once per
// path and not drawn: the shot still flies and hits.
//
// Main thread only (World::doPhysics).

#include "../mwphysics/physicssystem.hpp"

#include <cstdint>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <unordered_map>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

namespace MWRender
{
    class RenderManager;
    struct ProjectileHandle;
}

namespace MWWorld
{
    /// The attitude that turns the model's +Y axis (the way an arrow points) onto `direction`, with the roll chosen so
    /// that the model's +Z stays as upright as it can: the same construction as OpenMW's gravity-affected bolts.
    [[nodiscard]] vsg::quat neutralProjectileAttitude(const vsg::vec3& direction);

    class NeutralProjectiles
    {
    public:
        NeutralProjectiles();
        ~NeutralProjectiles();

        void apply(
            std::span<const MWPhysics::PhysicsSystem::NeutralProjectileOp> ops, MWRender::RenderManager& rendering);

        /// Projectiles drawn now.
        [[nodiscard]] std::size_t drawn() const { return mHandles.size(); }
        /// Ends every drawn projectile (the renderer removes them on its next update).
        void clear();

    private:
        std::unordered_map<std::uint32_t, std::shared_ptr<MWRender::ProjectileHandle>> mHandles;
        std::set<std::string> mWarned;
    };
}

#endif
