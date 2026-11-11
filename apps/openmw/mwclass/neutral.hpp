#ifndef OPENMW_MWCLASS_NEUTRAL_H
#define OPENMW_MWCLASS_NEUTRAL_H

#include "../mwworld/class.hpp"
#include "../mwworld/registeredclass.hpp"

namespace MWClass
{
    /// The one class of every object a game pack hands over in a neutral cell (decision 0016, design document 10.3,
    /// stage N3a-3, task 0109). The kind of the object is a field of its base record (MWWorld::NeutralBase), not a
    /// class of its own. In this stage an object with bounds collides as a box of those bounds and nothing is drawn
    /// (the model reader is stage N3a-4); an object of a kind that is a live thing (an actor, an item, a leveled list)
    /// is an inert reference. No scripts, no stats, not an actor, no tooltip.
    class Neutral final : public MWWorld::RegisteredClass<Neutral>
    {
        friend MWWorld::RegisteredClass<Neutral>;
        Neutral();

        MWWorld::Ptr copyToCellImpl(const MWWorld::ConstPtr& ptr, MWWorld::CellStore& cell) const override;

    public:
        std::string_view getName(const MWWorld::ConstPtr& ptr) const override;
        std::string_view getModel(const MWWorld::ConstPtr& ptr) const override;
        bool hasToolTip(const MWWorld::ConstPtr& ptr) const override { return false; }

        /// The preloader (a worker thread) must not read a neutral object's model: no reader for the game's models
        /// exists yet (stage N3a-4) and cells are built on the main thread (design 10.4, R9).
        void getModelsToPreload(const MWWorld::ConstPtr&, std::vector<std::string_view>&) const override {}

        void insertObjectRendering(const MWWorld::Ptr& ptr, const std::string& model,
            MWRender::RenderingInterface& renderingInterface) const override;
        void insertObject(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
            MWPhysics::PhysicsSystem& physics) const override;
        void insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
            MWPhysics::PhysicsSystem& physics) const override;
    };
}

#endif
