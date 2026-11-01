#ifndef OPENCS_VIEW_RENDER_ACTOR_H
#define OPENCS_VIEW_RENDER_ACTOR_H

#include <functional>
#include <memory>
#include <string>

#include <QObject>

#include <vsg/maths/vec3.h>

#include <components/esm/refid.hpp>
#include <components/esm3/loadarmo.hpp>

#include "../../model/world/actoradapter.hpp"

namespace CSMWorld
{
    class Data;
}

namespace MWAnim
{
    class Actor;
    class Object;
    struct Context;
}

namespace CSVRender
{
    /// Makes an npc or creature as ActorAdapter describes it, in its bind pose: the thin caller of MWAnim that
    /// MWRender::Npc is for the game, which needs game objects. Owned by the CSVRender::Object that shows it.
    class Actor : public QObject
    {
        Q_OBJECT
    public:
        /// \param id       The referenceable id
        /// \param data     The data store
        /// \param changed  Called when the actor must be made again
        Actor(
            const ESM::RefId& id, CSMWorld::Data& data, const MWAnim::Context& context, std::function<void()> changed);

        /// (Re)creates the npc or creature renderable, not compiled.
        std::unique_ptr<MWAnim::Object> create();

        /// The race's width and height, for an npc.
        vsg::vec3 getScale() const;

    private slots:
        void handleActorChanged(const ESM::RefId& refId);

    private:
        void attachBodyPart(MWAnim::Actor& actor, ESM::PartReferenceType type, const std::string& mesh);

        std::string getBodyPartMesh(const ESM::RefId& bodyPartId);

        ESM::RefId mId;
        CSMWorld::Data& mData;
        const MWAnim::Context& mContext;
        std::function<void()> mChanged;
        CSMWorld::ActorAdapter::ActorDataPtr mActorData;
    };
}

#endif
