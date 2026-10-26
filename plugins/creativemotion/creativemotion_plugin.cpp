#include "creativemotion_plugin.hpp"
#include "host_physics_world.hpp"

#include <openeden/phases.hpp>
#include <openeden/save.hpp>
#include <openeden/scripting.hpp>
#include <openeden/settings.hpp>
#include <openeden/vfs.hpp>

#include <opas/arbiter/Arbiter.hpp>
#include <opas/behaviors/Balance.hpp>
#include <opas/behaviors/BalanceDriver.hpp>
#include <opas/behaviors/HitReaction.hpp>
#include <opas/behaviors/Stepping.hpp>
#include <opas/ik/TwoBoneIk.hpp>
#include <opas/math/Quat.hpp>
#include <opas/math/Transform.hpp>
#include <opas/math/Vec3.hpp>
#include <opas/motor/MotorSolver.hpp>
#include <opas/motor/Ragdoll.hpp>
#include <opas/reconcile/CapsuleReconciler.hpp>
#include <opas/rig/Profile.hpp>
#include <opas/rig/ProfileFile.hpp>
#include <opas/rig/Rig.hpp>
#include <opas/rig/RigFile.hpp>
#include <opas/sensors/BalanceSensor.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <string>
#include <string_view>
#include <vector>

namespace openeden::creativemotion
{
    namespace
    {
        using namespace openeden::sdk;
        using namespace openeden::contracts;

        constexpr std::string_view kPluginId = "creativemotion";
        constexpr std::string_view kSaveChunkId = "creativemotion";
        constexpr std::uint32_t kSaveChunkVersion = 1;
        constexpr std::uint32_t kSaveMagic = 0x434D5356; // "CMSV"

