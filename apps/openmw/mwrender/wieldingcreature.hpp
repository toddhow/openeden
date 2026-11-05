#ifndef VSGOPENMW_MWRENDER_WIELDINGCREATURE_H
#define VSGOPENMW_MWRENDER_WIELDINGCREATURE_H

#include <optional>
#include <string>

#include <vsg/maths/vec4.h>

#include <components/mwanimation/wielding.hpp>

#include "../mwworld/ptr.hpp"

#include "equipmentupdate.hpp"
#include "partcontrollers.hpp"

namespace ESM
{
    struct Light;
}

namespace MWRender
{
    class WieldingCreature : public MWAnim::Wielding
    {
        void addPart(const MWWorld::Ptr& ptr, int slot);
        void addAmmo(const MWWorld::Ptr& ptr, const MWAnim::Context& mwctx);
        void addParts();
        std::vector<vsg::Node*> mParts[3];
        MWWorld::Ptr mPtr;

    protected:
        // Protected for the tests (task 0074b): what an equipment change removes.
        void removeParts();
        // Task 0074h: reads the ammunition model and adds it to the weapon part's placeholder (mAttachAmmo, which
        // must be set). False, with nothing changed, when the model cannot be read (a std::exception, logged once per
        // path), as addItemPart skips an item it cannot read.
        bool attachAmmoModel(const std::string& model, const MWAnim::Context& mwctx);
        // The node work of addPart for one slot (Slot_CarriedRight or Slot_CarriedLeft), apart from the inventory so a
        // test reaches it (task 0074d, finding 4): `light` is the carried light's record, or null.
        void linkPart(int slot, vsg::ref_ptr<vsg::Node> node, const ESM::Light* light, const std::string& bonename,
            std::optional<vsg::vec4> glowColor);
        // addPart's work for the item in `slot` (Slot_CarriedRight or Slot_CarriedLeft), at `bonename`: reads the
        // item's model, gives a carried light its light, links the part. A model that cannot be read (a
        // std::exception) is logged and the part skipped, as OpenMW 0.51 does (task 0074e, finding 7).
        void addItemPart(int slot, const MWWorld::ConstPtr& item, const std::string& bonename);
        // One equipment change (task 0074e, finding 5): `nodes` removes and adds the parts, then the creature is
        // compiled (a failed compile removes the parts), also when `nodes` throws; the exception is passed on
        // unchanged. WieldingCreature::updateEquipment.
        template <class Nodes>
        void runCompiledUpdate(Nodes&& nodes)
        {
            runEquipmentUpdate(nodes, [this] { compileOrRemoveParts(); });
        }
        void compileOrRemoveParts();
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
