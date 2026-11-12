#ifndef OPENMW_MWCLASS_NEUTRAL_H
#define OPENMW_MWCLASS_NEUTRAL_H

#include <memory>

#include "../mwworld/action.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/doorstate.hpp"
#include "../mwworld/neutralstore.hpp"
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
        /// Task 0142: the activate key (MWWorld::Player::activate) ignores a focused object whose class has no tooltip,
        /// so a neutral actor, door or container has one (a caption: its name), and every other kind stays silent.
        bool hasToolTip(const MWWorld::ConstPtr& ptr) const override;
        MWGui::ToolTipInfo getToolTipInfo(const MWWorld::ConstPtr& ptr, int count) const override;

        /// The preloader (a worker thread) must not read a neutral object's model: no reader for the game's models
        /// exists yet (stage N3a-4) and cells are built on the main thread (design 10.4, R9).
        /// Task 0115 (stage N4): a door of the neutral cell. A teleport door takes the player to the destination
        /// cell (built as a neutral cell) at the XTEL arrival; a locked door, a door to an exterior and a door that
        /// leads nowhere refuse with a message; a door with no teleport is inert; every other kind keeps the null
        /// action. The decision is MWWorld::decideNeutralDoor.
        std::unique_ptr<MWWorld::Action> activate(const MWWorld::Ptr& ptr, const MWWorld::Ptr& actor) const override;

        void getModelsToPreload(const MWWorld::ConstPtr&, std::vector<std::string_view>&) const override {}

        // Task 0131 (stage N3a-6): a neutral object is an inert reference. These are the Class virtuals whose default is
        // to throw and that a generic engine path (not guarded by isActor, isItem or isDoor) or the console can reach
        // with any Ptr; here they answer with the neutral value (nothing, zero, idle) instead. What only an actor has
        // (stats, movement, containers, inventories) keeps refusing: nothing generic calls those on a non-actor.
        int getValue(const MWWorld::ConstPtr&) const override { return 0; }
        float getWeight(const MWWorld::ConstPtr&) const override { return 0.f; }
        float getCapacity(const MWWorld::Ptr&) const override { return 0.f; }
        float getEncumbrance(const MWWorld::Ptr&) const override { return 0.f; }
        bool isPersistent(const MWWorld::ConstPtr&) const override { return false; }
        int getEnchantmentPoints(const MWWorld::ConstPtr&) const override { return 0; }
        const std::string& getInventoryIcon(const MWWorld::ConstPtr&) const override;
        int getItemMaxHealth(const MWWorld::ConstPtr&) const override { return 0; }
        const ESM::RefId& getUpSoundId(const MWWorld::ConstPtr&) const override;
        const ESM::RefId& getDownSoundId(const MWWorld::ConstPtr&) const override;
        ESM::RefId getSoundIdFromSndGen(const MWWorld::Ptr&, std::string_view) const override { return {}; }
        int getBaseGold(const MWWorld::ConstPtr&) const override { return 0; }
        void setRemainingUsageTime(const MWWorld::Ptr&, float) const override {}
        MWWorld::DoorState getDoorState(const MWWorld::ConstPtr&) const override { return MWWorld::DoorState::Idle; }
        void setDoorState(const MWWorld::Ptr&, MWWorld::DoorState) const override {}

        void insertObjectRendering(const MWWorld::Ptr& ptr, const std::string& model,
            MWRender::RenderingInterface& renderingInterface) const override;
        void insertObject(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
            MWPhysics::PhysicsSystem& physics) const override;

    private:
        /// An actor object (task 0121): drawn through the pack's ActorAppearanceSource when [Game] neutral actors is on.
        void insertActorRendering(const MWWorld::Ptr& ptr, const MWWorld::NeutralBase& base,
            MWRender::RenderingInterface& renderingInterface) const;

    public:
        void insertObjectPhysics(const MWWorld::Ptr& ptr, const std::string& model, const vsg::dquat& rotation,
            MWPhysics::PhysicsSystem& physics) const override;
    };
}

#endif
