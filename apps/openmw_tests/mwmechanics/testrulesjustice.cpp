// Task 0098, move 7, stage 2: characterization tests of the crime and stolen-item rules (commitCrime, reportCrime,
// canReportCrime, itemTaken, the stolen-item bookkeeping, confiscation, trespass by unlocking, actorAttacked,
// actorKilled) and of the save record that holds the stolen items (ESM::REC_STLN). They run the real
// MWMechanics::MechanicsManager's public functions on real NpcStats, the real NPC, container and miscellaneous
// classes, an in-memory ESMStore and test doubles of the MWBase managers. They pass on the old code, where the rules
// live in mechanicsmanagerimp.cpp, and must stay green and unedited when the rules have moved into the Morrowind game
// pack (installJusticeRulesForTest() is the one line that differs between the two runs).
//
// No actor is registered with the manager (an actor needs an animation), so the only witness is the victim, who is
// always considered even beyond the alarm radius. A crime with no victim is therefore never seen; the in-game check
// (tools/crime_check) covers bystanders. The GMSTs below are test inputs; several rules cache GMSTs in function-local
// statics, so no test may change them.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <sstream>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/stolenitems.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include "apps/openmw/mwbase/dialoguemanager.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwmechanics/mechanicsmanagerimp.hpp"
#include "apps/openmw/mwworld/localscripts.hpp"
#include "apps/openmw/mwworld/player.hpp"

#include "rulesfixture.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif

namespace CraftingTest
{
    using namespace MWBase;
#include "fakes/dialoguemanager.inc"
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

#include "justiceinstall.hpp"
#include "socialinstall.hpp"

namespace CraftingTest
{
    namespace
    {
        using OT = MWBase::MechanicsManager;

        class JusticeDialogue : public GeneratedDialogueManagerBase
        {
        public:
            bool say(const MWWorld::Ptr& actor, const ESM::RefId& topic) override
            {
                mSaid.push_back(actor.getCellRef().getRefId().getRefIdString() + ":" + topic.getRefIdString());
                return true;
            }
            int getFactionReaction(const ESM::RefId&, const ESM::RefId&) const override { return 0; }
            std::vector<std::string> mSaid;
        };

        /// The world as the crime code uses it: the player object, a line of sight that is on or off, a lookup of the
        /// owners by id and the global variables that release ownership.
        class JusticeWorld : public FakeWorld
        {
        public:
            MWWorld::Player* mPlayerObject = nullptr;
            MWWorld::LocalScripts* mLocalScripts = nullptr;
            bool mLineOfSight = true;
            std::map<ESM::RefId, MWWorld::Ptr> mActive;
            std::map<std::string, int> mGlobals;

            MWWorld::Player& getPlayer() override { return *mPlayerObject; }
            bool getLOS(const MWWorld::ConstPtr& actor, const MWWorld::ConstPtr& target) override
            {
                return mLineOfSight;
            }
            MWWorld::Ptr searchPtr(const ESM::RefId& name, bool activeOnly, bool searchInContainers) override
            {
                auto it = mActive.find(name);
                return it == mActive.end() ? MWWorld::Ptr() : it->second;
            }
            int getGlobalInt(MWWorld::GlobalVariableName name) const override
            {
                auto it = mGlobals.find(std::string(name.getValue()));
                return it == mGlobals.end() ? 0 : it->second;
            }
            MWWorld::LocalScripts& getLocalScripts() override { return *mLocalScripts; }
            MWWorld::TimeStamp getTimeStamp() const override { return MWWorld::TimeStamp(); }
            void addContainerScripts(const MWWorld::Ptr&, MWWorld::CellStore*) override {}
            bool isOnGround(const MWWorld::Ptr&) const override { return true; }
            bool isSwimming(const MWWorld::ConstPtr&) const override { return false; }
            bool isFlying(const MWWorld::Ptr&) const override { return false; }
        };

        class JusticeWindow : public FakeWindowManager
        {
        public:
            bool isGuiMode() const override { return false; }
        };

        constexpr float kAlarmRadius = 1000.f;
        constexpr float kFar = 2000.f; // a victim this far from the player is beyond the alarm radius

