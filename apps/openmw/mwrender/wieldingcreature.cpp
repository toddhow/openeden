#include "wieldingcreature.hpp"

#include <components/animation/context.hpp>
#include <components/debug/debuglog.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/mwanimation/attach.hpp>
#include <components/mwanimation/clone.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/decorate.hpp>
#include <components/mwanimation/play.hpp>
#include <components/vsgutil/compilecontext.hpp>
#include <components/vsgutil/removechild.hpp>

#include "../mwmechanics/weapontype.hpp"
#include "../mwworld/class.hpp"
#include "../mwworld/inventorystore.hpp"

#include "autoplaytrace.hpp"
#include "env.hpp"
#include "equipmentupdate.hpp"
#include "transparency.hpp"

namespace MWRender
{

    WieldingCreature::WieldingCreature(const MWAnim::Context& mwctx)
        : Wielding(mwctx)
    {
    }

    void WieldingCreature::setup(const MWWorld::Ptr& ptr)
    {
        mPtr = ptr;
        if (const auto* creature = autoPlayTraceOn() ? ptr.get<ESM::Creature>() : nullptr)
        {
            const std::string id = creature->mBase->mId.toString();
            if (autoPlayTraced(id, creature->mBase->mName))
                mAutoPlayTraced = id;
        }
        addParts();
    }

    void WieldingCreature::addParts()
    {
        addPart(mPtr, MWWorld::InventoryStore::Slot_CarriedRight);
        addPart(mPtr, MWWorld::InventoryStore::Slot_CarriedLeft);
        addAmmo(mPtr, mContext);
    }

    void WieldingCreature::removeParts()
    {
        // Task 0074b, finding 3: not autoPlay, which holds the creature's own model's auto-play controllers
        // (MWAnim::createCreature, addNode) and its inventory lights (addItemLight); emptying it stopped their
        // particles, flicker and texture animation at the first equipment change, for good. The parts' auto-play
        // controllers run in their own clocks, emptied here with their nodes.
        clearPartControllers();

        for (int i = 0; i < 3; ++i)
        {
            mPartControllers.forget(static_cast<std::size_t>(i));
            auto& path = mParts[i];
            if (!path.empty())
            {
                mContext.compileContext->detach(vsg::ref_ptr{ path.back() });
                // vsgUtil::prune<vsg::Group>(path);
                if (path.size() >= 2)
                    if (auto group = (*(path.rbegin() + 1))->cast<vsg::Group>())
                        vsgUtil::removeChild(group, path.back());
                path.clear();
            }
        }
    }

    void WieldingCreature::update(float dt)
    {
        Wielding::update(dt);
        mPartControllers.update(dt);
        if (!mAutoPlayTraced.empty())
            traceAutoPlay(mAutoPlayTraced, mAutoPlayFrame++, dt, autoPlay, mPartControllers.clocks(),
                { mStaticControllers.controllers.size(), mWeaponControllers.controllers.size(), 0 });
    }

    void WieldingCreature::addPart(const MWWorld::Ptr& ptr, int slot)
    {
        const MWWorld::InventoryStore& inv = ptr.getClass().getInventoryStore(ptr);
        MWWorld::ConstContainerStoreIterator it = inv.getSlot(slot);

        if (it == inv.end())
            return;

        MWWorld::ConstPtr item = *it;

        std::string bonename;
        if (slot == MWWorld::InventoryStore::Slot_CarriedRight)
        {
            if (item.getType() == ESM::Weapon::sRecordId)
            {
                int type = item.get<ESM::Weapon>()->mBase->mData.mType;
                bonename = MWMechanics::getWeaponType(type)->mAttachBone;
                if (bonename != "Weapon Bone")
                {
                    auto b = animation->bones.search(bonename);
                    if (!b)
                        bonename = "Weapon Bone";
                }
            }
            else
                bonename = "Weapon Bone";
        }
        else
        {
            bonename = "Shield Bone";
            // if (item.getType() == ESM::Armor::sRecordId)
            // itemModel = getShieldMesh(item, false);
        }

        addItemPart(slot, item, bonename);
    }

