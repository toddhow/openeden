// Tests for task 0074e, the fresh review of task 0074d (finding 7): a part whose model cannot be read is skipped, as
// OpenMW 0.51 does per part. MWRender::Npc::insertBoundedPart catches a std::exception from the part's model read,
// attaches and links nothing for that part and says so (false), and the parts before and after it in the same part
// update are kept; any other thrown value still reaches the caller. MWRender::WieldingCreature::addItemPart (addPart's
// work for one item) skips an item whose model read throws a std::exception the same way. Written from the task's
// test specification before the implementation and run red against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <string>

namespace
{
    using PartsTestSupport::frameTime;

    // The part models (lower case, as the reader compares them).
    constexpr const char* groinModel = "meshes/test/readerrors_groin.nif";
    constexpr const char* hairModel = "meshes/test/readerrors_hair.nif";
    constexpr const char* cuirassModel = "meshes/test/readerrors_cuirass.nif";
    // The creature's items: bare file names, whatever path the item's model is read with.
    constexpr const char* brokenTorchModel = "readerrors_broken_torch.nif";

    // An Npc built without a record (Npc's test constructor), with the part functions in reach.
    struct ReadErrorsNpc : public MWRender::Npc
    {
        explicit ReadErrorsNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::mPartControllers;
        using MWRender::Npc::mParts;
        using MWRender::Npc::mStaticControllers;
        using MWRender::Npc::mWeaponControllers;
        using MWRender::Npc::runPartUpdate;
    };

