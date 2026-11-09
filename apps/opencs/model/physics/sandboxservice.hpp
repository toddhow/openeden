#ifndef CSM_PHYSICS_SANDBOXSERVICE_H
#define CSM_PHYSICS_SANDBOXSERVICE_H

// Eden Studio's side of the Editor Module API 1.3 simulation sandbox (task 0065, stage B): the interface a module
// calls, over PushScene and the editor's one StudioWorld. No Qt and no render types.

#include "pushscene.hpp"
#include "studioworld.hpp"

#include <openeden/editor_module/types.hpp>

#include <memory>
#include <string>
#include <vector>

namespace CSMPhysics
{
    /// One ragdoll standing on a static ground in a StudioWorld, built from a rig and a profile read from files.
    ///
    /// Ownership: owns the PushScene and the ground body (a static box whose top is the plane z = 0). It must be
    /// destroyed BEFORE the world; its destructor removes both. StudioWorld::clear() removes the bodies behind its
    /// back: built() is then false until build() is called again.
    ///
    /// Threads: the main thread only. A push waits for the next step (see SimulationSandbox); nothing here allocates in
    /// the step path (PushScene's queue is a fixed array; bodyPose and push allocate nothing).
    class SandboxService final : public openeden::editor::SimulationSandbox
    {
    public:
        /// The ground: half extents in meters (x and y) and its thickness; the top face is z = 0.
        static constexpr double sGroundHalfExtentM = 20.0;
        static constexpr double sGroundHalfThicknessM = 0.5;

        /// \a world must outlive the service.
        explicit SandboxService(StudioWorld& world);
        ~SandboxService() override;
        SandboxService(const SandboxService&) = delete;
        SandboxService& operator=(const SandboxService&) = delete;

        bool build(const std::filesystem::path& rigPath, const std::filesystem::path& profilePath,
            const openeden::editor::Vec3& offsetM, double yawRadians, std::string& error) override;
        bool built() const override;
        std::vector<std::string> bodyNames() const override;
        bool push(std::string_view bone, const openeden::editor::Vec3& impulseNs,
            const openeden::editor::Vec3& worldPointM) override;
        std::size_t pendingPushes() const override;
        bool bodyPose(std::string_view bone, openeden::editor::SandboxBodyPose& out) const override;
        bool reset(std::string& error) override;
        void release() override;

        /// The scene, for tests and the host's checks; null when nothing is built.
        const PushScene* scene() const { return mScene.get(); }

        /// The ground body (invalid when nothing is built), for tests.
        JPH::BodyID ground() const { return mGround; }

    private:
        bool addGround(std::string& error);

        StudioWorld& mWorld;
        std::unique_ptr<PushScene> mScene;
        JPH::BodyID mGround;
    };
}

#endif
