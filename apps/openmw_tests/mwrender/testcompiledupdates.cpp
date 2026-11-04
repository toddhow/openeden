// Tests for task 0074e, the fresh review of task 0074d (finding 5): an actor's equipment change compiles the actor on
// every way out, through the production seams the game routes its node work through (MWRender::Npc::runCompiledUpdate,
// used by Npc::updateEquipment and the rebuild, and MWRender::WieldingCreature::runCompiledUpdate, used by
// WieldingCreature::updateEquipment): the actor's real compile() runs exactly once whether the node work returns or
// throws, and what it throws reaches the caller unchanged. Two guards pin the review's mutations: a creature's carried
// light item gives light (addItemPart), and a weapon removed in the update that inserted it leaves the weapon list
// (detachPart). Written from the task's test specification before the implementation and run red against its stub.

#include "partstestsupport.hpp"

#include "apps/openmw/mwrender/npc.hpp"
#include "apps/openmw/mwrender/wieldingcreature.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"

#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/mwanimation/context.hpp>
#include <components/mwanimation/partbones.hpp>

#include <vsg/core/ref_ptr.h>
#include <vsg/lighting/Light.h>
#include <vsg/nodes/Group.h>

#include <gtest/gtest.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using PartsTestSupport::frameTime;

    // The models (lower case, as the reader compares them); the creature's item by its bare file name, whatever path
    // the item's model is read with.
    constexpr const char* weaponModel = "meshes/test/compiled_weapon.nif";
    constexpr const char* lanternModel = "compiled_lantern.nif";

    // An Npc built without a record (Npc's test constructor), with the node work and the part functions in reach.
    struct CompiledNpc : public MWRender::Npc
    {
        explicit CompiledNpc(const MWAnim::Context& mwContext)
            : MWRender::Npc(mwContext, TestSetup{})
        {
        }

        using MWRender::Npc::insertBoundedPart;
        using MWRender::Npc::mWeaponControllers;
        using MWRender::Npc::removeIndividualPart;
        using MWRender::Npc::runCompiledUpdate;
        using MWRender::Npc::runPartUpdate;
    };

    // A creature that wields (MWAnim::createCreature for a creature with weapons), with its node work in reach.
    struct CompiledCreature : public MWRender::WieldingCreature
    {
        using MWRender::WieldingCreature::addItemPart;
        using MWRender::WieldingCreature::runCompiledUpdate;
        using MWRender::WieldingCreature::WieldingCreature;
    };

    // The message of the UpdateError `run` throws, or nothing when it returns (anything else reaches the test, and
    // gtest reports it as a failure).
    template <class Run>
    std::optional<std::string> updateErrorOf(Run&& run)
    {
        try
        {
            run();
        }
        catch (const PartsTestSupport::UpdateError& error)
        {
            return std::string(error.what());
        }
        return std::nullopt;
    }

    // The code of the NotAnException `run` throws, or nothing when it returns.
    template <class Run>
    std::optional<int> notAnExceptionOf(Run&& run)
    {
        try
        {
            run();
        }
        catch (const PartsTestSupport::NotAnException& value)
        {
            return value.mCode;
        }
        return std::nullopt;
    }

    struct CompiledUpdatesTest : public ::testing::Test
    {
        vsg::ref_ptr<PartsTestSupport::PartsReader> mReader{ new PartsTestSupport::PartsReader };
        PartsTestSupport::CountingCompile mCompile;
        MWAnim::Context mContext;
        // Declared after the context: they go first.
        std::unique_ptr<CompiledNpc> mNpc;
        std::unique_ptr<CompiledCreature> mCreature;
        // The node work and the compiles, in order.
        std::vector<std::string> mCalls;
        // Read from data, so the compiler does not see node work that always throws (MSVC's C4702 on the code after
        // it).
        const std::vector<int> mFailures{ 1 };

        void SetUp() override
        {
            PartsTestSupport::setUpContext(mContext, mCompile.context(), mReader);
            mCompile.onCompile = [this] { mCalls.emplace_back("compile"); };
            mNpc = std::make_unique<CompiledNpc>(mContext);
            const vsg::ref_ptr<vsg::Group> skeleton = PartsTestSupport::makeSkeleton({ "Weapon Bone", "Groin" });
            mNpc->animation = PartsTestSupport::npcPlay(*skeleton);
            mCreature = std::make_unique<CompiledCreature>(mContext);
            PartsTestSupport::addCreatureSkeleton(
                *mCreature, PartsTestSupport::makeSkeleton({ "Weapon Bone", "Weapon Bone Left", "Shield Bone" }));
        }

        void TearDown() override { mCompile.onCompile = nullptr; }

        void runNpcFrames(int frames)
        {
            for (int i = 0; i < frames; ++i)
                mNpc->update(frameTime);
        }
    };

    // Finding 5: an NPC's node work that throws a std::exception is compiled exactly once, and the exception reaches
    // the caller unchanged.
    TEST_F(CompiledUpdatesTest, an_npc_update_that_throws_a_std_exception_is_compiled_once_and_passes_it_on)
    {
        auto nodes = [&] {
            mCalls.emplace_back("nodes");
            if (!mFailures.empty())
                throw PartsTestSupport::UpdateError("npc nodes");
        };

        EXPECT_EQ(updateErrorOf([&] { mNpc->runCompiledUpdate(nodes); }), std::optional<std::string>{ "npc nodes" });

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_EQ(mCalls, (std::vector<std::string>{ "nodes", "compile" }));
    }

    // Finding 5: an NPC's node work that throws a value that is not a std::exception is compiled exactly once, and the
    // value reaches the caller unchanged.
    TEST_F(CompiledUpdatesTest, an_npc_update_that_throws_another_value_is_compiled_once_and_passes_it_on)
    {
        auto nodes = [&] {
            mCalls.emplace_back("nodes");
            if (!mFailures.empty())
                throw PartsTestSupport::NotAnException{ 11 };
        };

        EXPECT_EQ(notAnExceptionOf([&] { mNpc->runCompiledUpdate(nodes); }), std::optional<int>{ 11 });

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_EQ(mCalls, (std::vector<std::string>{ "nodes", "compile" }));
    }

    // Finding 5: an NPC's node work that returns is compiled exactly once, after it.
    TEST_F(CompiledUpdatesTest, an_npc_update_that_returns_is_compiled_once)
    {
        mNpc->runCompiledUpdate([&] { mCalls.emplace_back("nodes"); });

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_EQ(mCalls, (std::vector<std::string>{ "nodes", "compile" }));
    }

    // Finding 5: a creature's node work that throws a std::exception is compiled exactly once, and the exception
    // reaches the caller unchanged.
    TEST_F(CompiledUpdatesTest, a_creature_update_that_throws_a_std_exception_is_compiled_once_and_passes_it_on)
    {
        auto nodes = [&] {
            mCalls.emplace_back("nodes");
            if (!mFailures.empty())
                throw PartsTestSupport::UpdateError("creature nodes");
        };

        EXPECT_EQ(updateErrorOf([&] { mCreature->runCompiledUpdate(nodes); }),
            std::optional<std::string>{ "creature nodes" });

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_EQ(mCalls, (std::vector<std::string>{ "nodes", "compile" }));
    }

    // Finding 5: a creature's node work that throws a value that is not a std::exception is compiled exactly once, and
    // the value reaches the caller unchanged.
    TEST_F(CompiledUpdatesTest, a_creature_update_that_throws_another_value_is_compiled_once_and_passes_it_on)
    {
        auto nodes = [&] {
            mCalls.emplace_back("nodes");
            if (!mFailures.empty())
                throw PartsTestSupport::NotAnException{ 13 };
        };

        EXPECT_EQ(notAnExceptionOf([&] { mCreature->runCompiledUpdate(nodes); }), std::optional<int>{ 13 });

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_EQ(mCalls, (std::vector<std::string>{ "nodes", "compile" }));
    }

    // Finding 5: a creature's node work that returns is compiled exactly once, after it.
    TEST_F(CompiledUpdatesTest, a_creature_update_that_returns_is_compiled_once)
    {
        mCreature->runCompiledUpdate([&] { mCalls.emplace_back("nodes"); });

        EXPECT_EQ(mCompile.count(), 1);
        EXPECT_EQ(mCalls, (std::vector<std::string>{ "nodes", "compile" }));
    }

    // Finding 5, guard against the review's mutation "addPart's light choice back to nullptr": a creature's carried
    // flickering light item gives exactly one light under the creature, and it flickers.
    TEST_F(CompiledUpdatesTest, a_carried_flickering_light_item_gives_the_creature_one_flickering_light)
    {
        CompiledCreature& creature = *mCreature;
        mReader->add(lanternModel, PartsTestSupport::makePlainModel());
        PartsTestSupport::LightItem lantern(ESM::Light::Carry | ESM::Light::Flicker, lanternModel);

        creature.addItemPart(MWWorld::InventoryStore::Slot_CarriedLeft, lantern.ptr(), "Weapon Bone Left");

        const std::vector<vsg::Light*> lights = PartsTestSupport::lightsUnder(*creature.node());
        ASSERT_EQ(lights.size(), 1u);
        EXPECT_GT(PartsTestSupport::lightColours(creature, *lights[0], 30).size(), 10u);
    }

    // Finding 5, guard against the review's mutation "delete the Weapon case of detachPart": a weapon inserted and
    // removed in the same part update leaves its controller out of the weapon list, and it never runs.
    TEST_F(CompiledUpdatesTest, a_weapon_inserted_and_removed_in_one_update_leaves_the_weapon_list)
    {
        CompiledNpc& npc = *mNpc;
        vsg::ref_ptr<PartsTestSupport::Recorder> passing = PartsTestSupport::listRecorder();
        mReader->add(weaponModel, PartsTestSupport::makeModel({ passing }));

        npc.runPartUpdate([&] {
            npc.insertBoundedPart(
                ESM::PRT_Weapon, weaponModel, MWAnim::sPartBones.at(ESM::PRT_Weapon), std::nullopt, nullptr);
            npc.removeIndividualPart(ESM::PRT_Weapon);
        });

        EXPECT_EQ(PartsTestSupport::countIn(npc.mWeaponControllers, passing.get()), 0u);
        runNpcFrames(3);
        EXPECT_TRUE(passing->mTimes.empty());
    }

    // Finding 5, guard: a weapon inserted in an earlier part update and kept is linked once in the weapon list, and
    // runs on every update.
    TEST_F(CompiledUpdatesTest, a_weapon_kept_from_an_earlier_update_is_linked_once)
    {
        CompiledNpc& npc = *mNpc;
        vsg::ref_ptr<PartsTestSupport::Recorder> kept = PartsTestSupport::listRecorder();
        mReader->add(weaponModel, PartsTestSupport::makeModel({ kept }));
        npc.runPartUpdate([&] {
            npc.insertBoundedPart(
                ESM::PRT_Weapon, weaponModel, MWAnim::sPartBones.at(ESM::PRT_Weapon), std::nullopt, nullptr);
        });

        npc.runPartUpdate([] {});

        EXPECT_EQ(PartsTestSupport::countIn(npc.mWeaponControllers, kept.get()), 1u);
        const std::size_t runs = kept->mTimes.size();
        runNpcFrames(3);
        EXPECT_EQ(kept->mTimes.size(), runs + 3);
    }

    // Finding 5, through the production entry point (added by the implementing agent, not the test writer): the real
    // Npc::updateEquipment compiles the actor although its part update throws. A test Npc has no record, so
    // updateParts fails at once on the empty Ptr (MWWorld::Ptr::getClass throws std::runtime_error). Reverting
    // updateEquipment to "compile only after a part update that returns" fails this test.
    TEST_F(CompiledUpdatesTest, the_real_npc_update_equipment_compiles_when_its_part_update_throws)
    {
        EXPECT_THROW(mNpc->updateEquipment(), std::runtime_error);

        EXPECT_EQ(mCompile.count(), 1);
    }

    // Finding 5, as above for WieldingCreature::updateEquipment: removeParts runs, then addParts fails on the empty
    // Ptr, and the creature is compiled. Reverting it to compile-only-on-success fails this test.
    TEST_F(CompiledUpdatesTest, the_real_creature_update_equipment_compiles_when_its_part_update_throws)
    {
        EXPECT_THROW(mCreature->updateEquipment(), std::runtime_error);

        EXPECT_EQ(mCompile.count(), 1);
    }
}