        std::string toHex(const std::string& bytes)
        {
            std::string out;
            char buffer[4];
            for (unsigned char c : bytes)
            {
                std::snprintf(buffer, sizeof(buffer), "%02x", c);
                out += buffer;
            }
            return out;
        }

        std::vector<std::string> recordNames(const std::string& bytes)
        {
            std::vector<std::string> names;
            size_t pos = 0;
            while (pos + 16 <= bytes.size())
            {
                names.emplace_back(bytes.substr(pos, 4));
                std::uint32_t size = 0;
                std::memcpy(&size, bytes.data() + pos + 4, 4);
                pos += 16 + size;
            }
            EXPECT_EQ(pos, bytes.size());
            return names;
        }

        class JusticeTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                addFloatGmst("fAlarmRadius", kAlarmRadius);
                addIntGmst("iCrimeTresspass", 30);
                addFloatGmst("iDispTresspass", -5.f);
                addIntGmst("iCrimePickPocket", 25);
                addFloatGmst("fDispPickPocketMod", -20.f);
                addIntGmst("iCrimeAttack", 40);
                addFloatGmst("iDispAttackMod", -50.f);
                addFloatGmst("fDispAttacking", -20.f);
                addIntGmst("iCrimeKilling", 1000);
                addFloatGmst("iDispKilling", -50.f);
                addFloatGmst("fCrimeStealing", 1.f);
                addFloatGmst("fDispStealing", -0.5f);
                addIntGmst("iFightTrespass", 30);
                addIntGmst("iFightPickpocket", 25);
                addIntGmst("iFightAttacking", 50);
                addIntGmst("iFightAttack", 100);
                addIntGmst("iFightKilling", 50);
                addIntGmst("fFightStealing", 50);
                addFloatGmst("fFightDispMult", 0.006f);
                addIntGmst("iFightDistanceBase", 50);
                addFloatGmst("fFightDistanceMultiplier", 0.005f);
                addFloatGmst("fSneakDistanceBase", 0.5f);
                addFloatGmst("fSneakDistanceMultiplier", 0.002f);
                addFloatGmst("fSneakNoViewMult", 0.5f);
                addFloatGmst("fSneakViewMult", 1.f);
                addFloatGmst("fSneakSkillMult", 1.f);
                addFloatGmst("fSneakBootMult", 1.f);
                addFloatGmst("fDispRaceMod", 5.f);
                addFloatGmst("fDispPersonalityMult", 0.5f);
                addFloatGmst("fDispPersonalityBase", 50.f);
                addFloatGmst("fDispFactionRankMult", 2.f);
                addFloatGmst("fDispFactionRankBase", 1.f);
                addFloatGmst("fDispFactionMod", 0.5f);
                addFloatGmst("fDispCrimeMod", 0.25f);
                addFloatGmst("fDispDiseaseMod", -12.f);
                addFloatGmst("fDispWeaponDrawn", -7.f);
                MWClass::Container::registerSelf();

                mPlayerObject = std::make_unique<MWWorld::Player>(mStore.get<ESM::NPC>().find(rid("player")));
                mLocalScripts = std::make_unique<MWWorld::LocalScripts>(mStore);
                mJusticeWorld.mLocalScripts = mLocalScripts.get();
                mJusticeWorld.mPlayerObject = mPlayerObject.get();
                mJusticeWorld.mPlayer = mPlayer;
                mJusticeWorld.mStore = &mStore;
                mEnvironment->setWorld(mJusticeWorld);
                mEnvironment->setWindowManager(mJusticeWindow);
                mEnvironment->setDialogueManager(mDialogue);
                mEnvironment->setMechanicsManager(mManager);
                installSocialRulesForTest();
                installJusticeRulesForTest();
                setPosition(mPlayer, 0.f);
            }

            void TearDown() override
            {
                mItems.clear();
                mJusticeWorld.mActive.clear();
                mPlayerObject.reset();
                mLocalScripts.reset();
                RulesTest::TearDown();
            }

            static void setPosition(const MWWorld::Ptr& ptr, float x)
            {
                ESM::Position position;
                position.pos[0] = x;
                position.pos[1] = 0.f;
                position.pos[2] = 0.f;
                position.rot[0] = position.rot[1] = position.rot[2] = 0.f;
                ptr.getRefData().setPosition(position);
            }

