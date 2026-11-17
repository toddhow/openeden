#include "playerbody.hpp"

#include <cmath>
#include <cstdlib>

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/computetransform.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/player_body.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwmechanics/creaturestats.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/ptr.hpp"

#include "riderbob.hpp"
#include "scene.hpp"
#include "skyrimheldgear.hpp"

namespace MWRender
{
    namespace
    {
        /// The node of the horse skeleton the rider sits on (actors/horse/character assets/skeleton.nif, task 0251).
        const std::string kSaddleBoneName = "SaddleBone";
    }

    namespace
    {
        // A source is looked for this often while none was found (a session of another game, or the pack not up yet).
        constexpr std::uint32_t kLookupPeriodFrames = 60;

        // Task 0247: the player's draw state (MWMechanics::DrawState: 0 nothing, 1 weapon, 2 spell). The stub player is
        // an actor whose stats the Lua stance control (F) writes; 0 for anything else.
        std::uint8_t playerDrawState(const MWWorld::Ptr& player)
        {
            if (player.isEmpty() || !player.getClass().isActor())
                return 0;
            return static_cast<std::uint8_t>(player.getClass().getCreatureStats(player).getDrawState());
        }

        // A first-person clip that has not started this long after it was asked for is not waited for.
        constexpr float kUnequipStartSeconds = 3.f;
    }

    PlayerBody::PlayerBody(Scene& scene)
        : mScene(scene)
    {
    }

    PlayerBody::~PlayerBody() = default;

    vsg::ref_ptr<vsg::Node> PlayerBody::node() const
    {
        return mBody ? mBody->node() : vsg::ref_ptr<vsg::Node>();
    }

    openeden::sdk::game::PlayerBodySource* PlayerBody::findSource()
    {
        MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
        NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
        if (store == nullptr)
            return nullptr;
        if (!mGameId.empty())
            return dynamic_cast<openeden::sdk::game::PlayerBodySource*>(store->source(mGameId));
        if (mLookupFrames++ % kLookupPeriodFrames != 0)
            return nullptr;
        for (const std::string& id : store->gameIds())
            if (auto* found = dynamic_cast<openeden::sdk::game::PlayerBodySource*>(store->source(id)))
            {
                mGameId = id;
                return found;
            }
        return nullptr;
    }

    void PlayerBody::rebuild(openeden::sdk::game::PlayerBodySource& source, const MWWorld::Ptr& player,
        std::uint32_t revision, std::uint32_t swingToken)
    {
        openeden::sdk::game::ActorAppearance appearance;
        std::string error;
        if (!source.describePlayerBody(appearance, error))
        {
            Log(Debug::Warning) << "Player body: not drawn: " << error;
            mGate.failed(revision);
            return;
        }
        std::unique_ptr<MWAnim::ObjectAnimation> body = mScene.makeNeutralActorObject(player, appearance, true);
        if (!body || !mScene.context.compileContext->compile(body->node()))
        {
            mGate.failed(revision);
            return;
        }
        // The new body takes the old one's place and pose; a swing under way is not played again from the start.
        if (mBody)
        {
            body->transform()->translation = mBody->transform()->translation;
            body->transform()->rotation = mBody->transform()->rotation;
        }
        mBody = std::move(body);
        mSwing.adopt(swingToken);
        mRidingClip.clear(); // task 0251: a new body is not posed yet
        mGate.built(revision);
        ++mBuilds;
        Log(Debug::Info) << "PLAYERBODY built revision " << revision << ": " << appearance.parts.size() << " parts"
                         << (appearance.female ? ", female" : ", male")
                         << (appearance.heldWeaponModel.empty() ? "" : ", weapon " + appearance.heldWeaponModel)
                         << (appearance.heldShieldModel.empty() ? "" : ", shield " + appearance.heldShieldModel);
    }

