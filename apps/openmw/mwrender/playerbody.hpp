#ifndef OPENMW_MWRENDER_PLAYERBODY_HPP
#define OPENMW_MWRENDER_PLAYERBODY_HPP

// Task 0222 (the player has a Skyrim body): the player's drawn body in third person, for a session whose game pack
// describes it (openeden::sdk::game::PlayerBodySource, `[Game] neutral player body`). The player stays the stub
// record's Player object (the camera and the mechanics hold it); this object is drawn BESIDE it: made by the same
// path as every neutral actor (Scene::makeNeutralActorObject: skeleton, skin, head, worn gear, idle/walk/run
// controllers, held weapon and shield), placed on the player's transform every frame and given
//   * the gait speed the player really moved at (playerbodytrack.hpp GroundSpeed),
//   * the clip of each melee swing the pack began (PlayerBodyState::swingToken and swingClip),
//   * a new body whenever the pack's revision changes (worn armour, weapon, sex).
//
// Ownership: RenderManager owns one (null unless the setting is on). Threads: the main thread only (update runs next
// to Player::update). Nothing here reads a file on its own; the pack reads on its worker and the body's models come
// through Scene::makeNeutralActorObject on the main thread, as an NPC's do.

#include <cstdint>
#include <memory>
#include <string>

#include <vsg/core/ref_ptr.h>

#include <vector>

#include <vsg/maths/quat.h>
#include <vsg/maths/vec3.h>

#include <components/mwanimation/objectanim.hpp>

#include "playerbodytrack.hpp"
#include "skyrimfirstperson.hpp"
#include "skyrimhands.hpp"

namespace vsg
{
    class Node;
}
namespace Anim
{
    class Transform;
}
namespace MWWorld
{
    class Ptr;
}
namespace openeden::sdk::game
{
    class PlayerBodySource;
}
namespace MWRender
{
    class Scene;

    class PlayerBody
    {
    public:
        explicit PlayerBody(Scene& scene);
        ~PlayerBody();
        PlayerBody(const PlayerBody&) = delete;
        PlayerBody& operator=(const PlayerBody&) = delete;

        /// One frame. `player` is the stub Player object's transform (position and heading), `dt` the frame time.
        void update(float dt, const MWWorld::Ptr& player, const Anim::Transform& playerTransform);

        /// The body's scene graph node, or null before it exists. Changes when the body is made again.
        [[nodiscard]] vsg::ref_ptr<vsg::Node> node() const;

        /// Task 0245: the first-person arms. Called every frame the player body is on, before the camera's update.
        /// `active` is true while the camera is in first person: only then are the arms built and updated. `orient`
        /// is the camera's orientation and `yaw` its yaw (Camera::getOrientation, getYaw). The arms are turned by the
        /// whole orientation, about the rig's camera node, and stand so that node is at the eye.
        void updateFirstPerson(float dt, const MWWorld::Ptr& player, const Anim::Transform& playerTransform,
            const vsg::quat& orient, float yaw, bool active);

        /// The arms' scene graph node, or null while there are none.
        [[nodiscard]] vsg::ref_ptr<vsg::Node> firstPersonNode() const;
        /// The path (root first) to the arms' camera node, for Camera::setFirstPersonBodyPath; empty while there are
        /// no arms.
        [[nodiscard]] std::vector<vsg::ref_ptr<const Anim::Transform>> cameraPath() const;

        /// Test and trace aids.
        [[nodiscard]] std::uint32_t builds() const { return mBuilds; }
        [[nodiscard]] std::uint32_t armsBuilds() const { return mArmsBuilds; }

    private:
        openeden::sdk::game::PlayerBodySource* findSource();
        void rebuild(openeden::sdk::game::PlayerBodySource& source, const MWWorld::Ptr& player, std::uint32_t revision,
            std::uint32_t swingToken);

        Scene& mScene;
        std::string mGameId; ///< the game whose source gave the body; empty until one did
        std::uint32_t mLookupFrames = 0;
        std::unique_ptr<MWAnim::ObjectAnimation> mBody;
        PlayerBodyTrack::GroundSpeed mSpeed;
        PlayerBodyTrack::SwingWatch mSwing;
        PlayerBodyTrack::RebuildGate mGate;
        std::uint32_t mBuilds = 0;

        // ---- Task 0245: the first-person arms
        void rebuildArms(openeden::sdk::game::PlayerBodySource& source, const MWWorld::Ptr& player,
            std::uint32_t revision, std::uint32_t swingToken, std::uint32_t castToken, std::uint32_t bowDrawToken,
            std::uint32_t bowReleaseToken, std::uint8_t drawState);
        std::unique_ptr<MWAnim::ObjectAnimation> mArms;
        vsg::ref_ptr<SkyrimFirstPerson::ArmsController> mArmsController;
        std::vector<vsg::ref_ptr<const Anim::Transform>> mArmsCameraPath;
        vsg::vec3 mCameraRest; ///< the camera node's place in the rig at rest (root space)
        PlayerBodyTrack::RebuildGate mArmsGate;
        PlayerBodyTrack::SwingWatch mArmsSwing;
        PlayerBodyTrack::SwingWatch mArmsCast;
        // Task 0249: the bow's draws and releases, and the weapon that follows its clip's own event.
        PlayerBodyTrack::SwingWatch mArmsBowDraw;
        PlayerBodyTrack::SwingWatch mArmsBowRelease;
        SkyrimFirstPerson::GearPhase mGearPhase = SkyrimFirstPerson::GearPhase::None;
        std::string mGearClip; ///< the equip or unequip clip the phase waits on
        bool mGearStarted = false; ///< that clip has been seen playing
        std::uint8_t mCastHoldSeen = 0;
        std::uint8_t mArmsWeaponType = 0;
        float mArmsClock = 0.f; ///< seconds of first-person time; the stance holds are on it
        float mWeaponUntil = -1.f;
        float mSpellUntil = -1.f;
        std::uint32_t mArmsBuilds = 0;
        int mTraceSecond = -1;
        // Task 0247: the engine's draw state the arms last answered (F draws and sheathes). Task 0249: the putting
        // away and the drawing of a weapon are mGearPhase, which follows the clip's own event.
        std::uint8_t mArmsDrawState = 0;
        float mUnequipSince = 0.f;
    };
}

#endif
