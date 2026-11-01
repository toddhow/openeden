#include "actor.hpp"

#include <utility>

#include <apps/opencs/model/world/actoradapter.hpp>
#include <apps/opencs/model/world/idcollection.hpp>
#include <apps/opencs/model/world/record.hpp>

#include <components/animation/context.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/mwanimation/attach.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/create.hpp>
#include <components/mwanimation/decorate.hpp>
#include <components/mwanimation/groups.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/vsgutil/readnode.hpp>

#include "../../model/world/data.hpp"

namespace CSVRender
{
    Actor::Actor(
        const ESM::RefId& id, CSMWorld::Data& data, const MWAnim::Context& context, std::function<void()> changed)
        : mId(id)
        , mData(data)
        , mContext(context)
        , mChanged(std::move(changed))
    {
        mActorData = mData.getActorAdapter()->getActorData(mId);
        connect(mData.getActorAdapter(), &CSMWorld::ActorAdapter::actorChanged, this, &Actor::handleActorChanged);
    }

    std::unique_ptr<MWAnim::Object> Actor::create()
    {
        const std::string skeleton = mActorData->getSkeleton();

        if (mActorData->isCreature())
            return MWAnim::createCreature<MWAnim::Actor>(skeleton, false, mContext);

        // As MWRender::Npc::updateNpcBase: the skeleton gives the bones, and only the body parts go in the scene, which
        // is what the OSG editor's CleanObjectRootVisitor left.
        auto node = mContext.readActor(skeleton);
        auto actor = std::make_unique<MWAnim::Actor>(mContext);
        actor->animation = std::make_unique<MWAnim::Play>(
            MWAnim::Bones(static_cast<const vsg::Node&>(*node), MWAnim::groups), MWAnim::sNumBlendMasks);
        actor->assignBones();

        for (int i = 0; i < ESM::PRT_Count; ++i)
        {
            const auto type = static_cast<ESM::PartReferenceType>(i);
            const std::string mesh = getBodyPartMesh(mActorData->getPart(type));
            if (!mesh.empty())
                attachBodyPart(*actor, type, mesh);
        }
        return actor;
    }

    vsg::vec3 Actor::getScale() const
    {
        if (mActorData->isCreature())
            return vsg::vec3(1.f, 1.f, 1.f);
        const vsg::vec2& attributes = mActorData->getRaceWeightHeight();
        return vsg::vec3(attributes.x, attributes.x, attributes.y);
    }

    void Actor::handleActorChanged(const ESM::RefId& refId)
    {
        if (mId == refId && mChanged)
            mChanged();
    }

    void Actor::attachBodyPart(MWAnim::Actor& actor, ESM::PartReferenceType type, const std::string& mesh)
    {
        // As MWRender::Npc::insertBoundedPart, for a part that has no glow, no light and no equipment logic.
        auto node = vsgUtil::readNode(mesh, mContext.partBoneOptions[type]);
        const std::string& bone = MWAnim::sPartBones.at(type);
        const bool mirror = bone.find("Left") != std::string::npos;
        auto result = MWAnim::decorate(node, nullptr, mirror);
        auto path = MWAnim::attachBonesAndNode(node, *actor.nodeToAddChildrenTo(), actor.animation->bones, bone);
        if (path.empty())
            return;

        Anim::Context context{ { actor.transform() }, path, &actor.animation->bones, &mContext.mask };
        // Every controller, as the game's Npc does with its static controllers: skins are computed by controllers that
        // are not auto-play, and an unskinned mesh draws in pieces.
        result.link(context, [&actor](const Anim::Controller* controller, vsg::Object* target) {
            actor.addController(controller, target);
        });
    }

    std::string Actor::getBodyPartMesh(const ESM::RefId& bodyPartId)
    {
        const auto& bodyParts = mData.getBodyParts();

        const int index = bodyParts.searchId(bodyPartId);
        if (index != -1 && !bodyParts.getRecord(index).isDeleted())
            return bodyParts.getRecord(index).get().mModel;
        return {};
    }
}