    void PlayerBody::update(float dt, const MWWorld::Ptr& player, const Anim::Transform& playerTransform)
    {
        openeden::sdk::game::PlayerBodySource* source = findSource();
        if (source == nullptr)
            return;
        openeden::sdk::game::PlayerBodyState state;
        if (!source->playerBody(state))
            return;
        if (mGate.wants(state.revision))
            rebuild(*source, player, state.revision, state.swingToken);
        if (!mBody)
            return;

        // Where he stands: the stub player's transform (position and heading); the body keeps its own scale.
        Anim::Transform& transform = *mBody->transform();
        transform.translation = playerTransform.translation;
        transform.rotation = playerTransform.rotation;
        // Task 0267: while a paired clip plays the body is drawn at the horse's origin, the player on the saddle.
        if (state.bodyOffsetX != 0.f || state.bodyOffsetY != 0.f || state.bodyOffsetZ != 0.f)
            transform.translation = vsg::vec3(playerTransform.translation.x + state.bodyOffsetX,
                playerTransform.translation.y + state.bodyOffsetY, playerTransform.translation.z + state.bodyOffsetZ);

        // Task 0255 (riding): the body is drawn where the saddle bone really is. The pack carries the player to the
        // saddle's REST place; the gait moves the bone a few units, and the body follows (the player's own position,
        // which the game rules read, does not change). The horse's drawn object is looked up every frame (a cell may
        // unload under a stale Ptr).
        if (state.ridingMount != 0)
        {
            if (!mWasRiding)
            {
                mSaddleWarned = false;
                mHaveBaseline = false;
            }
            mWasRiding = true;
            // The horse's drawn object and its saddle path are looked up when the mount or the scene's objects
            // changed (a walk over every cell), never per frame; the nodes live as long as the object.
            if (state.ridingMount != mMountId || mScene.changeCounter() != mSceneEpoch)
            {
                mMountId = state.ridingMount;
                mSceneEpoch = mScene.changeCounter();
                mMount = mScene.findNeutralObject(mMountId);
                mSaddlePath = mMount.isEmpty() ? std::vector<const Anim::Transform*>()
                                               : mScene.neutralObjectBonePath(mMount, kSaddleBoneName);
            }
            if (!mSaddlePath.empty())
            {
                const vsg::vec3 bone = vsgUtil::computePosition(mSaddlePath);
                // The first sample of a ride (the horse stands idle) is where the pack's saddle point is: the body
                // follows how far the bone has moved from there.
                if (!mHaveBaseline)
                {
                    mBaseline = bone;
                    mHaveBaseline = true;
                }
                const vsg::vec3 offset = riderBobOffset(bone, mBaseline, playerTransform.rotation);
                transform.translation = vsg::vec3(playerTransform.translation.x + offset.x,
                    playerTransform.translation.y + offset.y, playerTransform.translation.z + offset.z);
                // OPENEDEN_RIDING_TRACE (a developer aid): the bob about twice a second.
                static const bool trace = std::getenv("OPENEDEN_RIDING_TRACE") != nullptr;
                if (trace && (++mBobFrames % 30) == 0)
                    Log(Debug::Info) << "RIDING bob " << offset.x << " " << offset.y << " " << offset.z << " (bone "
                                     << bone.x << " " << bone.y << " " << bone.z << ")";
            }
            else if (!mSaddleWarned)
            {
                mSaddleWarned = true;
                Log(Debug::Warning) << "Player body: the horse's '" << kSaddleBoneName
                                    << "' bone was not found: the rider does not follow the gait";
            }
        }
        else
        {
            mWasRiding = false;
            mMountId = 0;
            mMount = MWWorld::Ptr();
            mSaddlePath.clear();
        }

        const float speed = mSpeed.update(playerTransform.translation.x, playerTransform.translation.y, dt);
        Scene::setNeutralObjectSpeed(*mBody, state.ridingClip.empty() ? speed : 0.f);
        // Task 0251 (riding): the pack names the rider clip of the gait while the player sits on a horse; the body
        // loops it (and does not walk). It stays where the stub player is, which the pack carries to the saddle.
        if (state.ridingClip != mRidingClip)
        {
            // Task 0267: a name that starts with '~' is one side of a paired clip: it plays once and holds its last
            // pose.
            if (!state.ridingClip.empty() && state.ridingClip.front() == '~')
                Scene::playNeutralObjectPaired(*mBody, state.ridingClip);
            else
                Scene::setNeutralObjectPose(*mBody, state.ridingClip, false);
            Log(Debug::Info) << "PLAYERBODY riding clip '" << mRidingClip << "' -> '" << state.ridingClip << "'";
            mRidingClip = state.ridingClip;
        }
        if (mSwing.poll(state.swingToken) && !state.swingClip.empty())
        {
            Scene::playNeutralObjectClip(*mBody, state.swingClip, false);
            Log(Debug::Info) << "PLAYERBODY swing " << state.swingToken << " clip " << state.swingClip;
        }
        mBody->update(dt);
    }

