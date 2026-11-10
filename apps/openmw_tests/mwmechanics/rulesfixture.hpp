// Task 0098, move 5, stage 2: a fixture for the characterization tests of the small free-function rules (security,
// pickpocket, difficulty scaling, levelled lists, spell autocalc). It builds on the crafting fixture of task 0085 (a
// real ESMStore, the real NPC class, test doubles of the MWBase managers) and adds the records, classes and GMSTs those
// rules read.
//
// Records and numbers are TEST INPUTS: the tests pin what the code computes from them, so that moving the code cannot
// change a result. Several of the rules cache a GMST in a function-local static on their first call, so the GMST values
// below are the same for the whole process and no test may change them.
#ifndef OPENEDEN_TESTS_MWMECHANICS_RULESFIXTURE_H
#define OPENEDEN_TESTS_MWMECHANICS_RULESFIXTURE_H

#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadlock.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadprob.hpp>
#include <components/esm3/loadspel.hpp>

#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/creaturelevlist.hpp"
#include "apps/openmw/mwclass/itemlevlist.hpp"
#include "apps/openmw/mwclass/lockpick.hpp"
#include "apps/openmw/mwclass/probe.hpp"
#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwworld/cellref.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"

#include "craftingfixture.hpp"

namespace CraftingTest
{
    /// The mechanics double of the crafting fixture plus the one function the security code calls.
    class RulesMechanics : public FakeMechanicsManager
    {
    public:
        struct Attempt
        {
            MWWorld::Ptr mActor;
            MWWorld::Ptr mItem;
        };
        std::vector<Attempt> mUnlockAttempts;

        void unlockAttempted(const MWWorld::Ptr& ptr, const MWWorld::Ptr& item) override
        {
            mUnlockAttempts.push_back({ ptr, item });
        }
    };

    inline constexpr float kPickLockMult = -1.f;
    inline constexpr float kTrapCostMult = -1.f;
    inline constexpr int kPickMinChance = 5;
    inline constexpr int kPickMaxChance = 75;
    inline constexpr float kPickPocketMod = 0.5f;
    inline constexpr float kDifficultyMult = 5.f;

    class RulesTest : public CraftingRulesTest
    {
    protected:
        void SetUp() override
        {
            CraftingRulesTest::SetUp();

            MWClass::Container::registerSelf();
            MWClass::Lockpick::registerSelf();
            MWClass::Probe::registerSelf();
            MWClass::ItemLevList::registerSelf();
            MWClass::CreatureLevList::registerSelf();

            addFloatGmst("fPickLockMult", kPickLockMult);
            addFloatGmst("fTrapCostMult", kTrapCostMult);
            addIntGmst("iPickMinChance", kPickMinChance);
            addIntGmst("iPickMaxChance", kPickMaxChance);
            addFloatGmst("fPickPocketMod", kPickPocketMod);
            addFloatGmst("fDifficultyMult", kDifficultyMult);
            addFloatGmst("fAutoSpellChance", 40.f);
            addFloatGmst("fAutoPCSpellChance", 60.f);
            addIntGmst("iAutoPCSpellMax", 2);
            addIntGmst("iAutoSpellTimesCanCast", 3);
            addIntGmst("iAutoSpellAttSkillMin", 35);

            mEnvironment->setMechanicsManager(mRulesMechanics);
        }

        void TearDown() override
        {
            mLocks.clear();
            mOtherNpcs.clear();
            CraftingRulesTest::TearDown();
        }

        /// Another NPC with the same stats as the player's record (attributes 50, skills 30, full fatigue).
        MWWorld::Ptr makeNpc(const std::string& id)
        {
            ESM::NPC record = mNpc;
            record.mId = rid(id);
            const ESM::NPC* stored = mStore.insertStatic(record);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = stored->mId;
            cellRef.mRefNum = ESM::RefNum{ .mIndex = static_cast<uint32_t>(10 + mOtherNpcs.size()), .mContentFile = 0 };
            mOtherNpcs.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::NPC>>(cellRef, stored));
            return MWWorld::Ptr(mOtherNpcs.back().get());
        }