        constexpr std::string_view kDefaultHumanoidRigJson = R"json({
  "format": "opas.rig",
  "version": 2,
  "name": "humanoid",
  "bones": [
    {"name": "Pelvis", "parent": "", "translationM": [0, 0, 0.95], "rotation": [0, 0, 0, 1]},
    {"name": "Spine", "parent": "Pelvis", "translationM": [0, 0, 0.12], "rotation": [0, 0, 0, 1]},
    {"name": "Chest", "parent": "Spine", "translationM": [0, 0, 0.2], "rotation": [0, 0, 0, 1]},
    {"name": "Neck", "parent": "Chest", "translationM": [0, 0, 0.22], "rotation": [0, 0, 0, 1]},
    {"name": "Head", "parent": "Neck", "translationM": [0, 0, 0.07], "rotation": [0, 0, 0, 1]},
    {"name": "LeftUpperArm", "parent": "Chest", "translationM": [-0.21, 0, 0.17], "rotation": [0, 0, 0, 1]},
    {"name": "LeftForearm", "parent": "LeftUpperArm", "translationM": [0, 0, -0.29], "rotation": [0, 0, 0, 1]},
    {"name": "LeftHand", "parent": "LeftForearm", "translationM": [0, 0, -0.26], "rotation": [0, 0, 0, 1]},
    {"name": "LeftFingers", "parent": "LeftHand", "translationM": [0, 0, -0.1], "rotation": [0, 0, 0, 1]},
    {"name": "RightUpperArm", "parent": "Chest", "translationM": [0.21, 0, 0.17], "rotation": [0, 0, 0, 1]},
    {"name": "RightForearm", "parent": "RightUpperArm", "translationM": [0, 0, -0.29], "rotation": [0, 0, 0, 1]},
    {"name": "RightHand", "parent": "RightForearm", "translationM": [0, 0, -0.26], "rotation": [0, 0, 0, 1]},
    {"name": "RightFingers", "parent": "RightHand", "translationM": [0, 0, -0.1], "rotation": [0, 0, 0, 1]},
    {"name": "LeftThigh", "parent": "Pelvis", "translationM": [-0.09, 0, -0.01], "rotation": [0, 0, 0, 1]},
    {"name": "LeftShin", "parent": "LeftThigh", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]},
    {"name": "LeftFoot", "parent": "LeftShin", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]},
    {"name": "RightThigh", "parent": "Pelvis", "translationM": [0.09, 0, -0.01], "rotation": [0, 0, 0, 1]},
    {"name": "RightShin", "parent": "RightThigh", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]},
    {"name": "RightFoot", "parent": "RightShin", "translationM": [0, 0, -0.43], "rotation": [0, 0, 0, 1]}
  ],
  "bodies": [
    {
      "bone": "Pelvis",
      "shape": {"type": "capsule", "halfHeightM": 0.05, "radiusM": 0.11},
      "translationM": [0, 0, 0.04],
      "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
      "massKg": 8.3775
    },
    {
      "bone": "Spine",
      "shape": {"type": "capsule", "halfHeightM": 0.04, "radiusM": 0.11},
      "translationM": [0, 0, 0.1],
      "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
      "massKg": 12.2475
    },
    {
      "bone": "Chest",
      "shape": {"type": "capsule", "halfHeightM": 0.04, "radiusM": 0.12},
      "translationM": [0, 0, 0.11],
      "rotation": [0, 0.7071067811865476, 0, 0.7071067811865476],
      "massKg": 11.97
    },
    {
      "bone": "Head",
      "shape": {"type": "capsule", "halfHeightM": 0.02, "radiusM": 0.095},
      "translationM": [0, 0, 0.08],
      "rotation": [0, 0, 0, 1],
      "massKg": 5.205
    },
    {
      "bone": "LeftUpperArm",
      "shape": {"type": "capsule", "halfHeightM": 0.1, "radiusM": 0.045},
      "translationM": [0, 0, -0.145],
      "rotation": [0, 0, 0, 1],
      "massKg": 2.0325
    },
    {
      "bone": "LeftForearm",
      "shape": {"type": "capsule", "halfHeightM": 0.09, "radiusM": 0.04},
      "translationM": [0, 0, -0.13],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.215
    },
    {
      "bone": "LeftHand",
      "shape": {"type": "box", "halfExtentsM": [0.02, 0.045, 0.09]},
      "translationM": [0, 0, -0.08],
      "rotation": [0, 0, 0, 1],
      "massKg": 0.4575
    },
    {
      "bone": "RightUpperArm",
      "shape": {"type": "capsule", "halfHeightM": 0.1, "radiusM": 0.045},
      "translationM": [0, 0, -0.145],
      "rotation": [0, 0, 0, 1],
      "massKg": 2.0325
    },
    {
      "bone": "RightForearm",
      "shape": {"type": "capsule", "halfHeightM": 0.09, "radiusM": 0.04},
      "translationM": [0, 0, -0.13],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.215
    },
    {
      "bone": "RightHand",
      "shape": {"type": "box", "halfExtentsM": [0.02, 0.045, 0.09]},
      "translationM": [0, 0, -0.08],
      "rotation": [0, 0, 0, 1],
      "massKg": 0.4575
    },
    {
      "bone": "LeftThigh",
      "shape": {"type": "capsule", "halfHeightM": 0.145, "radiusM": 0.07},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 10.62
    },
    {
      "bone": "LeftShin",
      "shape": {"type": "capsule", "halfHeightM": 0.165, "radiusM": 0.05},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 3.2475
    },
    {
      "bone": "LeftFoot",
      "shape": {"type": "box", "halfExtentsM": [0.045, 0.12, 0.035]},
      "translationM": [0, 0.06, -0.045],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.0275
    },
    {
      "bone": "RightThigh",
      "shape": {"type": "capsule", "halfHeightM": 0.145, "radiusM": 0.07},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 10.62
    },
    {
      "bone": "RightShin",
      "shape": {"type": "capsule", "halfHeightM": 0.165, "radiusM": 0.05},
      "translationM": [0, 0, -0.215],
      "rotation": [0, 0, 0, 1],
      "massKg": 3.2475
    },
    {
      "bone": "RightFoot",
      "shape": {"type": "box", "halfExtentsM": [0.045, 0.12, 0.035]},
      "translationM": [0, 0.06, -0.045],
      "rotation": [0, 0, 0, 1],
      "massKg": 1.0275
    }
  ],
  "joints": [
    {
      "bone": "Spine",
      "twistAxis": [0, 0, 1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.305,
      "planeSwingMaxRad": 0.305,
      "normalSwingMinRad": -0.698,
      "normalSwingMaxRad": 0.218,
      "twistMinRad": -0.39,
      "twistMaxRad": 0.39
    },
    {
      "bone": "Chest",
      "twistAxis": [0, 0, 1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.305,
      "planeSwingMaxRad": 0.305,
      "normalSwingMinRad": -0.698,
      "normalSwingMaxRad": 0.218,
      "twistMinRad": -0.39,
      "twistMaxRad": 0.39
    },
    {
      "bone": "Head",
      "twistAxis": [0, 0, 1],
      "planeAxis": [1, 0, 0],
      "swing": "cone",
      "normalHalfConeRad": 0.785,
      "planeHalfConeRad": 0.785,
      "twistMinRad": -1.047,
      "twistMaxRad": 1.047
    },
    {
      "bone": "LeftUpperArm",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.524,
      "planeSwingMaxRad": 2.967,
      "normalSwingMinRad": -1.047,
      "normalSwingMaxRad": 2.967,
      "twistMinRad": -1.571,
      "twistMaxRad": 1.222
    },
    {
      "bone": "LeftForearm",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": 0,
      "twistMaxRad": 2.618
    },
    {
      "bone": "LeftHand",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.524,
      "planeSwingMaxRad": 0.349,
      "normalSwingMinRad": -1.222,
      "normalSwingMaxRad": 1.396,
      "twistMinRad": -1.396,
      "twistMaxRad": 1.396
    },
    {
      "bone": "RightUpperArm",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -2.967,
      "planeSwingMaxRad": 0.524,
      "normalSwingMinRad": -1.047,
      "normalSwingMaxRad": 2.967,
      "twistMinRad": -1.222,
      "twistMaxRad": 1.571
    },
    {
      "bone": "RightForearm",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": 0,
      "twistMaxRad": 2.618
    },
    {
      "bone": "RightHand",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.349,
      "planeSwingMaxRad": 0.524,
      "normalSwingMinRad": -1.222,
      "normalSwingMaxRad": 1.396,
      "twistMinRad": -1.396,
      "twistMaxRad": 1.396
    },
    {
      "bone": "LeftThigh",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.524,
      "planeSwingMaxRad": 0.785,
      "normalSwingMinRad": -0.524,
      "normalSwingMaxRad": 2.094,
      "twistMinRad": -0.785,
      "twistMaxRad": 0.785
    },
    {
      "bone": "LeftShin",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": -2.356,
      "twistMaxRad": 0
    },
    {
      "bone": "LeftFoot",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.611,
      "planeSwingMaxRad": 0.262,
      "normalSwingMinRad": -0.873,
      "normalSwingMaxRad": 0.349,
      "twistMinRad": -0.175,
      "twistMaxRad": 0.175
    },
    {
      "bone": "RightThigh",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.785,
      "planeSwingMaxRad": 0.524,
      "normalSwingMinRad": -0.524,
      "normalSwingMaxRad": 2.094,
      "twistMinRad": -0.785,
      "twistMaxRad": 0.785
    },
    {
      "bone": "RightShin",
      "twistAxis": [1, 0, 0],
      "planeAxis": [0, 1, 0],
      "swing": "cone",
      "normalHalfConeRad": 0,
      "planeHalfConeRad": 0,
      "twistMinRad": -2.356,
      "twistMaxRad": 0
    },
    {
      "bone": "RightFoot",
      "twistAxis": [0, 0, -1],
      "planeAxis": [1, 0, 0],
      "swing": "pyramid",
      "planeSwingMinRad": -0.262,
      "planeSwingMaxRad": 0.611,
      "normalSwingMinRad": -0.873,
      "normalSwingMaxRad": 0.349,
      "twistMinRad": -0.175,
      "twistMaxRad": 0.175
    }
  ]
})json";

        constexpr std::string_view kDefaultHumanoidProfileJson = R"json({
  "format": "opas.profile",
  "version": 2,
  "rig": "humanoid",
  "defaults": {
    "motor": {"stiffnessNmPerRad": 450, "dampingNmsPerRad": 30, "maxTorqueNm": 500},
    "material": {"friction": 0.8, "restitution": 0, "linearDamping": 0.05, "angularDamping": 0.05}
  },
  "joints": [
    {"bone": "Spine", "motor": {"stiffnessNmPerRad": 900, "dampingNmsPerRad": 60, "maxTorqueNm": 900}},
    {"bone": "Chest", "motor": {"stiffnessNmPerRad": 900, "dampingNmsPerRad": 60, "maxTorqueNm": 900}},
    {"bone": "LeftHand", "motor": {"stiffnessNmPerRad": 50, "dampingNmsPerRad": 5, "maxTorqueNm": 60}},
    {"bone": "RightHand", "motor": {"stiffnessNmPerRad": 50, "dampingNmsPerRad": 5, "maxTorqueNm": 60}}
  ],
  "bodies": [
    {"bone": "LeftFoot", "material": {"friction": 1, "restitution": 0, "linearDamping": 0.05, "angularDamping": 0.05}},
    {"bone": "RightFoot", "material": {"friction": 1, "restitution": 0, "linearDamping": 0.05, "angularDamping": 0.05}}
  ],
  "feet": ["LeftFoot", "RightFoot"],
  "balance": {
    "legMotor": {"stiffnessNmPerRad": 2000, "dampingNmsPerRad": 200, "maxTorqueNm": 5000},
    "ankleGainRadPerM": 2,
    "hipGainRadPerM": 1.5,
    "maxLeanRad": 0.3
  },
  "stepping": {
    "armTimeS": 0.25,
    "stepTimeS": 0.25,
    "maxSteps": 4,
    "liftM": 0.08,
    "stanceHalfWidthM": 0.09,
    "maxStepLegFraction": 0.6,
    "velocityTuning": {"gainNsPerM": 1000, "maxTorqueNm": 400, "afterTouchdownS": 2, "trunkBones": ["Spine", "Chest", "Head"], "ankleOffTiltRad": 0.17},
    "swingGravityCompensation": true
  },
  "hitReaction": {"dropFraction": 0.7, "spread": 0.5, "recoverTimeS": 0.4, "reachJoints": 2},
  "arbiter": {"blendTimeS": 0.3, "fallTiltRad": 0.8, "standingSpeedMps": 0.05, "standingTimeS": 0.25, "restSpeedMps": 0.05, "restTimeS": 0.5},
  "death": {"jointPositionSteps": 2, "linearDampingPerS": 2, "angularDampingPerS": 10}
})json";

        class CreativeMotionAssetReader final : public FileFormatReader
        {
        public:
            bool read(std::string_view virtualPath, std::span<const std::uint8_t> bytes,
                FileError* outError) override
            {
                const std::string_view text(
                    reinterpret_cast<const char*>(bytes.data()), bytes.size());
                if (text.empty() || text.find('{') == std::string_view::npos
                    || text.find("\"format\"") == std::string_view::npos
                    || text.find("\"version\"") == std::string_view::npos)
                {
                    if (outError != nullptr)
                    {
                        outError->file = std::string(virtualPath);
                        outError->line = 1;
                        outError->field = "/";
                        outError->message = "Invalid CreativeMotion JSON asset header";
                    }
                    return false;
                }
                if (text.find("\"opas.rig\"") != std::string_view::npos)
                {
                    const auto res = opas::rig::loadRig(text, virtualPath);
                    if (!res.ok())
                    {
                        if (outError != nullptr)
                        {
                            outError->file = std::string(virtualPath);
                            outError->line = res.error.line;
                            outError->field = res.error.field;
                            outError->message = res.error.message;
                        }
                        return false;
                    }
                }
                else if (text.find("\"opas.profile\"") != std::string_view::npos)
                {
                    const auto res = opas::rig::loadProfile(text, virtualPath);
                    if (!res.ok())
                    {
                        if (outError != nullptr)
                        {
                            outError->file = std::string(virtualPath);
                            outError->line = res.error.line;
                            outError->field = res.error.field;
                            outError->message = res.error.message;
                        }
                        return false;
                    }
                }
                return true;
            }
        };

        struct SavedChunkHeader
        {
            std::uint32_t magic = 0;
            std::uint8_t enabled = 0;
            std::uint8_t pad[3]{};
            std::uint32_t count = 0;
        };

        struct SavedCharacterRecord
        {
            std::uint32_t entityIndex = 0;
            std::uint32_t entityGeneration = 0;
            float rootX = 0.0f;
            float rootY = 0.0f;
            float rootZ = 0.0f;
            float yawRad = 0.0f;
        };

        struct CharacterInstance
        {
            EntityId entity{};
            PhysicsVec3 spawnRootM{};
            float spawnYawRad = 0.0f;
            opas::motor::Ragdoll ragdoll;
            std::unique_ptr<opas::behaviors::BalanceDriver> balanceDriver;
            opas::physics::StepListenerId listenerId{};
            opas::reconcile::CapsuleReconciler reconciler;
            std::size_t pelvisBodyIndex = 0;
        };
    } // namespace

    struct CreativeMotionPlugin::Impl
    {
        Host* host = nullptr;
        std::unique_ptr<HostPhysicsWorld> world;
        bool enabled = false;
        bool footPlanting = false;
        bool lookAt = false;
        bool smootherTurns = false;
        CollisionLayer ragdollLayer = 0;
        CollisionLayer worldLayer = 0;
        std::uint64_t fixedStepCount = 0;

        opas::rig::Rig humanoidRig;
        opas::rig::Profile humanoidProfile;
        bool assetsLoaded = false;

        std::vector<std::unique_ptr<CharacterInstance>> characters;
        opas::physics::BodyId supportFloorBody{};
        std::shared_ptr<SaveChunkHandler> saveHandler;

        void ensureDefaultAssetsLoaded()
        {
            if (assetsLoaded)
            {
                return;
            }
            auto rigRes = opas::rig::loadRig(kDefaultHumanoidRigJson, "humanoid.rig.json");
            auto profRes = opas::rig::loadProfile(
                kDefaultHumanoidProfileJson, "humanoid-default.profile.json");
            if (rigRes.ok() && profRes.ok())
            {
                humanoidRig = std::move(*rigRes.value);
                humanoidProfile = std::move(*profRes.value);
                assetsLoaded = true;
            }
        }

        [[nodiscard]] CharacterInstance* findCharacter(EntityId entity) noexcept
        {
            for (const auto& ch : characters)
            {
                if (ch->entity == entity)
                {
                    return ch.get();
                }
            }
            return nullptr;
        }

        [[nodiscard]] const CharacterInstance* findCharacter(EntityId entity) const noexcept
        {
            for (const auto& ch : characters)
            {
                if (ch->entity == entity)
                {
                    return ch.get();
                }
            }
            return nullptr;
        }

        void ensureSupportFloor(PhysicsVec3 rootPos)
        {
            if (world == nullptr || supportFloorBody.isValid())
            {
                return;
            }
            const auto hit = world->castRay(
                { rootPos.x, rootPos.y, rootPos.z + 0.5f }, { 0.0f, 0.0f, -2.0f });
            if (!hit)
            {
                opas::physics::BodyDesc floorDesc{};
                floorDesc.shape = opas::physics::BoxShape{ { 50.0f, 50.0f, 0.5f } };
                floorDesc.motion = opas::physics::MotionType::Static;
                floorDesc.positionM = { rootPos.x, rootPos.y, rootPos.z - 0.5f };
                floorDesc.friction = 0.8f;
                const auto created = world->createBody(floorDesc);
                if (created.ok())
                {
                    supportFloorBody = created.id;
                }
            }
        }

        void clearCharacters()
        {
            if (world != nullptr)
            {
                for (auto& ch : characters)
                {
                    if (ch->listenerId.isValid())
                    {
                        world->removeStepListener(ch->listenerId);
                    }
                    opas::motor::destroyRagdoll(*world, ch->ragdoll);
                }
                if (supportFloorBody.isValid())
                {
                    world->destroyBody(supportFloorBody);
                    supportFloorBody = {};
                }
            }
            characters.clear();
        }

        void onFixedStepPrePhysics(float dt, std::uint64_t /*stepIndex*/)
        {
            if (!enabled || world == nullptr)
            {
                return;
            }
            world->runPreStepListeners(dt);
        }

        void onFixedStepPostPhysics(float dt, std::uint64_t /*stepIndex*/)
        {
            if (!enabled || world == nullptr)
            {
                return;
            }
            world->runPostStepListeners(dt);

            for (auto& ch : characters)
            {
                if (ch->pelvisBodyIndex < ch->ragdoll.bodies.size())
                {
                    opas::physics::BodyState pelvisState{};
                    if (world->readBody(ch->ragdoll.bodies[ch->pelvisBodyIndex].id, pelvisState))
                    {
                        const opas::math::Transform pelvisWorld{
                            opas::math::Vec3{ static_cast<double>(pelvisState.positionM.x),
                                static_cast<double>(pelvisState.positionM.y),
                                static_cast<double>(pelvisState.positionM.z) },
                            opas::math::Quat{ static_cast<double>(pelvisState.rotation.x),
                                static_cast<double>(pelvisState.rotation.y),
                                static_cast<double>(pelvisState.rotation.z),
                                static_cast<double>(pelvisState.rotation.w) }
                        };
                        const auto arbState = (ch->balanceDriver && ch->balanceDriver->state())
                            ? *ch->balanceDriver->state()
                            : opas::arbiter::ArbiterState::Standing;
                        ch->reconciler.update(
                            pelvisWorld, arbState, ch->spawnRootM.z, dt);
                    }
                }
            }

            ++fixedStepCount;
        }

        class CreativeMotionSaveHandler final : public SaveChunkHandler
        {
        public:
            explicit CreativeMotionSaveHandler(CreativeMotionPlugin& plugin,
                CreativeMotionPlugin::Impl& impl)
                : mPlugin(plugin)
                , mImpl(impl)
            {
            }

            [[nodiscard]] std::uint32_t currentVersion() const noexcept override
            {
                return kSaveChunkVersion;
            }

            void writeSaveChunk(SaveChunkWriter& writer) override
            {
                SavedChunkHeader hdr{};
                hdr.magic = kSaveMagic;
                hdr.enabled = mImpl.enabled ? 1u : 0u;
                hdr.count = static_cast<std::uint32_t>(mImpl.characters.size());
                writer.write(&hdr, sizeof(hdr));

                for (const auto& ch : mImpl.characters)
                {
                    SavedCharacterRecord rec{};
                    rec.entityIndex = ch->entity.index;
                    rec.entityGeneration = ch->entity.generation;
                    rec.rootX = ch->spawnRootM.x;
                    rec.rootY = ch->spawnRootM.y;
                    rec.rootZ = ch->spawnRootM.z;
                    rec.yawRad = ch->spawnYawRad;
                    writer.write(&rec, sizeof(rec));
                }
            }

            void readSaveChunk(std::uint32_t chunkVersion, SaveChunkReader& reader) override
            {
                if (chunkVersion != kSaveChunkVersion || reader.size() < sizeof(SavedChunkHeader))
                {
                    return;
                }
                SavedChunkHeader hdr{};
                reader.read(&hdr, sizeof(hdr));
                if (hdr.magic != kSaveMagic || hdr.count > 1024u)
                {
                    return;
                }
                const std::size_t expectedBytes
                    = sizeof(SavedChunkHeader) + hdr.count * sizeof(SavedCharacterRecord);
                if (reader.size() < expectedBytes)
                {
                    return;
                }

                mImpl.clearCharacters();
                mPlugin.setEnabled(hdr.enabled != 0u);

                for (std::uint32_t i = 0; i < hdr.count; ++i)
                {
                    SavedCharacterRecord rec{};
                    reader.read(&rec, sizeof(rec));
                    const EntityId entity{ rec.entityIndex, rec.entityGeneration };
                    mPlugin.spawnCharacter(
                        entity, PhysicsVec3{ rec.rootX, rec.rootY, rec.rootZ }, rec.yawRad);
                }
            }

        private:
            CreativeMotionPlugin& mPlugin;
            CreativeMotionPlugin::Impl& mImpl;
        };
    };

    CreativeMotionPlugin::CreativeMotionPlugin()
        : mImpl(std::make_unique<Impl>())
    {
        mImpl->characters.reserve(32);
    }

    CreativeMotionPlugin::~CreativeMotionPlugin() = default;

    void CreativeMotionPlugin::registerServices(Registrar& registrar)
    {
        registrar.provide<AnimationContract>(*this);

        // Fixed-step and frame phase callbacks
        registrar.registerPhaseCallback(Phase::FixedStepPrePhysics,
            FixedStepPhaseCallback([this](float dt, std::uint64_t stepIdx) {
                mImpl->onFixedStepPrePhysics(dt, stepIdx);
            }));
        registrar.registerPhaseCallback(Phase::FixedStepPostPhysics,
            FixedStepPhaseCallback([this](float dt, std::uint64_t stepIdx) {
                mImpl->onFixedStepPostPhysics(dt, stepIdx);
            }));
        registrar.registerPhaseCallback(
            Phase::PostSimulation, PhaseCallback([](float /*dt*/) {}));
        registrar.registerPhaseCallback(Phase::Animation, PhaseCallback([](float /*dt*/) {}));
        registrar.registerPhaseCallback(Phase::PreRender, PhaseCallback([](float /*dt*/) {}));
    }

    bool CreativeMotionPlugin::resolve(const ServiceLocator& /*locator*/)
    {
        return true;
    }

    void CreativeMotionPlugin::start(Host& host)
    {
        mImpl->host = &host;
        mImpl->fixedStepCount = 0;
        mImpl->ensureDefaultAssetsLoaded();

        if (SettingsService* settings = host.getSettingsService())
        {
            mImpl->enabled = settings->getBool(kPluginId, "enabled", false);
            mImpl->footPlanting = settings->getBool(kPluginId, "footPlanting", false);
            mImpl->lookAt = settings->getBool(kPluginId, "lookAt", false);
            mImpl->smootherTurns = settings->getBool(kPluginId, "smootherTurns", false);
        }
        else
        {
            mImpl->enabled = false;
            mImpl->footPlanting = false;
            mImpl->lookAt = false;
            mImpl->smootherTurns = false;
        }

        if (PhysicsService* physics = host.getPhysicsService())
        {
            mImpl->world = std::make_unique<HostPhysicsWorld>(*physics);
            mImpl->ragdollLayer = physics->registerCollisionLayer("creativemotion.ragdoll");
            mImpl->worldLayer = physics->registerCollisionLayer("creativemotion.world");
            physics->declareCollisionPair("creativemotion.ragdoll", "creativemotion.world");
            physics->declareCollisionPair("creativemotion.ragdoll", "creativemotion.ragdoll");
        }

        if (VfsService* vfs = host.getVfsService())
        {
            auto reader = std::make_shared<CreativeMotionAssetReader>();
            for (const std::string_view ext :
                { ".rig.json", ".profile.json", ".style.json", ".animgraph.json" })
            {
                vfs->registerFormatReader(ext, reader);
            }
        }

        if (SaveService* save = host.getSaveService())
        {
            mImpl->saveHandler = std::make_shared<Impl::CreativeMotionSaveHandler>(*this, *mImpl);
            save->registerHandler(kSaveChunkId, mImpl->saveHandler);
        }

        if (ScriptingService* scripting = host.getScriptingService())
        {
            scripting->registerModule(kPluginId, [this](LuaModuleRegistrar& registrar) {
                LuaTable& module = registrar.getTable();
                module.setString("pluginId", "creativemotion");
                module.setString("contract", "openeden.animation");
                module.setBool("enabled", mImpl->enabled);
                module.setBool("footPlanting", mImpl->footPlanting);
                module.setBool("lookAt", mImpl->lookAt);
                module.setBool("smootherTurns", mImpl->smootherTurns);
                module.setFunction("isEnabled", [](void*) { return 0; });
            });
        }
    }

    void CreativeMotionPlugin::stop()
    {
        if (mImpl->host != nullptr)
        {
            if (VfsService* vfs = mImpl->host->getVfsService())
            {
                for (const std::string_view ext :
                    { ".rig.json", ".profile.json", ".style.json", ".animgraph.json" })
                {
                    vfs->registerFormatReader(ext, nullptr);
                }
            }
            if (SaveService* save = mImpl->host->getSaveService())
            {
                save->unregisterHandler(kSaveChunkId);
            }
            if (ScriptingService* scripting = mImpl->host->getScriptingService())
            {
                scripting->unregisterModule(kPluginId);
            }
        }
        mImpl->saveHandler.reset();
        mImpl->clearCharacters();
        mImpl->world.reset();
        mImpl->host = nullptr;
        mImpl->enabled = false;
    }

    bool CreativeMotionPlugin::isEnabled() const noexcept
    {
        return mImpl->enabled;
    }

    void CreativeMotionPlugin::setEnabled(bool enabled)
    {
        mImpl->enabled = enabled;
        if (mImpl->host != nullptr)
        {
            if (SettingsService* settings = mImpl->host->getSettingsService())
            {
                settings->setBool(kPluginId, "enabled", enabled);
            }
        }
    }

    std::size_t CreativeMotionPlugin::getActiveCharacterCount() const noexcept
    {
        return mImpl->characters.size();
    }

    bool CreativeMotionPlugin::isFootPlantingEnabled() const noexcept
    {
        return mImpl->footPlanting;
    }

    bool CreativeMotionPlugin::isLookAtEnabled() const noexcept
    {
        return mImpl->lookAt;
    }

    bool CreativeMotionPlugin::isSmootherTurnsEnabled() const noexcept
    {
        return mImpl->smootherTurns;
    }

    bool CreativeMotionPlugin::spawnCharacter(
        EntityId entity, PhysicsVec3 rootPositionM, float yawRad)
    {
        if (!entity.isValid() || mImpl->world == nullptr)
        {
            return false;
        }
        mImpl->ensureDefaultAssetsLoaded();
        if (!mImpl->assetsLoaded || mImpl->findCharacter(entity) != nullptr)
        {
            return false;
        }

        mImpl->ensureSupportFloor(rootPositionM);

        const opas::math::Transform rootWorld{
            opas::math::Vec3{ static_cast<double>(rootPositionM.x),
                static_cast<double>(rootPositionM.y), static_cast<double>(rootPositionM.z) },
            opas::math::fromAxisAngle(opas::math::Vec3{ 0.0, 0.0, 1.0 }, static_cast<double>(yawRad))
        };

        auto built = opas::motor::buildRagdoll(*mImpl->world, mImpl->humanoidRig,
            mImpl->humanoidProfile, rootWorld,
            opas::motor::RagdollOptions{ opas::motor::RootMotion::Dynamic });
        if (!built.ok())
        {
            return false;
        }

        auto instance = std::make_unique<CharacterInstance>();
        instance->entity = entity;
        instance->spawnRootM = rootPositionM;
        instance->spawnYawRad = yawRad;
        instance->ragdoll = std::move(*built.ragdoll);
        instance->pelvisBodyIndex
            = opas::rig::findBody(mImpl->humanoidRig, "Pelvis").value_or(0);
        instance->reconciler.reset(
            opas::math::Vec3{ static_cast<double>(rootPositionM.x),
                static_cast<double>(rootPositionM.y),
                static_cast<double>(rootPositionM.z + 0.85f) },
            yawRad);

        std::vector<opas::math::Transform> restPose(mImpl->humanoidRig.skeleton.bones.size());
        for (std::size_t i = 0; i < restPose.size(); ++i)
        {
            restPose[i] = mImpl->humanoidRig.skeleton.bones[i].localRest;
        }
        opas::motor::PoseSource poseSource(
            [restPose](double /*timeS*/, std::span<opas::math::Transform> outPose) {
                std::copy_n(
                    restPose.begin(), std::min(restPose.size(), outPose.size()), outPose.begin());
            });

        std::string err;
        auto sensor = opas::sensors::BalanceSensor::create(
            mImpl->humanoidRig, mImpl->humanoidProfile, instance->ragdoll, 9.81, &err);
        auto balance
            = opas::behaviors::Balance::create(mImpl->humanoidRig, mImpl->humanoidProfile, &err);
        auto stepping
            = opas::behaviors::Stepping::create(mImpl->humanoidRig, mImpl->humanoidProfile, &err);
        auto hitReaction = opas::behaviors::HitReaction::create(
            mImpl->humanoidRig, mImpl->humanoidProfile, &err);
        auto arbiter = opas::arbiter::Arbiter::create(mImpl->humanoidProfile, &err);

        if (sensor && balance)
        {
            instance->balanceDriver = std::make_unique<opas::behaviors::BalanceDriver>(
                instance->ragdoll, std::move(*sensor), std::move(*balance), std::move(poseSource),
                std::move(stepping), std::move(hitReaction), arbiter);
            instance->listenerId = mImpl->world->addStepListener(*instance->balanceDriver);
        }

        mImpl->characters.push_back(std::move(instance));
        return true;
    }

    bool CreativeMotionPlugin::despawnCharacter(EntityId entity)
    {
        if (mImpl->world == nullptr)
        {
            return false;
        }
        const auto it = std::find_if(mImpl->characters.begin(), mImpl->characters.end(),
            [entity](const std::unique_ptr<CharacterInstance>& ch) {
                return ch->entity == entity;
            });
        if (it == mImpl->characters.end())
        {
            return false;
        }

        if ((*it)->listenerId.isValid())
        {
            mImpl->world->removeStepListener((*it)->listenerId);
        }
        opas::motor::destroyRagdoll(*mImpl->world, (*it)->ragdoll);
        mImpl->characters.erase(it);

        if (mImpl->characters.empty() && mImpl->supportFloorBody.isValid())
        {
            mImpl->world->destroyBody(mImpl->supportFloorBody);
            mImpl->supportFloorBody = {};
        }
        return true;
    }

    bool CreativeMotionPlugin::hasCharacter(EntityId entity) const noexcept
    {
        return mImpl->findCharacter(entity) != nullptr;
    }

    bool CreativeMotionPlugin::applyHitImpulse(
        EntityId entity, PhysicsVec3 impulseNs, PhysicsVec3 contactPointM)
    {
        if (mImpl->world == nullptr)
        {
            return false;
        }
        CharacterInstance* ch = mImpl->findCharacter(entity);
        if (ch == nullptr || ch->pelvisBodyIndex >= ch->ragdoll.bodies.size())
        {
            return false;
        }
        if (ch->balanceDriver != nullptr)
        {
            ch->balanceDriver->reportHit(ch->pelvisBodyIndex);
        }
        return mImpl->world->applyImpulse(
            ch->ragdoll.bodies[ch->pelvisBodyIndex].id,
            { impulseNs.x, impulseNs.y, impulseNs.z },
            { contactPointM.x, contactPointM.y, contactPointM.z });
    }

    std::optional<PhysicsVec3> CreativeMotionPlugin::getPelvisPosition(EntityId entity) const
    {
        if (mImpl->world == nullptr)
        {
            return std::nullopt;
        }
        const CharacterInstance* ch = mImpl->findCharacter(entity);
        if (ch == nullptr || ch->pelvisBodyIndex >= ch->ragdoll.bodies.size())
        {
            return std::nullopt;
        }
        opas::physics::BodyState state{};
        if (!mImpl->world->readBody(ch->ragdoll.bodies[ch->pelvisBodyIndex].id, state))
        {
            return std::nullopt;
        }
        return PhysicsVec3{ state.positionM.x, state.positionM.y, state.positionM.z };
    }

    std::optional<PhysicsVec3> CreativeMotionPlugin::getReconciledCapsulePosition(
        EntityId entity) const
    {
        const CharacterInstance* ch = mImpl->findCharacter(entity);
        if (ch == nullptr)
        {
            return std::nullopt;
        }
        const auto& pos = ch->reconciler.position();
        return PhysicsVec3{ static_cast<float>(pos.x), static_cast<float>(pos.y),
            static_cast<float>(pos.z) };
    }

    std::string_view CreativeMotionPlugin::getCharacterStateName(EntityId entity) const
    {
        const CharacterInstance* ch = mImpl->findCharacter(entity);
        if (ch == nullptr)
        {
            return {};
        }
        if (ch->balanceDriver != nullptr && ch->balanceDriver->state().has_value())
        {
            return opas::arbiter::stateName(*ch->balanceDriver->state());
        }
        return "Standing";
    }

    HostPhysicsWorld* CreativeMotionPlugin::getHostPhysicsWorld() noexcept
    {
        return mImpl->world.get();
    }

    const HostPhysicsWorld* CreativeMotionPlugin::getHostPhysicsWorld() const noexcept
    {
        return mImpl->world.get();
    }

    CollisionLayer CreativeMotionPlugin::getRagdollCollisionLayer() const noexcept
    {
        return mImpl->ragdollLayer;
    }

    CollisionLayer CreativeMotionPlugin::getWorldCollisionLayer() const noexcept
    {
        return mImpl->worldLayer;
    }

    std::uint64_t CreativeMotionPlugin::getFixedStepCount() const noexcept
    {
        return mImpl->fixedStepCount;
    }

    namespace
    {
        inline bool isFiniteVec(const PhysicsVec3& v) noexcept
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        inline float vecLen(const PhysicsVec3& v) noexcept
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        inline float vecDist(const PhysicsVec3& a, const PhysicsVec3& b) noexcept
        {
            return vecLen({ a.x - b.x, a.y - b.y, a.z - b.z });
        }

        inline float vecDot(const PhysicsVec3& a, const PhysicsVec3& b) noexcept
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        inline PhysicsVec3 vecCross(const PhysicsVec3& a, const PhysicsVec3& b) noexcept
        {
            return {
                a.y * b.z - a.z * b.y,
                a.z * b.x - a.x * b.z,
                a.x * b.y - a.y * b.x
            };
        }

        inline opas::math::Vec3 toOpasVec(const PhysicsVec3& v) noexcept
        {
            return { static_cast<double>(v.x), static_cast<double>(v.y), static_cast<double>(v.z) };
        }

        inline PhysicsVec3 fromOpasVec(const opas::math::Vec3& v) noexcept
        {
            return { static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z) };
        }

        inline PhysicsQuat fromOpasQuat(const opas::math::Quat& q) noexcept
        {
            return { static_cast<float>(q.x), static_cast<float>(q.y), static_cast<float>(q.z), static_cast<float>(q.w) };
        }
    } // namespace

    AnimationContract::TwoBoneResult CreativeMotionPlugin::solveTwoBone(
        const TwoBoneChain& chain, PhysicsVec3 target, PhysicsVec3 poleDirection,
        const HingeLimits& limits) const
    {
        TwoBoneResult result;
        result.valid = false;
        result.reached = false;
        result.mid = chain.mid;
        result.end = chain.end;
        result.bendRad = 0.0f;
        result.rootRotation = { 0.0f, 0.0f, 0.0f, 1.0f };
        result.midRotation = { 0.0f, 0.0f, 0.0f, 1.0f };

        if (!isFiniteVec(chain.root) || !isFiniteVec(chain.mid) || !isFiniteVec(chain.end)
            || !isFiniteVec(target) || !isFiniteVec(poleDirection)
            || !std::isfinite(limits.minBendRad) || !std::isfinite(limits.maxBendRad)
            || limits.minBendRad < 0.0f || limits.maxBendRad > std::numbers::pi_v<float> + 1e-4f
            || limits.minBendRad > limits.maxBendRad)
        {
            return result;
        }

        const opas::ik::TwoBoneChain opasChain{
            toOpasVec(chain.root),
            toOpasVec(chain.mid),
            toOpasVec(chain.end)
        };
        const opas::ik::HingeLimits opasLimits{
            std::clamp(static_cast<double>(limits.minBendRad), 0.0, std::numbers::pi),
            std::clamp(static_cast<double>(limits.maxBendRad), 0.0, std::numbers::pi)
        };

        const opas::ik::TwoBoneResult opasResult = opas::ik::solveTwoBone(
            opasChain, toOpasVec(target), toOpasVec(poleDirection), opasLimits);

        result.valid = opasResult.valid;
        result.reached = opasResult.reached;
        if (opasResult.valid)
        {
            result.mid = fromOpasVec(opasResult.mid);
            result.end = fromOpasVec(opasResult.end);
            result.bendRad = static_cast<float>(opasResult.bendRad);
            result.rootRotation = fromOpasQuat(opasResult.rootRotation);
            result.midRotation = fromOpasQuat(opasResult.midRotation);
        }
        else
        {
            result.mid = chain.mid;
            result.end = chain.end;
        }
        return result;
    }

    AnimationContract::FootPlacementResult CreativeMotionPlugin::solveFootPlacement(
        const FootPlacementInput& input) const
    {
        FootPlacementResult result{};
        result.footAlignment = { 0.0f, 0.0f, 0.0f, 1.0f };
        result.pelvisOffsetZM = 0.0f;

        const float l1 = vecDist(input.legChain.root, input.legChain.mid);
        const float l2 = vecDist(input.legChain.mid, input.legChain.end);
        const float minBend = std::clamp(input.kneeLimits.minBendRad, 0.0f, std::numbers::pi_v<float>);
        const float maxReach = std::sqrt(std::max(0.0f, l1 * l1 + l2 * l2 + 2.0f * l1 * l2 * std::cos(minBend)));

        PhysicsVec3 targetAnkle = input.legChain.end;
        targetAnkle.z = input.legChain.root.z - input.groundDistanceM + input.nominalHeightM;

        const float targetDist = vecDist(input.legChain.root, targetAnkle);
        TwoBoneChain adjustedChain = input.legChain;
        if (targetDist > maxReach && maxReach > 0.0f)
        {
            result.pelvisOffsetZM = -(targetDist - maxReach + 1e-4f);
            adjustedChain.root.z += result.pelvisOffsetZM;
            adjustedChain.mid.z += result.pelvisOffsetZM;
            adjustedChain.end.z += result.pelvisOffsetZM;
        }

        result.ik = solveTwoBone(adjustedChain, targetAnkle, input.poleDirection, input.kneeLimits);

        const float normalLen = vecLen(input.groundNormal);
        if (normalLen > 1e-4f)
        {
            const PhysicsVec3 n{
                input.groundNormal.x / normalLen,
                input.groundNormal.y / normalLen,
                input.groundNormal.z / normalLen
            };
            const PhysicsVec3 up{ 0.0f, 0.0f, 1.0f };
            const float d = vecDot(up, n);
            if (d >= 0.999999f)
            {
                result.footAlignment = { 0.0f, 0.0f, 0.0f, 1.0f };
            }
            else if (d <= -0.999999f)
            {
                result.footAlignment = { 1.0f, 0.0f, 0.0f, 0.0f };
            }
            else
            {
                const PhysicsVec3 axis = vecCross(up, n);
                PhysicsQuat q{ axis.x, axis.y, axis.z, 1.0f + d };
                const float qLen = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
                if (qLen > 0.0f)
                {
                    q.x /= qLen;
                    q.y /= qLen;
                    q.z /= qLen;
                    q.w /= qLen;
                }
                result.footAlignment = q;
            }
        }
        return result;
    }

    AnimationContract::LookAtResult CreativeMotionPlugin::solveLookAt(
        const LookAtInput& input) const
    {
        LookAtResult result{};
        result.yawRad = input.currentYawRad;
        result.pitchRad = input.currentPitchRad;
        result.inRange = false;

        const PhysicsVec3 delta{
            input.targetPosition.x - input.eyePosition.x,
            input.targetPosition.y - input.eyePosition.y,
            input.targetPosition.z - input.eyePosition.z
        };
        const float distSq = delta.x * delta.x + delta.y * delta.y + delta.z * delta.z;
        if (distSq < 1e-6f || !isFiniteVec(delta))
        {
            return result;
        }

        const float rxy = std::sqrt(delta.x * delta.x + delta.y * delta.y);
        const float targetYaw = std::atan2(delta.x, delta.y);
        const float targetPitch = std::atan2(delta.z, rxy);

        result.inRange = (std::abs(targetYaw) <= input.maxYawRad)
            && (std::abs(targetPitch) <= input.maxPitchRad);

        const float clampedYaw = std::clamp(targetYaw, -input.maxYawRad, input.maxYawRad);
        const float clampedPitch = std::clamp(targetPitch, -input.maxPitchRad, input.maxPitchRad);

        const float dt = std::max(0.0f, input.dt);
        const float decay = std::exp(-input.speedRadPerS * dt);

        result.yawRad = clampedYaw + (input.currentYawRad - clampedYaw) * decay;
        result.pitchRad = clampedPitch + (input.currentPitchRad - clampedPitch) * decay;
        return result;
    }

    float CreativeMotionPlugin::solveTurnSmoothing(
        const TurnSmoothingInput& input) const
    {
        if (input.dt <= 0.0f || !std::isfinite(input.dt))
        {
            return input.currentYawRad;
        }
        if (input.halfLifeS <= 0.0f || !std::isfinite(input.halfLifeS))
        {
            return input.targetYawRad;
        }

        constexpr float kPi = std::numbers::pi_v<float>;
        constexpr float kTwoPi = 2.0f * kPi;

        float diff = std::fmod(input.targetYawRad - input.currentYawRad + kPi, kTwoPi);
        if (diff < 0.0f)
        {
            diff += kTwoPi;
        }
        const float shortestDelta = diff - kPi;

        const float decay = std::exp(-std::numbers::ln2_v<float> * input.dt / input.halfLifeS);
        float newYaw = input.currentYawRad + shortestDelta * (1.0f - decay);

        while (newYaw > kPi)
        {
            newYaw -= kTwoPi;
        }
        while (newYaw < -kPi)
        {
            newYaw += kTwoPi;
        }
        return newYaw;
    }
} // namespace openeden::creativemotion

#ifdef OPENEDEN_SHARED_PLUGIN
extern "C" OPENEDEN_PLUGIN_EXPORT const openeden::sdk::PluginDescriptor* openeden_plugin_entry(
    const openeden::sdk::HostInfo* host)
{
    if (host == nullptr || !openeden::sdk::isCompatible(host->sdkVersion, openeden::sdk::kSdkVersion))
    {
        return nullptr;
    }

    static const openeden::sdk::PluginDescriptor sDescriptor{
        openeden::sdk::kSdkVersion,
        "creativemotion",
        []() -> std::unique_ptr<openeden::sdk::Plugin> {
            return std::make_unique<openeden::creativemotion::CreativeMotionPlugin>();
        },
    };
    return &sDescriptor;
}
#endif