    vsg::ref_ptr<vsg::Node> PlayerBody::firstPersonNode() const
    {
        return mArms ? mArms->node() : vsg::ref_ptr<vsg::Node>();
    }

    std::vector<vsg::ref_ptr<const Anim::Transform>> PlayerBody::cameraPath() const
    {
        return mArmsCameraPath;
    }

    void PlayerBody::rebuildArms(openeden::sdk::game::PlayerBodySource& source, const MWWorld::Ptr& player,
        std::uint32_t revision, std::uint32_t swingToken, std::uint32_t castToken, std::uint32_t bowDrawToken,
        std::uint32_t bowReleaseToken, std::uint8_t drawState)
    {
        openeden::sdk::game::ActorAppearance appearance;
        std::string error;
        if (!source.describeFirstPersonBody(appearance, error))
        {
            Log(Debug::Warning) << "Player body: first-person arms not drawn: " << error;
            mArmsGate.failed(revision);
            return;
        }
        std::unique_ptr<MWAnim::ObjectAnimation> arms = mScene.makeFirstPersonArms(player, appearance);
        if (!arms || !mScene.context.compileContext->compile(arms->node()))
        {
            mArmsGate.failed(revision);
            return;
        }
        // The camera node: its place in the rig at rest, and the path the camera follows.
        std::vector<vsg::ref_ptr<const Anim::Transform>> path;
        vsg::vec3 rest(0.f, 0.f, 0.f);
        const std::vector<const Anim::Transform*> nodes
            = arms->worldTransform(std::string(SkyrimFirstPerson::kCameraBone));
        if (nodes.empty())
            Log(Debug::Warning) << "Player body: the first-person skeleton has no camera node '"
                                << SkyrimFirstPerson::kCameraBone << "': the camera stays where it was";
        else
        {
            for (const Anim::Transform* node : nodes)
                path.emplace_back(node);
            // root space: the whole path but the object's own transform
            std::vector<const Anim::Transform*> local(nodes.begin() + 1, nodes.end());
            rest = vsgUtil::computePosition(local);
        }
        mArmsCameraPath = std::move(path);
        mCameraRest = rest;
        mArmsController = {};
        for (const vsg::ref_ptr<vsg::Object>& kept : arms->keepAlive)
            if (auto* controller = dynamic_cast<SkyrimFirstPerson::ArmsController*>(kept.get()))
                mArmsController = vsg::ref_ptr<SkyrimFirstPerson::ArmsController>(controller);
        mArmsWeaponType = appearance.heldWeaponType;
        // A new body does not play the draw clip of what was already drawn.
        mArmsDrawState = drawState;
        mGearPhase = SkyrimFirstPerson::GearPhase::None;
        mGearStarted = false;
        mCastHoldSeen = 0;
        if (mArmsController)
        {
            // Decode the clips the hands will ask for now, so that F does not wait for a read.
            using namespace SkyrimFirstPerson;
            const std::initializer_list<std::string_view> names = { idleClipFor(mArmsWeaponType, Stance::Sheathed),
                idleClipFor(mArmsWeaponType, Stance::Weapon), idleClipFor(mArmsWeaponType, Stance::Spell),
                equipClipFor(mArmsWeaponType, Stance::Weapon), unequipClipFor(mArmsWeaponType, Stance::Weapon),
                equipClipFor(mArmsWeaponType, Stance::Spell), unequipClipFor(mArmsWeaponType, Stance::Spell),
                // task 0249: the spell holds and the release
                "mrh_release", "mrh_selfrelease", "mrh_chargeloop", "mrh_selfchargeloop", "mrh_wardcharge",
                "mrh_aimedconcentration", "mrh_selfconcentration", "mrh_wardloop" };
            for (const std::string_view name : names)
                mArmsController->prefetch(name);
            if (mArmsWeaponType == kBowType) // the bow's draw, drawn-back idle and release
                for (const std::string_view name : { std::string_view("bow_idledrawn"),
                         std::string_view("bow_drawlight"), std::string_view("bow_release") })
                    mArmsController->prefetch(name);
        }
        mArms = std::move(arms);
        mArmsSwing.adopt(swingToken);
        mArmsCast.adopt(castToken);
        mArmsBowDraw.adopt(bowDrawToken);
        mArmsBowRelease.adopt(bowReleaseToken);
        mArmsGate.built(revision);
        ++mArmsBuilds;
        Log(Debug::Info) << "PLAYERBODY first person built revision " << revision << ": " << appearance.parts.size()
                         << " parts" << (appearance.female ? ", female" : ", male")
                         << (appearance.heldWeaponModel.empty() ? "" : ", weapon " + appearance.heldWeaponModel)
                         << ", camera rest (" << rest.x << ", " << rest.y << ", " << rest.z << ")";
    }

