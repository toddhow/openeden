// Task 0098, move 6, stage 2: characterization tests of the social rules (a merchant's or speaker's derived
// disposition, the barter price and the disposition change of a persuasion attempt). They run the real
// MWMechanics::MechanicsManager's three functions on real NpcStats, so they pass on the old code, where the arithmetic
// lives in mechanicsmanagerimp.cpp, and must stay green and unedited when the arithmetic has moved into the Morrowind
// game pack (the one line that installs the pack's rules in SetUp is the only difference between the two runs; it is
// marked below). Rolls are chosen by seed (seedWithRoll) and the draw count is compared with a sibling generator
// (drawsSince), so nothing depends on how a standard library maps the generator to a roll.
//
// The disposition code caches several GMSTs in function-local statics on its first call, so the GMST values below are
// the same for the whole process and no test may change them.
#include <algorithm>
#include <sstream>

#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadspel.hpp>

#include "apps/openmw/mwbase/dialoguemanager.hpp"
#include "apps/openmw/mwclass/creature.hpp"
#include "apps/openmw/mwmechanics/magiceffects.hpp"
#include "apps/openmw/mwmechanics/mechanicsmanagerimp.hpp"

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

// installSocialRulesForTest(): on the old code the rules are part of the MechanicsManager and the function does
// nothing; with the move it installs the pack's social rules (the only difference between the two runs; the tests below
// are unedited).
#include "socialinstall.hpp"

namespace CraftingTest
{
    namespace
    {
        /// The opinion one faction has of another: a plain function of the two names, so that the tests can predict it.
        class SocialDialogue : public GeneratedDialogueManagerBase
        {
        public:
            int getFactionReaction(const ESM::RefId& faction1, const ESM::RefId& faction2) const override
            {
                ++mCalls;
                if (faction1 == faction2)
                    return 10;
                if (faction2 == ESM::RefId::stringRefId("faction_b"))
                    return -5;
                if (faction2 == ESM::RefId::stringRefId("faction_c"))
                    return 20;
                return 3;
            }
            mutable int mCalls = 0;
        };

        // GMST values: test inputs. The disposition ones are cached by the code on first use.
        constexpr float kRaceMod = 5.f;
        constexpr float kPersonalityMult = 0.5f;
        constexpr float kPersonalityBase = 50.f;
        constexpr float kRankMult = 2.f;
        constexpr float kRankBase = 1.f;
        constexpr float kFactionMod = 0.5f;
        constexpr float kCrimeMod = 0.25f;
        constexpr float kDiseaseMod = -12.f;
        constexpr float kWeaponDrawn = -7.f;

        class SocialTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                addFloatGmst("fDispRaceMod", kRaceMod);
                addFloatGmst("fDispPersonalityMult", kPersonalityMult);
                addFloatGmst("fDispPersonalityBase", kPersonalityBase);
                addFloatGmst("fDispFactionRankMult", kRankMult);
                addFloatGmst("fDispFactionRankBase", kRankBase);
                addFloatGmst("fDispFactionMod", kFactionMod);
                addFloatGmst("fDispCrimeMod", kCrimeMod);
                addFloatGmst("fDispDiseaseMod", kDiseaseMod);
                addFloatGmst("fDispWeaponDrawn", kWeaponDrawn);

                addFloatGmst("fPersonalityMod", 5.f);
                addFloatGmst("fLuckMod", 10.f);
                addFloatGmst("fReputationMod", 0.1f);
                addFloatGmst("fLevelMod", 2.f);
                addFloatGmst("fBribe10Mod", 0.f);
                addFloatGmst("fBribe100Mod", 10.f);
                addFloatGmst("fBribe1000Mod", 25.f);
                addIntGmst("iPerMinChance", 5);
                addIntGmst("iPerMinChange", 10);
                addFloatGmst("fPerDieRollMult", 0.3f);
                addFloatGmst("fPerTempMult", 1.5f);