            MWMechanics::NpcStats& statsOf(const MWWorld::Ptr& ptr) { return ptr.getClass().getNpcStats(ptr); }

            /// A far-away NPC that the world can find by id.
            MWWorld::Ptr makeVictim(const std::string& id, int alarm, int fight = 0)
            {
                MWWorld::Ptr npc = makeNpc(id);
                setPosition(npc, kFar);
                statsOf(npc).setAiSetting(MWMechanics::AiSetting::Alarm, alarm);
                statsOf(npc).setAiSetting(MWMechanics::AiSetting::Fight, fight);
                statsOf(npc).setBaseDisposition(50);
                mJusticeWorld.mActive[rid(id)] = npc;
                return npc;
            }

            /// A miscellaneous item in the world (not an inventory) with an owner and a faction.
            MWWorld::Ptr makeItem(
                const std::string& id, int value, const char* owner, const char* faction = "", const char* global = "")
            {
                addMisc(id, value);
                const ESM::Miscellaneous* record = mStore.get<ESM::Miscellaneous>().find(rid(id));
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefID = record->mId;
                cellRef.mGlobalVariable = global;
                cellRef.mRefNum = ESM::RefNum{ .mIndex = static_cast<uint32_t>(300 + mItems.size()), .mContentFile = 0 };
                mItems.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::Miscellaneous>>(cellRef, record));
                MWWorld::Ptr ptr(mItems.back().get());
                if (owner[0])
                    ptr.getCellRef().setOwner(rid(owner));
                if (faction[0])
                    ptr.getCellRef().setFaction(rid(faction));
                return ptr;
            }

            /// One line describing what a crime did to the player and the victim.
            std::string describe(OT::OffenseType type, int alarm, int fight, bool aware)
            {
                const std::string id = "grid_victim_" + std::to_string(mGridSerial++);
                MWWorld::Ptr victim = makeVictim(id, alarm, fight);
                statsOf(mPlayer).setBounty(0);
                mDialogue.mSaid.clear();
                const bool seen = mManager.commitCrime(mPlayer, victim, type, ESM::RefId(), 60, aware);
                MWMechanics::NpcStats& v = statsOf(victim);
                std::ostringstream out;
                out << "type=" << type << " alarm=" << alarm << " fight=" << fight << " aware=" << aware
                    << " seen=" << seen << " bounty=" << bounty() << " crimeId=" << v.getCrimeId()
                    << " dispBase=" << v.getBaseDisposition() << " dispCrime=" << v.getCrimeDispositionModifier()
                    << " fightBase=" << v.getAiSetting(MWMechanics::AiSetting::Fight).getBase()
                    << " alarmed=" << v.isAlarmed() << " inCombat=" << v.getAiSequence().isInCombat() << " said=";
                for (const std::string& s : mDialogue.mSaid)
                    out << s << ",";
                out << '\n';
                return out.str();
            }

            int bounty() { return statsOf(mPlayer).getBounty(); }

