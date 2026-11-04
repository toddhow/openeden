#ifndef VSGOPENMW_MWRENDER_WIELDINGCREATURE_H
#define VSGOPENMW_MWRENDER_WIELDINGCREATURE_H

#include <components/mwanimation/wielding.hpp>

#include "../mwworld/ptr.hpp"

#include "partcontrollers.hpp"

namespace MWRender
{
    class WieldingCreature : public MWAnim::Wielding
    {
        void addPart(const MWWorld::Ptr& ptr, int slot, const MWAnim::Context& mwctx);
        void addAmmo(const MWWorld::Ptr& ptr, const MWAnim::Context& mwctx);
        void addParts();
        std::vector<vsg::Node*> mParts[3];
        MWWorld::Ptr mPtr;

    protected:
        // Protected for the tests (task 0074b): what an equipment change removes.
        void removeParts();
        // The weapon's, the carried left's and the ammunition's controllers (Wield's order).
        PartControllers<3> mPartControllers;
        // Task 0074b: OPENEDEN_AUTOPLAY_TRACE (autoplaytrace.hpp), decided once in setup.
        std::string mAutoPlayTraced;
        unsigned mAutoPlayFrame = 0;

    public:
        WieldingCreature(const MWAnim::Context& mwctx);
        void setup(const MWWorld::Ptr& ptr);
        void update(float dt) override;
        void updateEquipment() override;
    };
}

#endif