    void WieldingCreature::addItemPart(int slot, const MWWorld::ConstPtr& item, const std::string& bonename)
    {
        const std::string model = std::string(item.getClass().getModel(item));
        // Task 0074d (review of 0074b, item B): a carried light gives light, as an NPC's torch does (insertBoundedPart)
        // and as OpenMW 0.51's CreatureWeaponAnimation::updatePart does (addExtraLight, 442eec6e2f).
        const ESM::Light* light = item.getType() == ESM::Light::sRecordId ? item.get<ESM::Light>()->mBase : nullptr;
        vsg::ref_ptr<vsg::Node> node;
        try
        {
            node = mContext.readNode(model);
        }
        catch (const std::exception& e)
        {
            // Task 0074e (finding 7 of the review of 0074d), as OpenMW 0.51 (CreatureWeaponAnimation::updatePart,
            // 442eec6e2f): the model cannot be read, so the part is skipped and the update goes on.
            Log(Debug::Error) << "Can not add creature part: " << e.what();
            return;
        }
        linkPart(slot, std::move(node), light, bonename, getGlowColor(item));
    }

    void WieldingCreature::linkPart(int slot, vsg::ref_ptr<vsg::Node> node, const ESM::Light* light,
        const std::string& bonename, std::optional<vsg::vec4> glowColor)
    {
        auto result = MWAnim::decorate(node, light);
        addEnv(node, glowColor, getTransparency(*this));
        if (slot == MWWorld::InventoryStore::Slot_CarriedRight)
        {
            mAttachAmmo = result.placeholders.attachAmmo;
            addSwitch(node, Wield::Weapon);
        }
        else
            addSwitch(node, Wield::CarriedLeft);

        int i = (slot == MWWorld::InventoryStore::Slot_CarriedRight) ? static_cast<int>(Wield::Weapon)
                                                                     : static_cast<int>(Wield::CarriedLeft);

        mParts[i] = MWAnim::attachNode(node, animation->bones, bonename);
        if (mParts[i].empty())
            return;

        Anim::Context ctx{ { transform() }, mParts[i], &animation->bones, &mContext.mask };
        Anim::Update* dst = &mStaticControllers;
        if (slot == MWWorld::InventoryStore::Slot_CarriedRight)
            dst = &mWeaponControllers;

        // Task 0074b: the part's auto-play controllers in its own clock (the creature's autoPlay is no longer emptied).
        const auto part = static_cast<std::size_t>(i);
        mPartControllers.beginPart(part);
        result.link(ctx, [this, &dst, part](const Anim::Controller* ctrl, vsg::Object* o) {
            if (ctrl->hints.autoPlay)
                mPartControllers.addAutoPlay(part, ctrl, o);
            else
                dst->add(ctrl, o);
        });
    }

    void WieldingCreature::addAmmo(const MWWorld::Ptr& ptr, const MWAnim::Context& mwctx)
    {
        if (!mAttachAmmo)
            return;

        const MWWorld::InventoryStore& inv = ptr.getClass().getInventoryStore(ptr);
        auto ammo = inv.getSlot(MWWorld::InventoryStore::Slot_Ammunition);
        if (ammo != inv.end())
        {
            std::string model = std::string(ammo->getClass().getModel(*ammo));
            auto node = mwctx.readNode(model);
            auto result = MWAnim::cloneIfRequired(node);
            addSwitch(node, Wield::Ammo);
            mAttachAmmo->subgraphRequiresLocalFrustum = true;
            mAttachAmmo->addChild(node);
            mParts[static_cast<int>(Wield::Ammo)] = { mAttachAmmo, node };
        }
        // updateQuiver();
    }

    void WieldingCreature::updateEquipment()
    {
        if (!mAutoPlayTraced.empty())
            traceAutoPlayEvent(mAutoPlayTraced, "updateEquipment");
        // Task 0074d (review of 0074b, finding 1): compiled also when addParts throws (runEquipmentUpdate).
        runEquipmentUpdate(
            [this] {
                removeParts();
                addParts();
            },
            [this] {
                if (!mContext.compileContext->compile(node()))
                    removeParts();
            });
    }

}