            JusticeDialogue mDialogue;
            JusticeWindow mJusticeWindow;
            MWMechanics::MechanicsManager mManager;
            JusticeWorld mJusticeWorld;
            std::unique_ptr<MWWorld::Player> mPlayerObject;
            std::unique_ptr<MWWorld::LocalScripts> mLocalScripts;
            int mGridSerial = 0;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Miscellaneous>>> mItems;
        };

        // ------------------------------------------------------------------------------------- commitCrime

        TEST_F(JusticeTest, a_crime_with_no_victim_and_no_bystander_is_not_seen_and_costs_nothing)
        {
            EXPECT_FALSE(mManager.commitCrime(mPlayer, MWWorld::Ptr(), OT::OT_Theft, ESM::RefId(), 100));
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, only_the_player_can_commit_a_crime)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            MWWorld::Ptr other = makeNpc("thief_npc");
            EXPECT_FALSE(mManager.commitCrime(other, victim, OT::OT_Assault));
            EXPECT_EQ(bounty(), 0);
            EXPECT_EQ(statsOf(victim).getCrimeId(), -1);
        }

        TEST_F(JusticeTest, trespassing_in_front_of_an_alarmed_victim_adds_the_trespass_bounty)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            EXPECT_TRUE(mManager.commitCrime(mPlayer, victim, OT::OT_Trespassing));
            EXPECT_EQ(bounty(), 30);
            // a plain witness that only complains gets no crime id (that needs a guard, a disposition change or a fight)
            EXPECT_EQ(statsOf(victim).getCrimeId(), -1);
            ASSERT_EQ(mDialogue.mSaid.size(), 1u);
            EXPECT_EQ(mDialogue.mSaid[0], "victim_a:intruder");
        }

        TEST_F(JusticeTest, a_witness_that_does_not_report_leaves_the_bounty_at_zero)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 50);
            EXPECT_TRUE(mManager.commitCrime(mPlayer, victim, OT::OT_Trespassing));
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, no_line_of_sight_means_the_crime_is_unseen_unless_the_victim_is_already_aware)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            mJusticeWorld.mLineOfSight = false;
            EXPECT_FALSE(mManager.commitCrime(mPlayer, victim, OT::OT_Trespassing));
            EXPECT_EQ(bounty(), 0);
            EXPECT_TRUE(mManager.commitCrime(mPlayer, victim, OT::OT_Trespassing, ESM::RefId(), 0, true));
            EXPECT_EQ(bounty(), 30);
        }

        TEST_F(JusticeTest, an_assault_makes_the_victim_aware_whatever_it_can_see)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            mJusticeWorld.mLineOfSight = false;
            EXPECT_TRUE(mManager.commitCrime(mPlayer, victim, OT::OT_Assault));
            EXPECT_EQ(bounty(), 40);
            EXPECT_EQ(statsOf(victim).getCrimeId(), 0);
        }

        TEST_F(JusticeTest, bounty_is_added_to_the_players_bounty)
        {
            statsOf(mPlayer).setBounty(15);
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            mManager.commitCrime(mPlayer, victim, OT::OT_Trespassing);
            EXPECT_EQ(bounty(), 45);
        }

        TEST_F(JusticeTest, theft_bounty_is_the_value_times_the_stealing_factor_and_at_least_one)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            mManager.commitCrime(mPlayer, victim, OT::OT_Theft, ESM::RefId(), 120);
            EXPECT_EQ(bounty(), 120);
            statsOf(mPlayer).setBounty(0);
            MWWorld::Ptr victim2 = makeVictim("victim_b", 100);
            mManager.commitCrime(mPlayer, victim2, OT::OT_Theft, ESM::RefId(), 0);
            EXPECT_EQ(bounty(), 1);
        }

        TEST_F(JusticeTest, a_theft_is_complained_about_even_when_the_witness_does_not_report)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 0);
            EXPECT_TRUE(mManager.commitCrime(mPlayer, victim, OT::OT_Theft, ESM::RefId(), 10));
            ASSERT_FALSE(mDialogue.mSaid.empty());
            EXPECT_EQ(mDialogue.mSaid[0], "victim_a:thief");
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, murder_is_seen_by_the_victim_and_costs_the_killing_bounty)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            EXPECT_TRUE(mManager.commitCrime(mPlayer, victim, OT::OT_Murder));
            EXPECT_EQ(bounty(), 1000);
        }

        TEST_F(JusticeTest, a_dead_or_knocked_down_witness_does_not_report)
        {
            MWWorld::Ptr dead = makeVictim("victim_a", 100);
            MWMechanics::DynamicStat<float> health = statsOf(dead).getHealth();
            health.setCurrent(0.f);
            statsOf(dead).setHealth(health);
            EXPECT_FALSE(mManager.commitCrime(mPlayer, dead, OT::OT_Trespassing));
            EXPECT_EQ(bounty(), 0);
            MWWorld::Ptr down = makeVictim("victim_b", 100);
            statsOf(down).setKnockedDown(true);
            EXPECT_FALSE(mManager.commitCrime(mPlayer, down, OT::OT_Trespassing));
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, committing_a_crime_against_a_member_of_a_faction_expels_the_player_from_it)
        {
            ESM::Faction faction;
            faction.blank();
            faction.mId = rid("victim_guild");
            faction.mName = "guild";
            for (size_t i = 0; i < faction.mRanks.size(); ++i)
                faction.mRanks[i] = "rank" + std::to_string(i);
            mStore.insertStatic(faction);
            ESM::NPC saved = mNpc;
            mNpc.mFaction = rid("victim_guild");
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            mNpc = saved;
            statsOf(mPlayer).joinFaction(rid("victim_guild"));
            mManager.commitCrime(mPlayer, victim, OT::OT_Trespassing, rid("ignored_faction"));
            EXPECT_EQ(bounty(), 30);
            EXPECT_TRUE(statsOf(mPlayer).getExpelled(rid("victim_guild")));
            EXPECT_FALSE(statsOf(mPlayer).getExpelled(rid("ignored_faction")));
        }

        // ------------------------------------------------------------------------------------- recording

        TEST_F(JusticeTest, commit_crime_grid_matches_the_recording)
        {
            std::string all;
            for (OT::OffenseType type : { OT::OT_Theft, OT::OT_Assault, OT::OT_Murder, OT::OT_Trespassing,
                     OT::OT_SleepingInOwnedBed, OT::OT_Pickpocket })
                for (int alarm : { 0, 30, 100 })
                    for (int fight : { 0, 60 })
                        for (bool aware : { false, true })
                            all += describe(type, alarm, fight, aware);
            expectRecording("rules_justice_commitcrime.golden.txt", all);
        }

        // ------------------------------------------------------------------------------------- theft

        TEST_F(JusticeTest, taking_an_unowned_item_is_allowed_and_leaves_no_record)
        {
            MWWorld::Ptr item = makeItem("gold_a", 10, "");
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 3, true);
            EXPECT_EQ(bounty(), 0);
            EXPECT_TRUE(mManager.getStolenItemOwners(rid("gold_a")).empty());
        }

        TEST_F(JusticeTest, taking_an_item_the_player_owns_is_allowed)
        {
            MWWorld::Ptr item = makeItem("gold_a", 10, "Player");
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 1, true);
            EXPECT_EQ(bounty(), 0);
            EXPECT_TRUE(mManager.getStolenItemOwners(rid("gold_a")).empty());
        }

        TEST_F(JusticeTest, a_witnessed_theft_records_the_stolen_item_and_adds_value_times_count_as_bounty)
        {
            MWWorld::Ptr victim = makeVictim("owner_a", 100);
            MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a");
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 3, true);
            EXPECT_EQ(bounty(), 30);
            const auto owners = mManager.getStolenItemOwners(rid("ring_a"));
            ASSERT_EQ(owners.size(), 1u);
            EXPECT_EQ(owners[0].first, rid("owner_a"));
            EXPECT_EQ(owners[0].second, 3);
            EXPECT_TRUE(mManager.isItemStolenFrom(rid("ring_a"), victim));
            EXPECT_FALSE(mManager.isItemStolenFrom(rid("ring_b"), victim));
            ASSERT_FALSE(mDialogue.mSaid.empty());
            EXPECT_EQ(mDialogue.mSaid[0], "owner_a:thief");
        }

        TEST_F(JusticeTest, taking_without_alarm_records_the_item_but_not_the_crime_and_counts_add_up)
        {
            MWWorld::Ptr victim = makeVictim("owner_a", 100);
            MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a");
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 2, false);
            EXPECT_EQ(bounty(), 0);
            EXPECT_EQ(mManager.getStolenItemOwners(rid("ring_a")).size(), 1u);
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 4, false);
            const auto owners = mManager.getStolenItemOwners(rid("ring_a"));
            ASSERT_EQ(owners.size(), 1u);
            EXPECT_EQ(owners[0].second, 6);
        }

        TEST_F(JusticeTest, items_taken_from_an_owned_container_take_the_containers_owner_or_faction)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr chest = makeLock(0);
            chest.getCellRef().setOwner(rid("owner_a"));
            MWWorld::Ptr item = makeItem("ring_a", 10, "");
            mManager.itemTaken(mPlayer, item, chest, 1, false);
            auto owners = mManager.getStolenItemOwners(rid("ring_a"));
            ASSERT_EQ(owners.size(), 1u);
            EXPECT_EQ(owners[0].first, rid("owner_a"));

            MWWorld::Ptr factionChest = makeLock(0);
            factionChest.getCellRef().setFaction(rid("guild_a"));
            MWWorld::Ptr item2 = makeItem("ring_b", 10, "");
            mManager.itemTaken(mPlayer, item2, factionChest, 2, false);
            owners = mManager.getStolenItemOwners(rid("ring_b"));
            ASSERT_EQ(owners.size(), 1u);
            EXPECT_EQ(owners[0].first, rid("guild_a"));
            EXPECT_EQ(owners[0].second, 2);
        }

        TEST_F(JusticeTest, a_global_variable_that_is_set_releases_the_ownership)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a", "", "owner_gone");
            mJusticeWorld.mGlobals["owner_gone"] = 1;
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 1, true);
            EXPECT_EQ(bounty(), 0);
            EXPECT_TRUE(mManager.getStolenItemOwners(rid("ring_a")).empty());
        }

        TEST_F(JusticeTest, only_the_player_takes_items_illegally)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a");
            MWWorld::Ptr other = makeNpc("other_npc");
            mManager.itemTaken(other, item, MWWorld::Ptr(), 1, true);
            EXPECT_EQ(bounty(), 0);
            EXPECT_TRUE(mManager.getStolenItemOwners(rid("ring_a")).empty());
        }

        TEST_F(JusticeTest, isAllowedToUse_follows_ownership_and_pickpocketing)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr owned = makeItem("ring_a", 10, "owner_a");
            MWWorld::Ptr victim;
            EXPECT_FALSE(mManager.isAllowedToUse(mPlayer, owned, victim));
            EXPECT_EQ(victim.getCellRef().getRefId(), rid("owner_a"));
            MWWorld::Ptr free = makeItem("ring_b", 10, "");
            MWWorld::Ptr none;
            EXPECT_TRUE(mManager.isAllowedToUse(mPlayer, free, none));
            EXPECT_TRUE(mManager.isAllowedToUse(mPlayer, MWWorld::Ptr(), none));

            MWWorld::Ptr npc = makeVictim("pocket_npc", 100);
            EXPECT_TRUE(mManager.isAllowedToUse(mPlayer, npc, none));
            statsOf(npc).setKnockedDown(true);
            EXPECT_FALSE(mManager.isAllowedToUse(mPlayer, npc, none));
        }

        // ------------------------------------------------------------------------------------- stolen items

        TEST_F(JusticeTest, confiscating_to_the_owner_moves_the_items_decrements_the_record_and_is_a_crime)
        {
            MWWorld::Ptr victim = makeVictim("owner_a", 100);
            MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a");
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 3, false);
            MWWorld::Ptr given = give(rid("ring_a"), 3);
            mManager.confiscateStolenItemToOwner(mPlayer, given, victim, 2);
            const auto owners = mManager.getStolenItemOwners(rid("ring_a"));
            ASSERT_EQ(owners.size(), 1u);
            EXPECT_EQ(owners[0].second, 1);
            EXPECT_EQ(countOf(rid("ring_a")), 1);
            EXPECT_EQ(victim.getClass().getContainerStore(victim).count(rid("ring_a")), 2);
            EXPECT_EQ(bounty(), 20);
        }

        TEST_F(JusticeTest, confiscating_something_never_stolen_changes_nothing)
        {
            MWWorld::Ptr victim = makeVictim("owner_a", 100);
            addMisc("ring_c", 7);
            MWWorld::Ptr item = give(rid("ring_c"), 2);
            mManager.confiscateStolenItemToOwner(mPlayer, item, victim, 2);
            EXPECT_EQ(countOf(rid("ring_c")), 2);
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, confiscating_everything_takes_the_stolen_items_into_the_chest_and_locks_it)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a");
            mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 2, false);
            give(rid("ring_a"), 5); // 3 of them were never stolen
            addMisc("ring_c", 7);
            give(rid("ring_c"), 4);
            MWWorld::Ptr chest = makeLock(0);
            mManager.confiscateStolenItems(mPlayer, chest);
            EXPECT_EQ(chest.getClass().getContainerStore(chest).count(rid("ring_a")), 2);
            EXPECT_EQ(countOf(rid("ring_a")), 3);
            EXPECT_EQ(countOf(rid("ring_c")), 4);
            EXPECT_TRUE(mManager.getStolenItemOwners(rid("ring_a")).empty());
            EXPECT_EQ(chest.getCellRef().getLockLevel(), 50);
        }

        // ------------------------------------------------------------------------------------- trespass

        TEST_F(JusticeTest, unlocking_an_owned_locked_thing_is_trespassing)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr chest = makeLock(25);
            chest.getCellRef().setOwner(rid("owner_a"));
            mManager.unlockAttempted(mPlayer, chest);
            EXPECT_EQ(bounty(), 30);
        }

        TEST_F(JusticeTest, unlocking_something_nobody_owns_or_that_was_never_locked_is_no_crime)
        {
            makeVictim("owner_a", 100);
            MWWorld::Ptr unowned = makeLock(25);
            mManager.unlockAttempted(mPlayer, unowned);
            MWWorld::Ptr neverLocked = makeLock(0);
            neverLocked.getCellRef().setOwner(rid("owner_a"));
            mManager.unlockAttempted(mPlayer, neverLocked);
            EXPECT_EQ(bounty(), 0);
        }

        // ------------------------------------------------------------------------------------- attack and kill

        TEST_F(JusticeTest, attacking_a_peaceful_npc_is_an_assault_and_the_victim_fights_back)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            EXPECT_TRUE(mManager.actorAttacked(victim, mPlayer));
            EXPECT_EQ(bounty(), 40);
            EXPECT_TRUE(statsOf(victim).getAiSequence().isInCombat(mPlayer));
        }

        TEST_F(JusticeTest, attacking_the_player_is_no_business_of_the_rules)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            EXPECT_FALSE(mManager.actorAttacked(mPlayer, victim));
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, a_werewolf_may_be_attacked_without_a_crime)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            statsOf(victim).setWerewolf(true);
            EXPECT_TRUE(mManager.actorAttacked(victim, mPlayer));
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, killing_a_peaceful_npc_is_a_murder)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            mManager.actorKilled(victim, mPlayer);
            EXPECT_EQ(bounty(), 1000);
        }

        TEST_F(JusticeTest, killing_by_someone_else_or_without_cause_is_not_the_players_murder)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            MWWorld::Ptr other = makeNpc("other_npc");
            mManager.actorKilled(victim, other);
            mManager.actorKilled(victim, MWWorld::Ptr());
            mManager.actorKilled(victim, victim);
            EXPECT_EQ(bounty(), 0);
        }

        TEST_F(JusticeTest, killing_a_werewolf_is_no_murder_unless_a_crime_against_it_was_recorded)
        {
            MWWorld::Ptr victim = makeVictim("victim_a", 100);
            statsOf(victim).setWerewolf(true);
            mManager.actorKilled(victim, mPlayer);
            EXPECT_EQ(bounty(), 0);
            statsOf(victim).setCrimeId(0);
            mManager.actorKilled(victim, mPlayer);
            EXPECT_EQ(bounty(), 1000);
        }

        // ------------------------------------------------------------------------------------- bound items

        TEST_F(JusticeTest, an_item_that_is_no_bound_item_is_not_recognised_as_one)
        {
            addStringGmst("sMagicBoundDaggerID", "bound_dagger");
            MWWorld::Ptr plain = makeItem("ring_a", 1, "");
            EXPECT_FALSE(mManager.isBoundItem(plain));
        }

        // ------------------------------------------------------------------------------------- the save record

        std::string writeAll(MWMechanics::MechanicsManager& manager)
        {
            auto stream = std::make_unique<std::stringstream>();
            std::stringstream* raw = stream.get();
            ESM::ESMWriter writer;
            writer.setFormatVersion(ESM::DefaultFormatVersion);
            writer.save(*raw);
            Loading::Listener listener;
            manager.write(writer, listener);
            writer.close();
            return raw->str();
        }

        void readAll(const std::string& bytes, MWMechanics::MechanicsManager& manager)
        {
            ESM::ESMReader reader;
            reader.open(std::make_unique<std::istringstream>(bytes), "save");
            while (reader.hasMoreRecs())
            {
                ESM::NAME n = reader.getRecName();
                reader.getRecHeader();
                manager.readRecord(reader, n.toInt());
            }
        }

        class JusticeSaveTest : public JusticeTest
        {
        protected:
            void fill()
            {
                makeVictim("owner_a", 100);
                MWWorld::Ptr item = makeItem("ring_a", 10, "owner_a");
                mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 3, false);
                MWWorld::Ptr second = makeItem("ring_b", 5, "", "guild_a");
                mManager.itemTaken(mPlayer, second, MWWorld::Ptr(), 7, false);
                mManager.itemTaken(mPlayer, item, MWWorld::Ptr(), 1, false);
            }
        };

        TEST_F(JusticeSaveTest, the_stream_has_the_death_count_record_then_the_stolen_items_record)
        {
            fill();
            const std::vector<std::string> names = recordNames(writeAll(mManager));
            ASSERT_EQ(names.size(), 3u); // the file header, DCOU, STLN
            EXPECT_EQ(names[0], "TES3");
            EXPECT_EQ(names[1], "DCOU");
            EXPECT_EQ(names[2], "STLN");
            EXPECT_EQ(mManager.countSavedGameRecords(), 2u);
        }

        TEST_F(JusticeSaveTest, the_stolen_items_record_equals_one_written_from_the_esm_struct)
        {
            fill();
            const std::string bytes = writeAll(mManager);

            ESM::StolenItems items;
            items.mStolenItems[rid("ring_a")][{ rid("owner_a"), false }] = 4;
            items.mStolenItems[rid("ring_b")][{ rid("guild_a"), true }] = 7;
            auto stream = std::make_unique<std::stringstream>();
            std::stringstream* raw = stream.get();
            ESM::ESMWriter writer;
            writer.setFormatVersion(ESM::DefaultFormatVersion);
            writer.save(*raw);
            writer.startRecord(ESM::REC_STLN);
            items.write(writer);
            writer.endRecord(ESM::REC_STLN);
            writer.close();
            const std::string direct = raw->str();
            // the manager's stream has the (empty) death count record between the header and this one
            const std::string record = direct.substr(direct.find("STLN"));
            ASSERT_GE(bytes.size(), record.size());
            EXPECT_EQ(bytes.substr(bytes.size() - record.size()), record);
        }

        TEST_F(JusticeSaveTest, a_small_state_has_the_bytes_the_old_code_wrote)
        {
            fill();
            // Hex of the whole stream, taken from the code before the move (see the stage log of task 0098).
            expectRecording("rules_justice_save.golden.txt", toHex(writeAll(mManager)) + "\n");
        }

        TEST_F(JusticeSaveTest, reading_and_writing_again_gives_the_same_bytes_and_the_same_answers)
        {
            fill();
            const std::string bytes = writeAll(mManager);
            MWMechanics::MechanicsManager other;
            readAll(bytes, other);
            EXPECT_EQ(writeAll(other), bytes);
            const auto owners = other.getStolenItemOwners(rid("ring_a"));
            ASSERT_EQ(owners.size(), 1u);
            EXPECT_EQ(owners[0].second, 4);
            EXPECT_TRUE(other.isItemStolenFrom(rid("ring_a"), mJusticeWorld.mActive[rid("owner_a")]));
        }

        TEST_F(JusticeSaveTest, clear_forgets_the_stolen_items_and_a_load_replaces_them)
        {
            fill();
            const std::string bytes = writeAll(mManager);
            mManager.clear();
            EXPECT_TRUE(mManager.getStolenItemOwners(rid("ring_a")).empty());
            readAll(bytes, mManager);
            EXPECT_EQ(mManager.getStolenItemOwners(rid("ring_a")).size(), 1u);
            readAll(bytes, mManager);
            EXPECT_EQ(mManager.getStolenItemOwners(rid("ring_a"))[0].second, 4);
        }
    }
}
