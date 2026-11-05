// Tests for task 0074h (per-part catches for the ammunition, holstered-item and quiver reads). 0074e caught the read
// of a worn part (Npc::insertBoundedPart, WieldingCreature::addItemPart) and left these throw sites to the callers,
// where a mechanics-driven change (torch auto-equip, AI combat, spell effects) ends the game session. Each read is now
// guarded like a part's: a std::exception from it is logged (once per path), the part is left out, nothing is attached
// for it and the actor's state stays consistent (the ammunition placeholder keeps its children, the holstered paths
// and switches stay empty). Written from the task's specification before the guards and run red against the old
// behaviour (the reads let the exception through).

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>
#include <components/mwanimation/wielding.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>
#include <vsg/nodes/Switch.h>
#include <vsg/nodes/Transform.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace
{
    constexpr const char* bowModel = "meshes/test/guards_bow.nif";
    constexpr const char* goodModel = "meshes/test/guards_good.nif";
    constexpr const char* brokenModel = "meshes/test/guards_broken.nif";
    // Holstered models: a scabbard with an empty weapon node, a scabbard that holds its weapon, a sheath that is empty.
    constexpr const char* emptyScabbard = "meshes/test/guards_empty_sh.nif";
    constexpr const char* fullScabbard = "meshes/test/guards_full_sh.nif";
    constexpr const char* brokenScabbard = "meshes/test/guards_broken_sh.nif";
    constexpr const char* emptySheath = "meshes/test/guards_sheath_sh.nif";
    constexpr const char* noSheath = "meshes/test/guards_nosheath_sh.nif";
    constexpr const char* creatureBrokenAmmo = "guards_creature_broken_ammo.nif";
    constexpr const char* creatureGoodAmmo = "guards_creature_good_ammo.nif";

    vsg::ref_ptr<vsg::Node> modelWithGroup(const std::string& groupName, bool filled)
    {
        auto root = vsg::Group::create();
        vsg::ref_ptr<Anim::Transform> group = PartsTestSupport::makeBone(groupName);
        if (filled)
            group->addChild(PartsTestSupport::makePlainModel());
        root->addChild(group);
        return root;
    }

    struct GuardNpc : public MWRender::Npc
    {
        explicit GuardNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWAnim::Wielding::mAttachAmmo;
        using MWRender::Npc::attachAmmoModel;
        using MWRender::Npc::attachHolsteredShield;
        using MWRender::Npc::attachHolsteredWeapon;
        using MWRender::Npc::attachQuiverArrow;
        using MWRender::Npc::holsteredModelHasSheath;
        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::mHolsteredShield;
        using MWRender::Npc::mHolsteredShieldSwitch;
        using MWRender::Npc::mQuiver;
        using MWRender::Npc::mScabbard;
        using MWRender::Npc::runPartUpdate;
    };

    struct PartReadGuardsTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<GuardNpc> mNpc;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mReader->add(bowModel, PartsTestSupport::makeBowModel());
            mReader->add(goodModel, PartsTestSupport::makePlainModel());
            mReader->add(emptyScabbard, modelWithGroup("Bip01 Weapon", false));
            mReader->add(fullScabbard, modelWithGroup("Bip01 Weapon", true));
            mReader->add(emptySheath, modelWithGroup("Bip01 Sheath", false));
            mReader->add(noSheath, modelWithGroup("Something Else", false));
            mReader->breakWithReadError(brokenModel);
            mReader->breakWithReadError(brokenScabbard);
            mNpc = std::make_unique<GuardNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> skeleton
                = PartsTestSupport::makeSkeleton({ "Weapon Bone", "Shield Bone", "Chest", "Bip01 AttachShield" });
            mNpc->animation = PartsTestSupport::npcPlay(*skeleton);
        }

        // A bow in the weapon part, whose placeholder takes the ammunition.
        vsg::Transform& bowPlaceholder()
        {
            mNpc->runPartUpdate([&] {
                mNpc->insertBoundedPart(
                    ESM::PRT_Weapon, bowModel, MWAnim::sPartBones.at(ESM::PRT_Weapon), std::nullopt, nullptr);
            });
            return *mNpc->mAttachAmmo;
        }
    };

    // The ammunition.
    TEST_F(PartReadGuardsTest, ammunition_whose_read_throws_is_skipped_and_the_placeholder_keeps_its_children)
    {
        vsg::Transform& placeholder = bowPlaceholder();
        const std::size_t children = placeholder.children.size();
        bool attached = true;

        EXPECT_NO_THROW(attached = mNpc->attachAmmoModel(brokenModel));

        EXPECT_FALSE(attached);
        EXPECT_EQ(mNpc->mAttachAmmo, &placeholder);
        EXPECT_EQ(placeholder.children.size(), children);
    }

    TEST_F(PartReadGuardsTest, ammunition_that_reads_is_attached_to_the_placeholder)
    {
        vsg::Transform& placeholder = bowPlaceholder();
        const std::size_t children = placeholder.children.size();

        EXPECT_TRUE(mNpc->attachAmmoModel(goodModel));

        EXPECT_EQ(placeholder.children.size(), children + 1);
    }

    // The quiver.
    TEST_F(PartReadGuardsTest, a_quiver_arrow_whose_read_throws_is_skipped_and_the_quiver_stays_empty)
    {
        bool attached = true;

        EXPECT_NO_THROW(attached = mNpc->attachQuiverArrow(brokenModel, "Chest", std::nullopt));

        EXPECT_FALSE(attached);
        EXPECT_TRUE(mNpc->mQuiver.empty());
    }

    TEST_F(PartReadGuardsTest, a_quiver_arrow_that_reads_is_attached_and_one_that_throws_keeps_the_earlier_ones)
    {
        ASSERT_TRUE(mNpc->attachQuiverArrow(goodModel, "Chest", std::nullopt));
        ASSERT_EQ(mNpc->mQuiver.size(), 1u);

        EXPECT_NO_THROW(mNpc->attachQuiverArrow(brokenModel, "Chest", std::nullopt));

        EXPECT_EQ(mNpc->mQuiver.size(), 1u);
    }

    // The holstered weapon.
    TEST_F(PartReadGuardsTest, a_holstered_weapon_without_a_scabbard_whose_read_throws_attaches_nothing)
    {
        auto weaponSwitch = vsg::Switch::create();
        bool attached = true;

        EXPECT_NO_THROW(attached = mNpc->attachHolsteredWeapon(
                            brokenModel, "", false, "Weapon Bone", true, nullptr, std::nullopt, *weaponSwitch));

        EXPECT_FALSE(attached);
        EXPECT_TRUE(mNpc->mScabbard.empty());
        EXPECT_TRUE(weaponSwitch->children.empty());
    }

    TEST_F(PartReadGuardsTest, a_holstered_weapon_without_a_scabbard_that_reads_is_attached)
    {
        auto weaponSwitch = vsg::Switch::create();

        EXPECT_TRUE(mNpc->attachHolsteredWeapon(
            goodModel, "", false, "Weapon Bone", true, nullptr, std::nullopt, *weaponSwitch));

        EXPECT_FALSE(mNpc->mScabbard.empty());
        EXPECT_EQ(weaponSwitch->children.size(), 1u);
    }

    TEST_F(PartReadGuardsTest, a_scabbard_whose_read_throws_attaches_nothing)
    {
        auto weaponSwitch = vsg::Switch::create();
        bool attached = true;

        EXPECT_NO_THROW(attached = mNpc->attachHolsteredWeapon(goodModel, brokenScabbard, true, "Weapon Bone", true,
                            nullptr, std::nullopt, *weaponSwitch));

        EXPECT_FALSE(attached);
        EXPECT_TRUE(mNpc->mScabbard.empty());
        EXPECT_TRUE(weaponSwitch->children.empty());
    }

    // An empty weapon node asks for the common weapon mesh, and that read throws: nothing is attached.
    TEST_F(PartReadGuardsTest, a_scabbard_whose_weapon_mesh_read_throws_attaches_nothing)
    {
        auto weaponSwitch = vsg::Switch::create();
        bool attached = true;

        EXPECT_NO_THROW(attached = mNpc->attachHolsteredWeapon(brokenModel, emptyScabbard, true, "Weapon Bone", true,
                            nullptr, std::nullopt, *weaponSwitch));

        EXPECT_FALSE(attached);
        EXPECT_TRUE(mNpc->mScabbard.empty());
        EXPECT_TRUE(weaponSwitch->children.empty());
    }

    TEST_F(PartReadGuardsTest, a_scabbard_that_reads_is_attached_with_its_weapon_behind_the_switch)
    {
        auto weaponSwitch = vsg::Switch::create();

        EXPECT_TRUE(mNpc->attachHolsteredWeapon(
            goodModel, emptyScabbard, true, "Weapon Bone", true, nullptr, std::nullopt, *weaponSwitch));
        EXPECT_FALSE(mNpc->mScabbard.empty());
        EXPECT_EQ(weaponSwitch->children.size(), 1u);
    }

    // A scabbard that holds its weapon needs the common mesh not at all: a broken one does not matter.
    TEST_F(PartReadGuardsTest, a_scabbard_that_holds_its_weapon_does_not_read_the_common_mesh)
    {
        auto weaponSwitch = vsg::Switch::create();

        EXPECT_TRUE(mNpc->attachHolsteredWeapon(
            brokenModel, fullScabbard, true, "Weapon Bone", true, nullptr, std::nullopt, *weaponSwitch));
        EXPECT_FALSE(mNpc->mScabbard.empty());
    }

    // The holstered shield.
    TEST_F(PartReadGuardsTest, a_holstered_shield_whose_read_throws_leaves_no_switch_and_no_path)
    {
        bool attached = true;

        EXPECT_NO_THROW(
            attached = mNpc->attachHolsteredShield(brokenModel, "", false, "Bip01 AttachShield", std::nullopt));

        EXPECT_FALSE(attached);
        EXPECT_EQ(mNpc->mHolsteredShieldSwitch, nullptr);
        EXPECT_TRUE(mNpc->mHolsteredShield.empty());
    }

    TEST_F(PartReadGuardsTest, a_sheath_whose_shield_mesh_read_throws_leaves_no_switch_and_no_path)
    {
        bool attached = true;

        EXPECT_NO_THROW(
            attached = mNpc->attachHolsteredShield(brokenModel, emptySheath, true, "Bip01 AttachShield", std::nullopt));

        EXPECT_FALSE(attached);
        EXPECT_EQ(mNpc->mHolsteredShieldSwitch, nullptr);
        EXPECT_TRUE(mNpc->mHolsteredShield.empty());
    }

    TEST_F(PartReadGuardsTest, a_holstered_shield_that_reads_is_attached)
    {
        EXPECT_TRUE(mNpc->attachHolsteredShield(goodModel, emptySheath, true, "Bip01 AttachShield", std::nullopt));

        EXPECT_NE(mNpc->mHolsteredShieldSwitch, nullptr);
        EXPECT_FALSE(mNpc->mHolsteredShield.empty());
    }

    // hasSheathedShield and canAnimateShield, reached by the mechanics, read the holstered model to look for the
    // sheath.
    TEST_F(PartReadGuardsTest, the_sheath_check_counts_a_model_whose_read_throws_as_having_no_sheath)
    {
        bool has = true;

        EXPECT_NO_THROW(has = mNpc->holsteredModelHasSheath(brokenScabbard));

        EXPECT_FALSE(has);
    }

    TEST_F(PartReadGuardsTest, the_sheath_check_finds_the_sheath_node_of_a_model_that_reads)
    {
        EXPECT_TRUE(mNpc->holsteredModelHasSheath(emptySheath));
        EXPECT_FALSE(mNpc->holsteredModelHasSheath(noSheath));
    }

    // The creature's ammunition.
    struct GuardCreature : public MWRender::WieldingCreature
    {
        using MWAnim::Wielding::mAttachAmmo;
        using MWRender::WieldingCreature::attachAmmoModel;
        using MWRender::WieldingCreature::linkPart;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    struct CreatureReadGuardsTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        std::unique_ptr<GuardCreature> mCreature;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mReader->add(creatureGoodAmmo, PartsTestSupport::makePlainModel());
            mReader->breakWithReadError(creatureBrokenAmmo);
            mCreature = std::make_unique<GuardCreature>(mContext);
            PartsTestSupport::addCreatureSkeleton(
                *mCreature, PartsTestSupport::makeSkeleton({ "Weapon Bone", "Shield Bone" }));
            mCreature->linkPart(MWWorld::InventoryStore::Slot_CarriedRight, PartsTestSupport::makeBowModel(), nullptr,
                "Weapon Bone", std::nullopt);
        }
    };

    TEST_F(CreatureReadGuardsTest, ammunition_whose_read_throws_is_skipped_and_the_placeholder_keeps_its_children)
    {
        GuardCreature& creature = *mCreature;
        vsg::Transform* const placeholder = creature.mAttachAmmo;
        ASSERT_NE(placeholder, nullptr);
        const std::size_t children = placeholder->children.size();
        bool attached = true;

        EXPECT_NO_THROW(attached = creature.attachAmmoModel(creatureBrokenAmmo, mContext));

        EXPECT_FALSE(attached);
        EXPECT_EQ(creature.mAttachAmmo, placeholder);
        EXPECT_EQ(placeholder->children.size(), children);
    }

    TEST_F(CreatureReadGuardsTest, ammunition_that_reads_is_attached_to_the_placeholder)
    {
        GuardCreature& creature = *mCreature;
        const std::size_t children = creature.mAttachAmmo->children.size();

        EXPECT_TRUE(creature.attachAmmoModel(creatureGoodAmmo, mContext));

        EXPECT_EQ(creature.mAttachAmmo->children.size(), children + 1);
    }
}
