// Tests for task 0074f, the fresh review of task 0074e (finding 5): the part code after a failed read. A part whose
// model cannot be read is skipped (task 0074e, finding 7), but its slot stays taken, the body part under it stays
// hidden, and nothing that follows a successful insertion runs: Npc::attachPart looks up the item for its sound only
// when insertBoundedPart returned true. These tests pin that behaviour and what is consistent after a thrown value
// that is not skipped. Most of them pass against the stub as guards; the red-or-guard status of each is said in its
// comment. Written from the task's test specification before the implementation and run against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/partslots.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/body/mergedruntime.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>

#include <vsg/core/Exception.h>
#include <vsg/core/ref_ptr.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using MWRender::PartSlots;
    using Inv = MWWorld::InventoryStore;

    // The part models (lower case, as the reader compares them).
    constexpr const char* goodModel = "meshes/test/failures_good.nif";
    constexpr const char* brokenModel = "meshes/test/failures_broken.nif";
    constexpr const char* logicErrorModel = "meshes/test/failures_logic.nif";
    constexpr const char* vsgExceptionModel = "meshes/test/failures_vsg.nif";
    constexpr const char* notAnExceptionModel = "meshes/test/failures_value.nif";
    // The creature's items: bare file names, whatever path the item's model is read with.
    constexpr const char* logicErrorItemModel = "failures_logic_item.nif";
    constexpr const char* vsgExceptionItemModel = "failures_vsg_item.nif";

    // The part that takes its slot in these tests and the inventory slot that holds it.
    constexpr ESM::PartReferenceType slotPart = ESM::PRT_Cuirass;
    constexpr int slotGroup = Inv::Slot_Cuirass;
    constexpr int slotPriority = 6;

    // An Npc built without a record (Npc's test constructor), with the part functions and the slots in reach.
    struct FailureNpc : public MWRender::Npc
    {
        explicit FailureNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::addOrReplaceIndividualPart;
        using MWRender::Npc::attachPart;
        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::mPartControllers;
        using MWRender::Npc::mParts;
        using MWRender::Npc::mSlots;
        using MWRender::Npc::mStaticControllers;
        using MWRender::Npc::mWeaponControllers;
        using MWRender::Npc::runPartUpdate;
    };

    // The node work of the slots does not matter here: the nodes are not asked to do anything in these tests.
    struct NoNodes final : PartSlots::Nodes
    {
        void detachPart(ESM::PartReferenceType /*part*/) override {}
    };

    struct PartFailuresTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<FailureNpc> mNpc;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mReader->add(goodModel, PartsTestSupport::makePlainModel());
            mReader->breakWithReadError(brokenModel);
            mReader->breakWithNotAnException(notAnExceptionModel);
            // The reads that throw other values: the reader's own mode list has none, its callback does.
            mReader->onRead = [](const std::string& name) {
                if (name.ends_with(logicErrorModel))
                    throw std::logic_error("logic error reading " + name);
                if (name.ends_with(vsgExceptionModel))
                    throw vsg::Exception{ "vsg error reading " + name, 3 };
            };
            mNpc = makeNpc();
        }

        std::unique_ptr<FailureNpc> makeNpc()
        {
            auto npc = std::make_unique<FailureNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> skeleton = PartsTestSupport::makeSkeleton({ "Chest", "Groin", "Head" });
            npc->animation = PartsTestSupport::npcPlay(*skeleton);
            return npc;
        }

        // The part `type` has no node and no controller anywhere.
        static void expectNothingAttached(FailureNpc& npc, ESM::PartReferenceType type)
        {
            EXPECT_TRUE(npc.mParts[type].empty());
            EXPECT_TRUE(npc.mPartControllers.entries(type).empty());
            EXPECT_TRUE(npc.mPartControllers.autoPlay(type).groups.empty());
            EXPECT_TRUE(npc.mStaticControllers.controllers.empty());
            EXPECT_TRUE(npc.mWeaponControllers.controllers.empty());
        }

        // Each of these runs in a part update of its own, as the game always runs one.
        static bool insert(FailureNpc& npc, const std::string& model)
        {
            bool inserted = true;
            npc.runPartUpdate(
                [&] { inserted = npc.insertBoundedPart(slotPart, model, "Chest", std::nullopt, nullptr); });
            return inserted;
        }

        static void attach(FailureNpc& npc, const std::string& model)
        {
            npc.runPartUpdate([&] { npc.attachPart(slotPart, slotGroup, model, std::nullopt, nullptr); });
        }

        static bool addOrReplace(FailureNpc& npc, const std::string& model, int group, int priority)
        {
            bool taken = false;
            npc.runPartUpdate([&] { taken = npc.addOrReplaceIndividualPart(slotPart, group, priority, model); });
            return taken;
        }
    };

    // Finding 5, guard (passes against the stub): attachPart for a part whose model read throws a ReadError returns
    // normally, attaches nothing and makes no sound, because the item lookup that follows a successful insertion (an
    // empty Ptr throws there) is skipped.
    TEST_F(PartFailuresTest, attach_part_with_a_failed_read_attaches_nothing_and_looks_up_no_sound)
    {
        FailureNpc& npc = *mNpc;

        EXPECT_NO_THROW(attach(npc, brokenModel));

        expectNothingAttached(npc, slotPart);
        EXPECT_TRUE(npc.soundsToPlay.empty());
    }

    // Finding 5, guard (passes against the stub): attachPart for a part whose read succeeds does reach the item
    // lookup (the test actor's empty Ptr throws a std::runtime_error there), which shows that the test above passes
    // because the lookup was skipped and not because it never exists.
    TEST_F(PartFailuresTest, attach_part_with_a_good_read_reaches_the_item_lookup)
    {
        FailureNpc& npc = *mNpc;

        EXPECT_THROW(attach(npc, goodModel), std::runtime_error);
    }

    // Finding 5, guard (passes against the stub): a part that took its slot and then failed to read keeps the slot
    // (its priority and group), refuses a second part of the same or a lower priority, and the race's own body part is
    // not attached for the held slot.
    TEST_F(PartFailuresTest, a_part_that_took_its_slot_and_failed_keeps_the_slot_taken)
    {
        FailureNpc& npc = *mNpc;

        EXPECT_TRUE(addOrReplace(npc, brokenModel, slotGroup, slotPriority));
        expectNothingAttached(npc, slotPart);
        EXPECT_TRUE(npc.soundsToPlay.empty());
        EXPECT_EQ(npc.mSlots.priority(slotPart), slotPriority);
        EXPECT_EQ(npc.mSlots.groups()[slotPart], slotGroup);

        EXPECT_FALSE(addOrReplace(npc, goodModel, Inv::Slot_Shirt, slotPriority));
        EXPECT_FALSE(addOrReplace(npc, goodModel, Inv::Slot_Shirt, slotPriority - 1));
        EXPECT_TRUE(npc.mParts[slotPart].empty());
        EXPECT_EQ(npc.mSlots.groups()[slotPart], slotGroup);

        // The race's stock body parts: asked for every slot nothing holds, never for the held one.
        ESM::BodyPart skin;
        skin.mData.mType = static_cast<unsigned char>(ESM::BodyPart::MT_Skin);
        std::array<const ESM::BodyPart*, ESM::PRT_Count> raceParts{};
        raceParts[ESM::PRT_Cuirass] = &skin;
        raceParts[ESM::PRT_Groin] = &skin;
        NoNodes nodes;
        std::vector<ESM::PartReferenceType> asked;

        npc.mSlots.addBodyParts(raceParts, {}, nodes,
            [&](ESM::PartReferenceType part, const ESM::BodyPart& /*bodyPart*/) { asked.push_back(part); });

        EXPECT_EQ(asked, (std::vector<ESM::PartReferenceType>{ ESM::PRT_Groin }));
    }

    // The merged slots of the test: the two the tests use.
    const std::vector<int> mergedSlots{ ESM::PRT_Cuirass, ESM::PRT_Groin };

    // Finding 5, guard (passes against the stub): the merged body's region for the slot stays hidden after a failed
    // read, exactly as after a successful one.
    TEST_F(PartFailuresTest, the_merged_body_region_of_a_failed_part_stays_hidden_as_after_a_good_one)
    {
        FailureNpc& failed = *mNpc;
        addOrReplace(failed, brokenModel, slotGroup, slotPriority);
        const std::vector<int> hiddenAfterFailure
            = Body::hiddenMergedSlots(mergedSlots, failed.mSlots.groups(), failed.mSlots.priorities());

        const std::unique_ptr<FailureNpc> good = makeNpc();
        // The good part's sound lookup throws on the empty Ptr after the slot was taken.
        EXPECT_THROW(addOrReplace(*good, goodModel, slotGroup, slotPriority), std::runtime_error);
        const std::vector<int> hiddenAfterSuccess
            = Body::hiddenMergedSlots(mergedSlots, good->mSlots.groups(), good->mSlots.priorities());

        EXPECT_EQ(hiddenAfterFailure, (std::vector<int>{ ESM::PRT_Cuirass }));
        EXPECT_EQ(hiddenAfterSuccess, hiddenAfterFailure);
    }

    // Finding 5, guard (passes against the stub): a read that throws a std::logic_error (not the support reader's
    // ReadError) is caught the same way.
    TEST_F(PartFailuresTest, a_read_that_throws_a_logic_error_is_caught_like_any_std_exception)
    {
        FailureNpc& npc = *mNpc;
        bool inserted = true;

        EXPECT_NO_THROW(inserted = insert(npc, logicErrorModel));
        EXPECT_NO_THROW(attach(npc, logicErrorModel));

        EXPECT_FALSE(inserted);
        expectNothingAttached(npc, slotPart);
        EXPECT_TRUE(npc.soundsToPlay.empty());
    }

    // Finding 5, guard (passes against the stub): a thrown value that is not a std::exception reaches the caller of
    // insertBoundedPart and of attachPart unchanged, and the state is consistent: no node, no controller.
    TEST_F(PartFailuresTest, a_value_that_is_not_an_exception_propagates_with_nothing_attached)
    {
        FailureNpc& npc = *mNpc;

        EXPECT_THROW(insert(npc, notAnExceptionModel), PartsTestSupport::NotAnException);
        expectNothingAttached(npc, slotPart);

        EXPECT_THROW(attach(npc, notAnExceptionModel), PartsTestSupport::NotAnException);
        expectNothingAttached(npc, slotPart);
        EXPECT_TRUE(npc.soundsToPlay.empty());
    }

    // Finding 5, guard (passes against the stub): through addOrReplaceIndividualPart the same value propagates with
    // the slot taken, and still no node or controller.
    TEST_F(PartFailuresTest, a_value_that_is_not_an_exception_leaves_the_taken_slot_without_a_node)
    {
        FailureNpc& npc = *mNpc;

        EXPECT_THROW(addOrReplace(npc, notAnExceptionModel, slotGroup, slotPriority), PartsTestSupport::NotAnException);

        expectNothingAttached(npc, slotPart);
        EXPECT_EQ(npc.mSlots.priority(slotPart), slotPriority);
        EXPECT_EQ(npc.mSlots.groups()[slotPart], slotGroup);
    }

    // Finding 5, guard (passes against the stub): a vsg::Exception (it does not derive from std::exception) thrown
    // by the reader propagates out of insertBoundedPart and attachPart unchanged, with the same state.
    TEST_F(PartFailuresTest, a_vsg_exception_propagates_with_nothing_attached)
    {
        FailureNpc& npc = *mNpc;

        try
        {
            insert(npc, vsgExceptionModel);
            ADD_FAILURE() << "insertBoundedPart did not throw";
        }
        catch (const vsg::Exception& error)
        {
            EXPECT_EQ(error.result, 3);
            EXPECT_NE(error.message.find("vsg error reading"), std::string::npos);
        }
        expectNothingAttached(npc, slotPart);

        EXPECT_THROW(attach(npc, vsgExceptionModel), vsg::Exception);
        expectNothingAttached(npc, slotPart);
        EXPECT_TRUE(npc.soundsToPlay.empty());
    }

    // A creature that wields, with the item functions in reach.
    struct FailureCreature : public MWRender::WieldingCreature
    {
        using MWRender::WieldingCreature::addItemPart;
        using MWRender::WieldingCreature::mPartControllers;
        using MWRender::WieldingCreature::mStaticControllers;
        using MWRender::WieldingCreature::mWeaponControllers;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    struct CreaturePartFailuresTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        MWAnim::Context mContext;
        // Declared after the context: it goes first.
        std::unique_ptr<FailureCreature> mCreature;

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, PartsTestSupport::quietCompileContext(), mReader);
            mReader->onRead = [](const std::string& name) {
                if (name.ends_with(logicErrorItemModel))
                    throw std::logic_error("logic error reading " + name);
                if (name.ends_with(vsgExceptionItemModel))
                    throw vsg::Exception{ "vsg error reading " + name, 5 };
            };
            mCreature = std::make_unique<FailureCreature>(mContext);
            PartsTestSupport::addCreatureSkeleton(
                *mCreature, PartsTestSupport::makeSkeleton({ "Weapon Bone", "Weapon Bone Left", "Shield Bone" }));
        }
    };

    // Finding 5, guard (passes against the stub): the creature's item whose read throws a std::logic_error is caught
    // ("Can not add creature part") and attaches nothing.
    TEST_F(CreaturePartFailuresTest, an_item_read_that_throws_a_logic_error_is_caught_and_attaches_nothing)
    {
        FailureCreature& creature = *mCreature;
        const std::size_t nodesBefore = PartsTestSupport::countNodes(*creature.node());
        PartsTestSupport::LightItem torch(ESM::Light::Carry | ESM::Light::Flicker, logicErrorItemModel);

        EXPECT_NO_THROW(creature.addItemPart(Inv::Slot_CarriedLeft, torch.ptr(), "Weapon Bone Left"));

        EXPECT_TRUE(PartsTestSupport::lightsUnder(*creature.node()).empty());
        EXPECT_EQ(PartsTestSupport::countNodes(*creature.node()), nodesBefore);
        EXPECT_TRUE(creature.mPartControllers.entries(1).empty());
        EXPECT_TRUE(creature.mStaticControllers.controllers.empty());
        EXPECT_TRUE(creature.mWeaponControllers.controllers.empty());
    }

    // Finding 5, guard (passes against the stub): a vsg::Exception from the creature's item read propagates.
    TEST_F(CreaturePartFailuresTest, an_item_read_that_throws_a_vsg_exception_propagates)
    {
        FailureCreature& creature = *mCreature;
        const std::size_t nodesBefore = PartsTestSupport::countNodes(*creature.node());
        PartsTestSupport::LightItem torch(ESM::Light::Carry | ESM::Light::Flicker, vsgExceptionItemModel);

        EXPECT_THROW(creature.addItemPart(Inv::Slot_CarriedLeft, torch.ptr(), "Weapon Bone Left"), vsg::Exception);

        EXPECT_EQ(PartsTestSupport::countNodes(*creature.node()), nodesBefore);
        EXPECT_TRUE(creature.mPartControllers.entries(1).empty());
    }
}