                for (const char* name : { "faction_a", "faction_b", "faction_c" })
                {
                    ESM::Faction faction;
                    faction.blank();
                    faction.mId = rid(name);
                    faction.mName = name;
                    for (size_t i = 0; i < faction.mRanks.size(); ++i)
                        faction.mRanks[i] = "rank" + std::to_string(i);
                    mStore.insertStatic(faction);
                }

                ESM::Race other;
                other.blank();
                other.mId = rid("otherrace");
                mStore.insertStatic(other);

                ESM::Spell disease;
                disease.blank();
                disease.mId = rid("test_disease");
                disease.mData.mType = ESM::Spell::ST_Disease;
                mDisease = mStore.insertStatic(disease);
                ESM::Spell blight;
                blight.blank();
                blight.mId = rid("test_blight");
                blight.mData.mType = ESM::Spell::ST_Blight;
                mBlight = mStore.insertStatic(blight);

                ESM::Creature creature;
                creature.blank();
                creature.mId = rid("test_creature");
                creature.mName = "creature";
                mCreatureRecord = mStore.insertStatic(creature);
                MWClass::Creature::registerSelf();

                mEnvironment->setDialogueManager(mDialogue);
                installSocialRulesForTest();
            }

            void TearDown() override
            {
                mCreature = MWWorld::Ptr();
                mCreatureRef.reset();
                RulesTest::TearDown();
            }

            /// An NPC of a race and a primary faction, with the player-like stats of the fixture.
            MWWorld::Ptr makeSpeaker(const std::string& id, const char* race, const char* faction)
            {
                const ESM::NPC saved = mNpc;
                mNpc.mRace = rid(race);
                mNpc.mFaction = faction[0] ? rid(faction) : ESM::RefId();
                MWWorld::Ptr ptr = makeNpc(id);
                mNpc = saved;
                return ptr;
            }

            MWMechanics::NpcStats& statsOf(const MWWorld::Ptr& ptr) { return ptr.getClass().getNpcStats(ptr); }

            void setFatigue(const MWWorld::Ptr& ptr, float current)
            {
                MWMechanics::DynamicStat<float> fatigue = statsOf(ptr).getFatigue();
                fatigue.setCurrent(current);
                statsOf(ptr).setFatigue(fatigue);
            }

            MWWorld::Ptr makeCreature()
            {
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefID = mCreatureRecord->mId;
                cellRef.mRefNum = ESM::RefNum{ .mIndex = 500, .mContentFile = 0 };
                mCreatureRef = std::make_unique<MWWorld::LiveCellRef<ESM::Creature>>(cellRef, mCreatureRecord);
                mCreature = MWWorld::Ptr(mCreatureRef.get());
                return mCreature;
            }

            /// Makes the player a member of the faction at  rank (a rank record must exist: all ten have a name).
            void joinAt(const char* faction, int rank)
            {
                stats().joinFaction(rid(faction));
                stats().setFactionRank(rid(faction), rank);
            }

            /// Takes the player out of the faction and clears an expulsion.
            void leave(const char* faction)
            {
                stats().joinFaction(rid(faction));
                stats().setFactionRank(rid(faction), -1);
                stats().clearExpelled(rid(faction));
            }

            int disposition(const MWWorld::Ptr& npc, bool clamp = true)
            {
                return mManager.getDerivedDisposition(npc, clamp);
            }