        /// A container (a lock) with the given lock level and trap spell (empty for none).
        MWWorld::Ptr makeLock(int lockLevel, const ESM::RefId& trap = ESM::RefId())
        {
            ESM::Container record;
            record.blank();
            record.mId = rid("test_chest" + std::to_string(mLocks.size()));
            record.mName = "chest";
            const ESM::Container* stored = mStore.insertStatic(record);
            ESM::CellRef cellRef;
            cellRef.blank();
            cellRef.mRefID = stored->mId;
            cellRef.mRefNum = ESM::RefNum{ .mIndex = static_cast<uint32_t>(100 + mLocks.size()), .mContentFile = 0 };
            mLocks.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::Container>>(cellRef, stored));
            MWWorld::Ptr ptr(mLocks.back().get());
            if (lockLevel != 0)
                ptr.getCellRef().lock(lockLevel);
            if (!trap.empty())
                ptr.getCellRef().setTrap(trap);
            return ptr;
        }

        void addLockpick(const std::string& id, float quality, int uses)
        {
            ESM::Lockpick tool;
            tool.blank();
            tool.mId = rid(id);
            tool.mName = id;
            tool.mData.mQuality = quality;
            tool.mData.mUses = uses;
            tool.mData.mWeight = 1.f;
            tool.mData.mValue = 10;
            mStore.insertStatic(tool);
        }

        void addProbe(const std::string& id, float quality, int uses)
        {
            ESM::Probe tool;
            tool.blank();
            tool.mId = rid(id);
            tool.mName = id;
            tool.mData.mQuality = quality;
            tool.mData.mUses = uses;
            tool.mData.mWeight = 1.f;
            tool.mData.mValue = 10;
            mStore.insertStatic(tool);
        }

        void addMisc(const std::string& id, int value)
        {
            ESM::Miscellaneous item;
            item.blank();
            item.mId = rid(id);
            item.mName = id;
            item.mData.mWeight = 1.f;
            item.mData.mValue = value;
            mStore.insertStatic(item);
        }

        /// Count of the item with that id in the player's inventory.
        int countOf(const ESM::RefId& id) { return mPlayer.getClass().getContainerStore(mPlayer).count(id); }

        /// How many roll0to99-sized draws the world generator has made since \a seed, found by comparing it with a
        /// sibling that made 0, 1, 2 ... draws. This does not depend on how a standard library maps the generator's
        /// output to a roll (both sides use the same library). -1 if no count up to \a limit matches.
        int drawsSince(unsigned seed, int limit = 12)
        {
            for (int draws = 0; draws <= limit; ++draws)
            {
                Misc::Rng::Generator sibling(seed);
                for (int i = 0; i < draws; ++i)
                    Misc::Rng::roll0to99(sibling);
                Misc::Rng::Generator mine = mWorld.mPrng;
                bool same = true;
                for (int i = 0; i < 4 && same; ++i)
                    same = Misc::Rng::roll0to99(sibling) == Misc::Rng::roll0to99(mine);
                if (same)
                    return draws;
            }
            return -1;
        }

        RulesMechanics mRulesMechanics;
        std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::NPC>>> mOtherNpcs;
        std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Container>>> mLocks;
    };

    /// Compares \a actual with the recording in apps/openmw_tests/mwmechanics/data/<name>. With the environment
    /// variable OPENEDEN_WRITE_RULES_GOLDEN set to a folder, the recording is written there instead (and the test
    /// fails, so that nobody mistakes writing for checking).
    inline void expectRecording(const std::string& name, const std::string& actual)
    {
        if (const char* folder = std::getenv("OPENEDEN_WRITE_RULES_GOLDEN"))
        {
            std::ofstream(std::string(folder) + "/" + name, std::ios::binary) << actual;
            FAIL() << "wrote the recording " << name << " to " << folder;
        }
        const std::string path = std::string(reinterpret_cast<const char*>(OPENMW_PROJECT_SOURCE_DIR))
            + "/apps/openmw_tests/mwmechanics/data/" + name;
        std::ifstream file(path, std::ios::binary);
        ASSERT_TRUE(file.good()) << "cannot open " << path;
        std::string expected((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        // a Windows checkout may have turned the line ends into CR LF
        expected.erase(std::remove(expected.begin(), expected.end(), '\r'), expected.end());
        ASSERT_EQ(actual, expected);
    }
}

#endif
