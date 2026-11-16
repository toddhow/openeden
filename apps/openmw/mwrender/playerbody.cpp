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
#include "../mwworld/ptr.hpp"

#include "scene.hpp"
#include "skyrimheldgear.hpp"

namespace MWRender
{
    namespace
    {
        // A source is looked for this often while none was found (a session of another game, or the pack not up yet).
        constexpr std::uint32_t kLookupPeriodFrames = 60;
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

        const float speed = mSpeed.update(playerTransform.translation.x, playerTransform.translation.y, dt);
        Scene::setNeutralObjectSpeed(*mBody, speed);
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
        std::uint32_t revision, std::uint32_t swingToken, std::uint32_t castToken)
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
        mArms = std::move(arms);
        mArmsSwing.adopt(swingToken);
        mArmsCast.adopt(castToken);
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
        if (mArmsGate.wants(state.revision))
            rebuildArms(*source, player, state.revision, state.swingToken, state.castToken);
        if (!mArms)
            return;

        if (std::isfinite(dt) && dt > 0.f)
            mArmsClock += dt;
        // A swing or a cast plays once; the weapon (or the spell hand) stays up for a while after it, as an NPC's.
        if (mArmsController)
        {
            if (mArmsSwing.poll(state.swingToken) && !state.swingClip.empty())
            {
                mArmsController->playOnce(state.swingClip);
                mWeaponUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
                SkyrimHeldGear::noteCombat(mArms->keepAlive);
                Log(Debug::Info) << "PLAYERBODY first person swing " << state.swingToken << " clip " << state.swingClip;
            }
            if (mArmsCast.poll(state.castToken) && !state.castClip.empty())
            {
                mArmsController->playOnce(state.castClip);
                mSpellUntil = mArmsClock + SkyrimHeldGear::kDrawnHoldSeconds;
                Log(Debug::Info) << "PLAYERBODY first person cast " << state.castToken << " clip " << state.castClip;
            }
            const SkyrimFirstPerson::Stance stance = mArmsClock < mSpellUntil ? SkyrimFirstPerson::Stance::Spell
                : mArmsClock < mWeaponUntil                                    ? SkyrimFirstPerson::Stance::Weapon
                                                                                : SkyrimFirstPerson::Stance::Sheathed;
            mArmsController->setIdle(SkyrimFirstPerson::idleClipFor(mArmsWeaponType, stance));
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