    struct PartReadErrorsTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<ReadErrorsNpc> mNpc;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mNpc = std::make_unique<ReadErrorsNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> skeleton = PartsTestSupport::makeSkeleton({ "Head", "Chest", "Groin" });
            mNpc->animation = PartsTestSupport::npcPlay(*skeleton);
        }

        bool insertPart(ESM::PartReferenceType type, const std::string& model)
        {
            return mNpc->insertBoundedPart(type, model, MWAnim::sPartBones.at(type), std::nullopt, nullptr);
        }

        void runFrames(int frames)
        {
            for (int i = 0; i < frames; ++i)
                mNpc->update(frameTime);
        }
    };

    // Finding 7: when a part's model read throws a std::exception, insertBoundedPart does not throw, says false, and
    // attaches and links nothing for the part; the part inserted before it in the same part update is kept and the
    // one inserted after it is attached and linked as usual.
    TEST_F(PartReadErrorsTest, a_part_whose_model_read_throws_is_skipped_and_the_parts_around_it_are_kept)
    {
        ReadErrorsNpc& npc = *mNpc;
        vsg::ref_ptr<PartsTestSupport::Recorder> before = PartsTestSupport::listRecorder();
        vsg::ref_ptr<PartsTestSupport::Recorder> after = PartsTestSupport::listRecorder();
        mReader->add(groinModel, PartsTestSupport::makeModel({ before }));
        mReader->add(cuirassModel, PartsTestSupport::makeModel({ after }));
        mReader->breakWithReadError(hairModel);
        std::optional<bool> brokenInserted;
        auto update = [&] {
            insertPart(ESM::PRT_Groin, groinModel);
            brokenInserted = insertPart(ESM::PRT_Hair, hairModel);
            insertPart(ESM::PRT_Cuirass, cuirassModel);
        };

        EXPECT_NO_THROW(npc.runPartUpdate(update));

        EXPECT_EQ(brokenInserted, std::optional<bool>{ false });
        EXPECT_TRUE(npc.mParts[ESM::PRT_Hair].empty());
        EXPECT_TRUE(npc.mPartControllers.entries(ESM::PRT_Hair).empty());
        EXPECT_TRUE(npc.mPartControllers.autoPlay(ESM::PRT_Hair).groups.empty());
        EXPECT_FALSE(npc.mParts[ESM::PRT_Groin].empty());
        EXPECT_FALSE(npc.mParts[ESM::PRT_Cuirass].empty());
        EXPECT_EQ(PartsTestSupport::countIn(npc.mStaticControllers, before.get()), 1u);
        EXPECT_EQ(PartsTestSupport::countIn(npc.mStaticControllers, after.get()), 1u);
        EXPECT_EQ(npc.mStaticControllers.controllers.size(), 2u);
        EXPECT_TRUE(npc.mWeaponControllers.controllers.empty());
        runFrames(3);
        EXPECT_EQ(before->mTimes.size(), 3u);
        EXPECT_EQ(after->mTimes.size(), 3u);
    }

    // Finding 7, guard: a part whose model can be read is inserted, and insertBoundedPart says so.
    TEST_F(PartReadErrorsTest, a_part_whose_model_can_be_read_is_inserted_and_returns_true)
    {
        ReadErrorsNpc& npc = *mNpc;
        vsg::ref_ptr<PartsTestSupport::Recorder> controller = PartsTestSupport::listRecorder();
        mReader->add(groinModel, PartsTestSupport::makeModel({ controller }));
        std::optional<bool> inserted;

        npc.runPartUpdate([&] { inserted = insertPart(ESM::PRT_Groin, groinModel); });

        EXPECT_EQ(inserted, std::optional<bool>{ true });
        EXPECT_FALSE(npc.mParts[ESM::PRT_Groin].empty());
        EXPECT_EQ(PartsTestSupport::countIn(npc.mStaticControllers, controller.get()), 1u);
    }

    // Finding 7: only a std::exception is caught, as in OpenMW 0.51: any other thrown value reaches the caller.
    TEST_F(PartReadErrorsTest, a_thrown_value_that_is_not_a_std_exception_reaches_the_caller)
    {
        ReadErrorsNpc& npc = *mNpc;
        mReader->breakWithNotAnException(hairModel);
        auto update = [&] { insertPart(ESM::PRT_Hair, hairModel); };

        EXPECT_THROW(npc.runPartUpdate(update), PartsTestSupport::NotAnException);

        EXPECT_TRUE(npc.mParts[ESM::PRT_Hair].empty());
    }

    // A creature that wields (MWAnim::createCreature for a creature with weapons), with the part functions in reach.
    struct ReadErrorsCreature : public MWRender::WieldingCreature
    {
        using MWRender::WieldingCreature::addItemPart;
        using MWRender::WieldingCreature::linkPart;
        using MWRender::WieldingCreature::mPartControllers;
        using MWRender::WieldingCreature::mStaticControllers;
        using MWRender::WieldingCreature::mWeaponControllers;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    struct CreaturePartReadErrorsTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<ReadErrorsCreature> mCreature;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mCreature = std::make_unique<ReadErrorsCreature>(mContext);
            PartsTestSupport::addCreatureSkeleton(
                *mCreature, PartsTestSupport::makeSkeleton({ "Weapon Bone", "Weapon Bone Left", "Shield Bone" }));
        }
    };

    // Finding 7: when a creature's item model read throws a std::exception, addItemPart does not throw and attaches
    // nothing for the slot (no light from a light item whose model is broken) and links nothing; the creature's
    // other part, linked before, is untouched.
    TEST_F(CreaturePartReadErrorsTest, an_item_whose_model_read_throws_is_skipped_and_the_other_part_is_untouched)
    {
        ReadErrorsCreature& creature = *mCreature;
        vsg::ref_ptr<PartsTestSupport::Recorder> weapon = PartsTestSupport::autoPlayRecorder(0);
        creature.linkPart(MWWorld::InventoryStore::Slot_CarriedRight, PartsTestSupport::makeModel({ weapon }), nullptr,
            "Weapon Bone", std::nullopt);
        const std::size_t nodesBefore = PartsTestSupport::countNodes(*creature.node());
        const std::size_t weaponEntries = creature.mPartControllers.entries(0).size();
        const std::size_t carriedLeftEntries = creature.mPartControllers.entries(1).size();
        const std::size_t ammoEntries = creature.mPartControllers.entries(2).size();
        const std::size_t staticLinks = creature.mStaticControllers.controllers.size();
        const std::size_t weaponLinks = creature.mWeaponControllers.controllers.size();
        PartsTestSupport::LightItem torch(ESM::Light::Carry | ESM::Light::Flicker, brokenTorchModel);
        mReader->breakWithReadError(brokenTorchModel);

        EXPECT_NO_THROW(
            creature.addItemPart(MWWorld::InventoryStore::Slot_CarriedLeft, torch.ptr(), "Weapon Bone Left"));

        EXPECT_TRUE(PartsTestSupport::lightsUnder(*creature.node()).empty());
        EXPECT_EQ(PartsTestSupport::countNodes(*creature.node()), nodesBefore);
        EXPECT_EQ(creature.mPartControllers.entries(0).size(), weaponEntries);
        EXPECT_EQ(creature.mPartControllers.entries(1).size(), carriedLeftEntries);
        EXPECT_EQ(creature.mPartControllers.entries(2).size(), ammoEntries);
        EXPECT_TRUE(creature.mPartControllers.autoPlay(1).groups.empty());
        EXPECT_EQ(creature.mStaticControllers.controllers.size(), staticLinks);
        EXPECT_EQ(creature.mWeaponControllers.controllers.size(), weaponLinks);
        const std::size_t weaponRuns = weapon->mTimes.size();
        for (int i = 0; i < 3; ++i)
            creature.update(frameTime);
        EXPECT_EQ(weapon->mTimes.size(), weaponRuns + 3);
    }
}