    void PlayerBody::updateFirstPerson(float dt, const MWWorld::Ptr& player, const Anim::Transform& playerTransform,
        const vsg::quat& orient, float yaw, bool active)
    {
        if (!active)
            return;
        openeden::sdk::game::PlayerBodySource* source = findSource();
        if (source == nullptr)
            return;
        openeden::sdk::game::PlayerBodyState state;
        if (!source->playerBody(state) || !state.firstPerson)
            return;
        const std::uint8_t drawState = playerDrawState(player);
        if (mArmsGate.wants(state.revision))
            rebuildArms(*source, player, state.revision, state.swingToken, state.castToken, state.bowDrawToken,
                state.bowReleaseToken, drawState);
        if (!mArms)
            return;

        if (std::isfinite(dt) && dt > 0.f)
            mArmsClock += dt;
        // A swing or a cast plays once; the weapon (or the spell hand) stays up for a while after it, as an NPC's.
        if (mArmsController)
        {
            // F draws and sheathes: the hands play the raise or lower clip of what was drawn (a swing or a cast in the
            // same frame replaces it, below). Sheathing ends every hold of the old stance.
            if (drawState != mArmsDrawState)
            {
                const std::string_view clip
                    = SkyrimFirstPerson::drawTransitionClip(mArmsWeaponType, mArmsDrawState, drawState);
                if (!clip.empty())
                {
                    mArmsController->playOnce(clip);
                    Log(Debug::Info) << "PLAYERBODY first person draw state " << int(mArmsDrawState) << " -> "
                                     << int(drawState) << " clip " << clip;
                }
                // The weapon follows the clip's own event (weaponDraw, weaponSheathe), not its start or end.
                mGearPhase = SkyrimFirstPerson::GearPhase::None;
                if (!clip.empty() && drawState == SkyrimFirstPerson::kDrawWeapon)
                    mGearPhase = SkyrimFirstPerson::GearPhase::Equipping;
                else if (!clip.empty() && mArmsDrawState == SkyrimFirstPerson::kDrawWeapon
                    && drawState == SkyrimFirstPerson::kDrawNothing)
                    mGearPhase = SkyrimFirstPerson::GearPhase::Unequipping;
                mGearClip.assign(clip);
                mGearStarted = false;
                mUnequipSince = mArmsClock;
                if (drawState == SkyrimFirstPerson::kDrawNothing)
                {
                    mWeaponUntil = -1.f;
                    mSpellUntil = -1.f;
                }
                mArmsDrawState = drawState;
            }
            if (mArmsSwing.poll(state.swingToken) && !state.swingClip.empty())
            {
                mGearPhase = SkyrimFirstPerson::GearPhase::None;
                mArmsController->playOnce(state.swingClip);
                mWeaponUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
                Log(Debug::Info) << "PLAYERBODY first person swing " << state.swingToken << " clip " << state.swingClip;
            }
            if (mArmsCast.poll(state.castToken) && !state.castClip.empty())
            {
                mGearPhase = SkyrimFirstPerson::GearPhase::None;
                mArmsController->playOnce(state.castClip);
                mSpellUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
                Log(Debug::Info) << "PLAYERBODY first person cast " << state.castToken << " clip " << state.castClip;
            }
            // Task 0249: the bow. A draw plays the draw clip and then the drawn-back idle while the string is held;
            // a release plays the release clip.
            if (mArmsBowDraw.poll(state.bowDrawToken) && !state.bowDrawClip.empty())
            {
                mGearPhase = SkyrimFirstPerson::GearPhase::None;
                mArmsController->playOnce(state.bowDrawClip);
                mWeaponUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
                Log(Debug::Info) << "PLAYERBODY first person bow draw " << state.bowDrawToken << " clip "
                                 << state.bowDrawClip;
            }
            if (mArmsBowRelease.poll(state.bowReleaseToken) && !state.bowReleaseClip.empty())
            {
                mArmsController->playOnce(state.bowReleaseClip);
                mWeaponUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
                Log(Debug::Info) << "PLAYERBODY first person bow release " << state.bowReleaseToken << " clip "
                                 << state.bowReleaseClip;
            }
            // A spell he holds keeps the spell hand up for as long as he holds it and gives the hold's loop.
            std::string_view holdClip;
            float holdSeconds = 0.f;
            if (state.castHold != 0 && !state.castHoldClip.empty())
            {
                holdClip = state.castHoldClip;
                holdSeconds = state.castHoldSeconds;
                mSpellUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
            }
            if (state.castHold != mCastHoldSeen)
            {
                Log(Debug::Info) << "PLAYERBODY first person cast hold " << int(mCastHoldSeen) << " -> "
                                 << int(state.castHold) << " clip " << state.castHoldClip << " after " << holdSeconds
                                 << " s";
                mCastHoldSeen = state.castHold;
            }
            const SkyrimFirstPerson::Stance stance = SkyrimFirstPerson::stanceFor(
                drawState, mArmsClock < mWeaponUntil, mArmsClock < mSpellUntil);
            mArmsController->setIdle(
                SkyrimFirstPerson::handsIdleClip(mArmsWeaponType, stance, state.bowDrawing, holdClip), holdSeconds);
            // The weapon is in the hand while drawn, and during the equip and unequip clips from or until the clip's
            // own event (the sidecar's weaponDraw and weaponSheathe).
            float elapsed = 0.f;
            bool playing = false;
            SkyrimFirstPerson::EventTime event;
            if (mGearPhase != SkyrimFirstPerson::GearPhase::None)
            {
                playing = mArmsController->onceProgress(mGearClip, elapsed);
                if (playing)
                    mGearStarted = true;
                else if (mGearStarted || mArmsClock - mUnequipSince > kUnequipStartSeconds)
                    mGearPhase = SkyrimFirstPerson::GearPhase::None; // played to the end, or never started
                if (mGearPhase != SkyrimFirstPerson::GearPhase::None)
                    event = mArmsController->eventTime(mGearClip, SkyrimFirstPerson::gearEventName(mGearPhase));
            }
            SkyrimHeldGear::setDrawn(mArms->keepAlive,
                SkyrimFirstPerson::weaponShown(stance, mGearPhase, playing, elapsed, event));
        }

        // The arms turn with the view about the rig's camera node, which stands at the eye.
        Anim::Transform& transform = *mArms->transform();
        const vsg::vec3 root = SkyrimFirstPerson::rootTranslation(
            vsg::vec3(playerTransform.translation), yaw, orient, mCameraRest);
        transform.translation = root;
        // Developer aids (task 0245): the opposite turn, and a line a second with where the arms stand.
        static const bool conjugate = std::getenv("OPENEDEN_FIRSTPERSON_CONJ") != nullptr;
        static const bool trace = std::getenv("OPENEDEN_PLAYERBODY_TRACE") != nullptr;
        transform.setAttitude(conjugate ? vsg::quat(-orient.x, -orient.y, -orient.z, orient.w) : orient);
        if (trace && static_cast<int>(mArmsClock) != mTraceSecond)
        {
            mTraceSecond = static_cast<int>(mArmsClock);
            const vsg::vec3 camera
                = mArmsCameraPath.empty() ? vsg::vec3() : vsgUtil::computePosition(mArmsCameraPath);
            const vsg::vec3 forward = orient * vsg::vec3(0.f, 1.f, 0.f);
            Log(Debug::Info) << "PLAYERBODY first person trace feet (" << playerTransform.translation.x << ", "
                             << playerTransform.translation.y << ", " << playerTransform.translation.z << ") yaw "
                             << yaw << " forward (" << forward.x << ", " << forward.y << ", " << forward.z
                             << ") root (" << root.x << ", " << root.y << ", " << root.z << ") camera ("
                             << camera.x << ", " << camera.y << ", " << camera.z << ") clip "
                             << (mArmsController ? mArmsController->current() : std::string());
        }
        mArms->update(dt);
    }
}