            SocialDialogue mDialogue;
            MWMechanics::MechanicsManager mManager;
            const ESM::Spell* mDisease = nullptr;
            const ESM::Spell* mBlight = nullptr;
            const ESM::Creature* mCreatureRecord = nullptr;
            std::unique_ptr<MWWorld::LiveCellRef<ESM::Creature>> mCreatureRef;
            MWWorld::Ptr mCreature;
        };

        // -------------------------------------------------------------------------------------------- disposition

        // Base 50, same race (+5), personality 50 (0), no faction, nothing else: 55.
        TEST_F(SocialTest, derived_disposition_adds_the_race_bonus_to_the_base)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "testrace", "");
            statsOf(npc).setBaseDisposition(50);
            EXPECT_EQ(disposition(npc), 55);

            MWWorld::Ptr stranger = makeSpeaker("npc_b", "otherrace", "");
            statsOf(stranger).setBaseDisposition(50);
            EXPECT_EQ(disposition(stranger), 50);
        }

        TEST_F(SocialTest, derived_disposition_follows_the_players_personality_and_clamps)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(50);
            stats().setAttribute(ESM::Attribute::Personality, 90.f);
            EXPECT_EQ(disposition(npc), 70); // + 0.5 * (90 - 50)
            statsOf(npc).setBaseDisposition(95);
            EXPECT_EQ(disposition(npc), 100);
            EXPECT_EQ(disposition(npc, false), 115);
            statsOf(npc).setBaseDisposition(-90);
            EXPECT_EQ(disposition(npc), 0);
            EXPECT_EQ(disposition(npc, false), -70);
        }

        TEST_F(SocialTest, derived_disposition_uses_the_crime_modifier_bounty_diseases_weapon_and_charm)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(60);
            statsOf(npc).setCrimeDispositionModifier(-8);
            stats().setBounty(40); // -0.25 * 40
            EXPECT_EQ(disposition(npc, false), 42);
            stats().getSpells().add(mDisease, false);
            EXPECT_EQ(disposition(npc, false), 30);
            stats().getSpells().add(mBlight, false); // still one fixed bonus, not two
            EXPECT_EQ(disposition(npc, false), 30);
            stats().setDrawState(MWMechanics::DrawState::Weapon);
            EXPECT_EQ(disposition(npc, false), 23);
            npc.getClass().getCreatureStats(npc).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Charm), MWMechanics::EffectParam(15.f));
            EXPECT_EQ(disposition(npc, false), 38);
        }

        // The faction code: the faction of the NPC against the factions the player belongs to.
        TEST_F(SocialTest, derived_disposition_uses_the_factions_reaction_and_the_players_rank)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "faction_a");
            statsOf(npc).setBaseDisposition(50);

            // player not in any faction: the NPC's faction is not empty, so the reaction loop finds nothing: 0.
            EXPECT_EQ(disposition(npc, false), 50);

            // member of the NPC's faction at rank 3: (2 * 3 + 1) * 0.5 * 10 (a faction's opinion of itself)
            joinAt("faction_a", 3);
            EXPECT_EQ(disposition(npc, false), 50 + 35);

            // expelled from it: reaction and rank stay 0
            stats().expell(rid("faction_a"), false);
            EXPECT_EQ(disposition(npc, false), 50);
        }

        TEST_F(SocialTest, derived_disposition_picks_the_lowest_reaction_of_the_players_other_factions)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "faction_a");
            statsOf(npc).setBaseDisposition(50);
            joinAt("faction_b", 2); // reaction -5
            joinAt("faction_c", 5); // reaction 20
            // lowest reaction is faction_b's: (2 * 2 + 1) * 0.5 * -5 = -12.5, 37.5 truncated
            EXPECT_EQ(disposition(npc, false), 37);

            // an expelled faction is ignored. Quirk of the existing code, pinned as it is: the first non-expelled
            // faction counts only if it is the FIRST of the map (`playerFactionIt == begin()`), so with b expelled
            // faction_c (reaction 20, not lower than the initial 0) leaves the reaction at 0.
            stats().expell(rid("faction_b"), false);
            EXPECT_EQ(disposition(npc, false), 50);
        }

        // A recording over a grid of inputs.
        TEST_F(SocialTest, disposition_grid_matches_the_recording)
        {
            std::ostringstream out;
            const char* const races[] = { "testrace", "otherrace" };
            const char* const factions[] = { "", "faction_a", "faction_b" };
            for (const char* race : races)
                for (const char* faction : factions)
                    for (int base : { 0, 35, 80 })
                        for (int crime : { 0, -15 })
                            for (float personality : { 20.f, 50.f, 85.f })
                                for (int playerFaction = 0; playerFaction < 5; ++playerFaction)
                                    for (int bounty : { 0, 100 })
                                        for (int extras = 0; extras < 4; ++extras)
                                        {
                                            MWWorld::Ptr npc = makeSpeaker(
                                                "grid_" + std::to_string(mOtherNpcs.size()), race, faction);
                                            statsOf(npc).setBaseDisposition(base);
                                            statsOf(npc).setCrimeDispositionModifier(crime);
                                            stats().setAttribute(ESM::Attribute::Personality, personality);
                                            stats().setBounty(bounty);
                                            // the player's factions: none, a, b, a and c, b and c with b expelled
                                            for (const char* f : { "faction_a", "faction_b", "faction_c" })
                                                leave(f);
                                            switch (playerFaction)
                                            {
                                                case 1:
                                                    joinAt("faction_a", 4);
                                                    break;
                                                case 2:
                                                    joinAt("faction_b", 1);
                                                    break;
                                                case 3:
                                                    joinAt("faction_a", 0);
                                                    joinAt("faction_c", 6);
                                                    break;
                                                case 4:
                                                    joinAt("faction_b", 2);
                                                    joinAt("faction_c", 3);
                                                    stats().expell(rid("faction_b"), false);
                                                    break;
                                                default:
                                                    break;
                                            }
                                            stats().setDrawState((extras & 1) ? MWMechanics::DrawState::Weapon
                                                                              : MWMechanics::DrawState::Nothing);
                                            out << race << ' ' << (faction[0] ? faction : "-") << " base " << base
                                                << " crime " << crime << " pers " << personality << " pf "
                                                << playerFaction << " bounty " << bounty << " extras " << extras
                                                << " -> " << disposition(npc, false) << ' ' << disposition(npc, true)
                                                << '\n';
                                        }
            expectRecording("rules_social_disposition.golden.txt", out.str());
        }

        // ------------------------------------------------------------------------------------------------- barter

        TEST_F(SocialTest, barter_leaves_a_zero_price_and_a_creature_merchants_price_alone)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "testrace", "");
            EXPECT_EQ(mManager.getBarterOffer(npc, 0, true), 0);
            EXPECT_EQ(mManager.getBarterOffer(npc, 0, false), 0);
            MWWorld::Ptr creature = makeCreature();
            EXPECT_EQ(mManager.getBarterOffer(creature, 777, true), 777);
            EXPECT_EQ(mManager.getBarterOffer(creature, 777, false), 777);
        }

        TEST_F(SocialTest, barter_buying_costs_more_than_selling_and_never_less_than_one)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(20);
            const int buy = mManager.getBarterOffer(npc, 1000, true);
            const int sell = mManager.getBarterOffer(npc, 1000, false);
            EXPECT_GT(buy, 1000);
            EXPECT_LT(sell, 1000);
            // an item worth 1 sold to a hostile merchant still fetches 1
            statsOf(npc).setBaseDisposition(0);
            EXPECT_EQ(mManager.getBarterOffer(npc, 1, false), 1);
        }

        // Everything equal (skills 30, luck 50, personality 50, fatigue term 1.25), disposition 50 (race differs):
        // pcTerm = npcTerm = 56.25, so buyTerm = 1 and sellTerm = 0.5.
        TEST_F(SocialTest, barter_with_equal_traders_has_the_known_terms)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(50);
            EXPECT_EQ(mManager.getBarterOffer(npc, 100, true), 100 + 0); // pcTerm is (50-50+45)*1.25 = 56.25
            EXPECT_EQ(mManager.getBarterOffer(npc, 100, false), 50);
        }

        TEST_F(SocialTest, barter_grid_matches_the_recording)
        {
            std::ostringstream out;
            for (int basePrice : { 1, 7, 100, 1234 })
                for (bool buying : { true, false })
                    for (int base : { 10, 50, 90 })
                        for (float mercantile : { 10.f, 60.f, 120.f })
                            for (float sellerMercantile : { 20.f, 70.f, 140.f })
                                for (float luck : { 30.f, 100.f })
                                    for (float fatigue : { 20.f, 200.f })
                                    {
                                        MWWorld::Ptr npc = makeSpeaker(
                                            "barter_" + std::to_string(mOtherNpcs.size()), "otherrace", "");
                                        statsOf(npc).setBaseDisposition(base);
                                        statsOf(npc).getSkill(ESM::Skill::Mercantile).setBase(sellerMercantile);
                                        statsOf(npc).setAttribute(ESM::Attribute::Luck, luck + 10.f);
                                        statsOf(npc).setAttribute(ESM::Attribute::Personality, 70.f);
                                        setFatigue(npc, 200.f);
                                        stats().getSkill(ESM::Skill::Mercantile).setBase(mercantile);
                                        stats().setAttribute(ESM::Attribute::Luck, luck);
                                        stats().setAttribute(ESM::Attribute::Personality, 40.f + luck / 10.f);
                                        setFatigue(mPlayer, fatigue);
                                        out << basePrice << (buying ? " buy " : " sell ") << base << ' ' << mercantile
                                            << ' ' << sellerMercantile << ' ' << luck << ' ' << fatigue << " -> "
                                            << mManager.getBarterOffer(npc, basePrice, buying) << '\n';
                                    }
            expectRecording("rules_social_barter.golden.txt", out.str());
        }

        // ------------------------------------------------------------------------------------------- persuasion

        struct Persuasion
        {
            bool mSuccess = false;
            int mTemp = 0;
            int mPerm = 0;
            int mFlee = 0;
            int mFight = 0;
            int mDraws = 0;
        };

        class SocialPersuasionTest : public SocialTest
        {
        protected:
            Persuasion persuade(const MWWorld::Ptr& npc, MWBase::MechanicsManager::PersuasionType type, int roll)
            {
                const unsigned seed = seedWithRoll(roll);
                seedWorld(seed);
                Persuasion result;
                // poison the outputs so that a function that does not write them is noticed
                result.mTemp = -12345;
                result.mPerm = -12345;
                mManager.getPersuasionDispositionChange(npc, type, result.mSuccess, result.mTemp, result.mPerm);
                result.mFlee = statsOf(npc).getAiSetting(MWMechanics::AiSetting::Flee).getBase();
                result.mFight = statsOf(npc).getAiSetting(MWMechanics::AiSetting::Fight).getBase();
                result.mDraws = drawsSince(seed);
                return result;
            }
        };

        TEST_F(SocialPersuasionTest, every_attempt_draws_exactly_one_number)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(50);
            using PT = MWBase::MechanicsManager;
            for (auto type :
                { PT::PT_Admire, PT::PT_Intimidate, PT::PT_Taunt, PT::PT_Bribe10, PT::PT_Bribe100, PT::PT_Bribe1000 })
                EXPECT_EQ(persuade(npc, type, 30).mDraws, 1) << int(type);
        }

        // The player and the NPC have equal ratings, so target1 and target2 are d * 50 and the roll decides. With
        // disposition 55 (50 + race none... otherrace: 50) d = 1.
        TEST_F(SocialPersuasionTest, admire_succeeds_up_to_the_target_and_fails_above)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(50);
            // equal ratings except the level term and reputation: both zero here, so target1 = 50
            const Persuasion atTarget = persuade(npc, MWBase::MechanicsManager::PT_Admire, 50);
            const Persuasion above = persuade(npc, MWBase::MechanicsManager::PT_Admire, 51);
            EXPECT_TRUE(atTarget.mSuccess);
            EXPECT_FALSE(above.mSuccess);
            EXPECT_GE(atTarget.mTemp, 0);
            EXPECT_LE(above.mTemp, 0);
        }

        TEST_F(SocialPersuasionTest, a_successful_intimidation_makes_the_npc_flee_and_a_taunt_makes_it_fight)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(50);
            statsOf(npc).setAiSetting(MWMechanics::AiSetting::Flee, 20);
            statsOf(npc).setAiSetting(MWMechanics::AiSetting::Fight, 60);
            const Persuasion intimidate = persuade(npc, MWBase::MechanicsManager::PT_Intimidate, 5);
            EXPECT_TRUE(intimidate.mSuccess);
            EXPECT_GT(intimidate.mFlee, 20);
            EXPECT_LT(intimidate.mFight, 60);

            statsOf(npc).setAiSetting(MWMechanics::AiSetting::Flee, 60);
            statsOf(npc).setAiSetting(MWMechanics::AiSetting::Fight, 20);
            const Persuasion taunt = persuade(npc, MWBase::MechanicsManager::PT_Taunt, 5);
            EXPECT_TRUE(taunt.mSuccess);
            EXPECT_LT(taunt.mFlee, 60);
            EXPECT_GT(taunt.mFight, 20);

            // a failed intimidation changes neither
            statsOf(npc).setAiSetting(MWMechanics::AiSetting::Flee, 20);
            statsOf(npc).setAiSetting(MWMechanics::AiSetting::Fight, 60);
            const Persuasion failed = persuade(npc, MWBase::MechanicsManager::PT_Intimidate, 99);
            EXPECT_FALSE(failed.mSuccess);
            EXPECT_EQ(failed.mFlee, 20);
            EXPECT_EQ(failed.mFight, 60);
        }

        TEST_F(SocialPersuasionTest, the_disposition_change_never_leaves_the_range_0_to_100)
        {
            MWWorld::Ptr npc = makeSpeaker("npc_a", "otherrace", "");
            statsOf(npc).setBaseDisposition(98);
            const Persuasion up = persuade(npc, MWBase::MechanicsManager::PT_Admire, 0);
            EXPECT_LE(up.mTemp, 2);
            statsOf(npc).setBaseDisposition(2);
            const Persuasion down = persuade(npc, MWBase::MechanicsManager::PT_Taunt, 0);
            EXPECT_GE(down.mTemp, -2);
        }

        TEST_F(SocialPersuasionTest, persuasion_grid_matches_the_recording)
        {
            std::ostringstream out;
            using PT = MWBase::MechanicsManager;
            const char* const names[] = { "admire", "intimidate", "taunt", "bribe10", "bribe100", "bribe1000" };
            int typeIndex = 0;
            for (auto type :
                { PT::PT_Admire, PT::PT_Intimidate, PT::PT_Taunt, PT::PT_Bribe10, PT::PT_Bribe100, PT::PT_Bribe1000 })
            {
                for (int base : { 5, 50, 95 })
                    for (float speech : { 10.f, 40.f, 90.f })
                        for (int reputation : { 0, 30 })
                            for (int roll : { 0, 1, 4, 5, 20, 35, 49, 50, 51, 70, 98, 99 })
                            {
                                MWWorld::Ptr npc
                                    = makeSpeaker("persuade_" + std::to_string(mOtherNpcs.size()), "otherrace", "");
                                statsOf(npc).setBaseDisposition(base);
                                statsOf(npc).setAiSetting(MWMechanics::AiSetting::Flee, 30);
                                statsOf(npc).setAiSetting(MWMechanics::AiSetting::Fight, 70);
                                statsOf(npc).getSkill(ESM::Skill::Speechcraft).setBase(25.f);
                                stats().getSkill(ESM::Skill::Speechcraft).setBase(speech);
                                stats().getSkill(ESM::Skill::Mercantile).setBase(speech + 5.f);
                                stats().setReputation(reputation);
                                stats().setLevel(1 + reputation / 10);
                                const Persuasion r = persuade(npc, type, roll);
                                out << names[typeIndex] << " base " << base << " speech " << speech << " rep "
                                    << reputation << " roll " << roll << " -> " << (r.mSuccess ? "ok" : "no") << ' '
                                    << r.mTemp << ' ' << r.mPerm << " flee " << r.mFlee << " fight " << r.mFight
                                    << " draws " << r.mDraws << '\n';
                            }
                ++typeIndex;
            }
            expectRecording("rules_social_persuasion.golden.txt", out.str());
        }
    }
}
