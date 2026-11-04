// Tests for task 0074f, the fresh review of task 0074e (finding 1): MWAnim::Wielding::mAttachAmmo is a raw pointer
// to the ammunition placeholder of the decorated weapon model, which lives in the weapon part's subtree. It must be
// null whenever the weapon part it came from is gone (a part removed, replaced by a weapon whose read failed, or all
// parts removed), or the next ammunition change (Npc::addAmmo, WieldingCreature::addAmmo) would attach an arrow to
// a freed node. A test actor has an empty Ptr, so the rest of addAmmo throws: "returned at once" shows itself as
// "does not throw". Written from the task's test specification before the implementation and run red against its
// stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/mwanimation/wielding.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Transform.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace
{
    // The weapon and shield models (lower case, as the reader compares them).
    constexpr const char* bowModel = "meshes/test/attachammo_bow.nif";
    constexpr const char* otherBowModel = "meshes/test/attachammo_bow_other.nif";
    constexpr const char* swordModel = "meshes/test/attachammo_sword.nif";
    constexpr const char* brokenWeaponModel = "meshes/test/attachammo_broken.nif";
    constexpr const char* shieldModel = "meshes/test/attachammo_shield.nif";
    // The creature's items: bare file names, whatever path the item's model is read with.
    constexpr const char* brokenItemModel = "attachammo_broken_item.nif";

    // An Npc built without a record (Npc's test constructor), with the weapon and the ammunition in reach.
    struct AmmoNpc : public MWRender::Npc
    {
        explicit AmmoNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWAnim::Wielding::mAttachAmmo;
        using MWRender::Npc::addAmmo;
        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::mParts;
        using MWRender::Npc::removeAllParts;
        using MWRender::Npc::removeIndividualPart;
        using MWRender::Npc::runPartUpdate;
    };

    struct AttachAmmoTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<AmmoNpc> mNpc;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mReader->add(bowModel, PartsTestSupport::makeBowModel());
            mReader->add(otherBowModel, PartsTestSupport::makeBowModel());
            mReader->add(swordModel, PartsTestSupport::makePlainModel());
            mReader->add(shieldModel, PartsTestSupport::makePlainModel());
            mReader->breakWithReadError(brokenWeaponModel);
            mNpc = std::make_unique<AmmoNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> skeleton
                = PartsTestSupport::makeSkeleton({ "Weapon Bone", "Shield Bone", "Chest" });
            mNpc->animation = PartsTestSupport::npcPlay(*skeleton);
        }

        // One part update around the insertion; true when the part was inserted.
        bool insertWeapon(const std::string& model)
        {
            std::optional<bool> inserted;
            mNpc->runPartUpdate([&] {
                inserted = mNpc->insertBoundedPart(
                    ESM::PRT_Weapon, model, MWAnim::sPartBones.at(ESM::PRT_Weapon), std::nullopt, nullptr);
            });
            return inserted.value_or(false);
        }

        void removeWeapon()
        {
            mNpc->runPartUpdate([&] { mNpc->removeIndividualPart(ESM::PRT_Weapon); });
        }

        // The ammunition's placeholder in a node count of its children.
        static std::size_t childrenOf(const vsg::ref_ptr<vsg::Transform>& placeholder)
        {
            return placeholder->children.size();
        }
    };

    // Finding 1: removing the weapon part leaves no pointer into its subtree, and the ammunition change returns at
    // once.
    TEST_F(AttachAmmoTest, removing_the_bow_part_clears_the_ammunition_placeholder)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        ASSERT_NE(npc.mAttachAmmo, nullptr);

        removeWeapon();

        EXPECT_EQ(npc.mAttachAmmo, nullptr);
        EXPECT_NO_THROW(npc.addAmmo());
    }

    // Finding 1, the review's path: the bow's slot is taken by a weapon whose read throws; the bow was not removed
    // first. The failed insertion leaves no stale pointer.
    TEST_F(AttachAmmoTest, a_failed_weapon_read_over_a_bow_leaves_no_ammunition_placeholder)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        ASSERT_NE(npc.mAttachAmmo, nullptr);

        EXPECT_FALSE(insertWeapon(brokenWeaponModel));

        EXPECT_EQ(npc.mAttachAmmo, nullptr);
        EXPECT_NO_THROW(npc.addAmmo());
    }

    // Finding 1, the same with the bow removed first (the way addOrReplaceIndividualPart goes).
    TEST_F(AttachAmmoTest, a_failed_weapon_read_after_the_bow_was_removed_leaves_no_ammunition_placeholder)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        ASSERT_NE(npc.mAttachAmmo, nullptr);
        removeWeapon();

        EXPECT_FALSE(insertWeapon(brokenWeaponModel));

        EXPECT_EQ(npc.mAttachAmmo, nullptr);
        EXPECT_NO_THROW(npc.addAmmo());
    }

    // Finding 1: removing every part leaves no pointer into the weapon's subtree.
    TEST_F(AttachAmmoTest, removing_all_parts_clears_the_ammunition_placeholder)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        ASSERT_NE(npc.mAttachAmmo, nullptr);

        npc.removeAllParts();

        EXPECT_EQ(npc.mAttachAmmo, nullptr);
        EXPECT_NO_THROW(npc.addAmmo());
    }

    // Finding 1, guard: a shield inserted and removed does not touch the weapon's ammunition placeholder.
    TEST_F(AttachAmmoTest, a_shield_inserted_and_removed_leaves_the_bows_placeholder_alone)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        vsg::Transform* const placeholder = npc.mAttachAmmo;
        ASSERT_NE(placeholder, nullptr);

        npc.runPartUpdate([&] {
            npc.insertBoundedPart(
                ESM::PRT_Shield, shieldModel, MWAnim::sPartBones.at(ESM::PRT_Shield), std::nullopt, nullptr);
        });
        EXPECT_EQ(npc.mAttachAmmo, placeholder);
        npc.runPartUpdate([&] { npc.removeIndividualPart(ESM::PRT_Shield); });

        EXPECT_EQ(npc.mAttachAmmo, placeholder);
    }

    // Finding 1, guard: a bow replaced by a sword that reads fine gives no placeholder (the sword has none).
    TEST_F(AttachAmmoTest, a_bow_replaced_by_a_sword_gives_no_ammunition_placeholder)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        ASSERT_NE(npc.mAttachAmmo, nullptr);
        removeWeapon();

        EXPECT_TRUE(insertWeapon(swordModel));

        EXPECT_EQ(npc.mAttachAmmo, nullptr);
    }

    // Finding 1, guard: a bow replaced by another bow gives the new bow's placeholder, not the old one.
    TEST_F(AttachAmmoTest, a_bow_replaced_by_another_bow_gives_the_new_bows_placeholder)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        // Kept alive, so that the new placeholder cannot be allocated at the old one's address.
        const vsg::ref_ptr<vsg::Transform> oldPlaceholder(npc.mAttachAmmo);
        ASSERT_NE(oldPlaceholder, nullptr);
        removeWeapon();

        ASSERT_TRUE(insertWeapon(otherBowModel));

        EXPECT_NE(npc.mAttachAmmo, nullptr);
        EXPECT_NE(npc.mAttachAmmo, oldPlaceholder.get());
    }

    // Finding 1, the end to end shape: the bow's slot is taken by a weapon whose read throws, and the ammunition
    // change then attaches nothing to the old bow's placeholder.
    TEST_F(AttachAmmoTest, the_ammunition_change_after_a_failed_weapon_read_attaches_nothing_to_the_old_bow)
    {
        AmmoNpc& npc = *mNpc;
        ASSERT_TRUE(insertWeapon(bowModel));
        const vsg::ref_ptr<vsg::Transform> oldPlaceholder(npc.mAttachAmmo);
        ASSERT_NE(oldPlaceholder, nullptr);
        const std::size_t childrenBefore = childrenOf(oldPlaceholder);
        removeWeapon();
        EXPECT_FALSE(insertWeapon(brokenWeaponModel));

        ASSERT_NO_THROW(npc.addAmmo());

        EXPECT_EQ(childrenOf(oldPlaceholder), childrenBefore);
    }

    // A creature that wields, with the parts and the ammunition in reach.
    struct AmmoCreature : public MWRender::WieldingCreature
    {
        using MWAnim::Wielding::mAttachAmmo;
        using MWRender::WieldingCreature::addItemPart;
        using MWRender::WieldingCreature::linkPart;
        using MWRender::WieldingCreature::removeParts;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    // WieldingCreature::addAmmo is private, and a guard of it must call it: the standard allows naming a private
    // member in an explicit instantiation, and the friend function defined in the instantiated class hands it out.
    using AddAmmoFunction = void (MWRender::WieldingCreature::*)(const MWWorld::Ptr&, const MWAnim::Context&);
    AddAmmoFunction addAmmoOfCreature();
    template <AddAmmoFunction Member>
    struct ExposeAddAmmo
    {
        friend AddAmmoFunction addAmmoOfCreature() { return Member; }
    };
    template struct ExposeAddAmmo<&MWRender::WieldingCreature::addAmmo>;

    struct CreatureAttachAmmoTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<AmmoCreature> mCreature;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mReader->breakWithReadError(brokenItemModel);
            mCreature = std::make_unique<AmmoCreature>(mContext);
            PartsTestSupport::addCreatureSkeleton(
                *mCreature, PartsTestSupport::makeSkeleton({ "Weapon Bone", "Weapon Bone Left", "Shield Bone" }));
        }

        void linkBow()
        {
            mCreature->linkPart(MWWorld::InventoryStore::Slot_CarriedRight, PartsTestSupport::makeBowModel(), nullptr,
                "Weapon Bone", std::nullopt);
        }
    };

    // Finding 1: a creature's equipment change removes the bow, and with it the ammunition placeholder.
    TEST_F(CreatureAttachAmmoTest, removing_the_creatures_parts_clears_the_ammunition_placeholder)
    {
        AmmoCreature& creature = *mCreature;
        linkBow();
        ASSERT_NE(creature.mAttachAmmo, nullptr);

        creature.removeParts();

        EXPECT_EQ(creature.mAttachAmmo, nullptr);
    }

    // Finding 1: the creature's weapon whose read throws, after the bow went, leaves no placeholder either.
    TEST_F(CreatureAttachAmmoTest, a_failed_item_read_after_a_bow_leaves_no_ammunition_placeholder)
    {
        AmmoCreature& creature = *mCreature;
        linkBow();
        ASSERT_NE(creature.mAttachAmmo, nullptr);
        creature.removeParts();
        // A light item stands for the weapon: the read of its model is what throws.
        PartsTestSupport::LightItem item(ESM::Light::Carry, brokenItemModel);

        EXPECT_NO_THROW(creature.addItemPart(MWWorld::InventoryStore::Slot_CarriedRight, item.ptr(), "Weapon Bone"));

        EXPECT_EQ(creature.mAttachAmmo, nullptr);
    }

    // Finding 1, guard: with no placeholder the creature's ammunition change returns before it asks the (empty) Ptr
    // for its inventory, which would throw.
    TEST_F(CreatureAttachAmmoTest, the_ammunition_change_without_a_placeholder_returns_at_once)
    {
        AmmoCreature& creature = *mCreature;
        ASSERT_EQ(creature.mAttachAmmo, nullptr);
        const AddAmmoFunction addAmmo = addAmmoOfCreature();

        EXPECT_NO_THROW((creature.*addAmmo)(MWWorld::Ptr(), mContext));
    }
}
