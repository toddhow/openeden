#include "playerbody.hpp"

#include <components/animation/transform.hpp>
#include <components/debug/debuglog.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/worldmodel/store.hpp>

#include <openeden/game/player_body.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwworld/ptr.hpp"

#include "scene.hpp"

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
}
