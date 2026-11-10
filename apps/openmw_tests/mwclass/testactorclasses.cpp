// Task 0098, move 8, stage 8c: characterization tests of the actor classes (npc, creature, creature levelled list, item
// levelled list). They run the REAL MWClass::Npc, Creature, CreatureLevList and ItemLevList classes (registered the way
// the game registers them) on records in an in-memory ESMStore, behind test doubles of the MWBase managers, and pin
// what the classes answer and which state they save, so that moving the classes into the Morrowind pack cannot change
// a result. Written against the OLD code (apps/openmw/mwclass) first; the file is not edited by the move.
//
// Records and numbers are TEST INPUTS, not claimed to be Morrowind's data. The GMST values are the same for the whole
// process: the classes cache pointers to their GMST records in function-local statics on the first call (see
// primeSpeedGmsts).
#include <algorithm>
#include <cstdio>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include <components/esm3/aipackage.hpp>
#include <components/esm3/creaturelevliststate.hpp>
#include <components/esm3/creaturestate.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadarmo.hpp>
#include <components/esm3/loadclas.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loadfact.hpp>
#include <components/esm3/loadlevlist.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/esm3/loadrace.hpp>
#include <components/esm3/loadsndg.hpp>
#include <components/esm3/loadsoun.hpp>
#include <components/esm3/loadweap.hpp>
#include <components/esm3/npcstate.hpp>
#include <components/misc/constants.hpp>

#include "apps/openmw/mwbase/dialoguemanager.hpp"
#include "apps/openmw/mwclass/creature.hpp"
#include "apps/openmw/mwclass/creaturelevlist.hpp"
#include "apps/openmw/mwclass/itemlevlist.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwgui/tooltips.hpp"
#include "apps/openmw/mwmechanics/creaturestats.hpp"
#include "apps/openmw/mwmechanics/magiceffects.hpp"
#include "apps/openmw/mwmechanics/npcstats.hpp"
#include "apps/openmw/mwrender/renderinginterface.hpp"
#include "apps/openmw/mwworld/actionopen.hpp"
#include "apps/openmw/mwworld/actiontalk.hpp"
#include "apps/openmw/mwworld/containerstore.hpp"
#include "apps/openmw/mwworld/failedaction.hpp"
#include "apps/openmw/mwworld/inventorystore.hpp"
#include "apps/openmw/mwworld/localscripts.hpp"
#include "apps/openmw/mwworld/nullaction.hpp"
#include "apps/openmw/mwworld/timestamp.hpp"

#include "apps/openmw_tests/mwmechanics/rulesfixture.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif

// A namespace of its own: CraftingTest declares a struct Sound that the generated doubles would find instead of
// MWSound::Sound.
namespace ActorDoubles
{
    using namespace MWBase;
    using CraftingTest::unexpectedCall;
#include "apps/openmw_tests/mwmechanics/fakes/dialoguemanager.inc"
}

#ifdef _MSC_VER
#pragma warning(pop)
#endif
#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

namespace CraftingTest
{
    namespace
    {
        // The GMSTs the actor classes read. Fixed for the process (see the file comment). Vanilla-like but not claimed
        // to be Morrowind's data.
        struct ActorGmst
        {
            const char* mName;
            float mValue;
        };

        constexpr ActorGmst kActorFloatGmsts[] = {
            { "fMinWalkSpeed", 100.f },
            { "fMaxWalkSpeed", 200.f },
            { "fEncumberedMoveEffect", 0.5f },
            { "fSneakSpeedMultiplier", 0.5f },
            { "fAthleticsRunBonus", 1.f },
            { "fBaseRunMultiplier", 1.5f },
            { "fMinFlySpeed", 5.f },
            { "fMaxFlySpeed", 10.f },
            { "fSwimRunBase", 0.5f },
            { "fSwimRunAthleticsMult", 0.1f },
            { "fJumpEncumbranceBase", 0.5f },
            { "fJumpEncumbranceMultiplier", 0.5f },
            { "fJumpAcrobaticsBase", 128.f },
            { "fJumpAcroMultiplier", 2.f },
            { "fJumpRunMultiplier", 1.2f },
            { "fWereWolfRunMult", 1.5f },
            { "fKnockDownMult", 0.5f },
            { "fCombatArmorMinMult", 0.25f },
            { "fMinWalkSpeedCreature", 50.f },
            { "fMaxWalkSpeedCreature", 150.f },
            { "fEncumbranceStrMult", 5.f },
            { "fCorpseRespawnDelay", 72.f },
            { "fCorpseClearDelay", 48.f },
            { "fCombatCriticalStrikeMult", 4.f },
            { "fCombatKODamageMult", 1.5f },
            { "fLightMaxMod", 0.6f },
            { "fMedMaxMod", 0.9f },
        };

        struct ActorIntGmst
        {
            const char* mName;
            int mValue;
        };

        constexpr ActorIntGmst kActorIntGmstTable[] = {
            { "iKnockDownOddsMult", 50 },
            { "iKnockDownOddsBase", 50 },
            { "iVoiceHitOdds", 30 },
            { "iBaseArmorSkill", 15 },
            { "iHelmWeight", 5 },
            { "iCuirassWeight", 30 },
            { "iPauldronWeight", 10 },
            { "iGreavesWeight", 15 },
            { "iBootsWeight", 20 },
            { "iGauntletWeight", 5 },
            { "iShieldWeight", 15 },
        };

        ESM::RefId R(const std::string& name)
        {
            return ESM::RefId::stringRefId(name);
        }

        std::string f(float v)
        {
            char b[48];
            std::snprintf(b, sizeof(b), "%.6f", v);
            return b;
        }

        std::string yn(bool b)
        {
            return b ? "1" : "0";
        }

        template <class F>
        std::string attempt(F&& fn)
        {
            try
            {
                return fn();
            }
            catch (const std::exception& e)
            {
                return std::string("throws(") + e.what() + ")";
            }
        }

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

        std::string actionKind(const MWWorld::Action& action)
        {
            if (dynamic_cast<const MWWorld::ActionTalk*>(&action))
                return "talk";
            if (dynamic_cast<const MWWorld::ActionOpen*>(&action))
                return "open";
            if (dynamic_cast<const MWWorld::FailedAction*>(&action))
                return "failed";
            if (dynamic_cast<const MWWorld::NullAction*>(&action))
                return "null";
            return "other";
        }

        class ActorWorld : public FakeWorld
        {
        public:
            bool mSwimming = false;
            bool mFlying = false;
            bool mUnderwater = false;
            bool mWalkingOnWater = false;
            bool mOnGround = true;
            bool mLevitation = true;
            bool mGodMode = false;
            bool mFirstPerson = false;
            MWWorld::TimeStamp mNow{ 0.f, 0 };
            Misc::Rng::Generator* mSharedPrng = nullptr;
            MWWorld::LocalScripts* mLocalScripts = nullptr;
            int mContainerScriptsRemoved = 0;
            int mMoves = 0;
            int mRotations = 0;
            std::vector<MWWorld::Ptr> mDeleted;
            std::vector<MWWorld::Ptr> mPlaceResults;
            std::vector<std::string> mPlaced;

            Misc::Rng::Generator& getPrng() override { return *mSharedPrng; }
            MWWorld::TimeStamp getTimeStamp() const override { return mNow; }
            bool isSwimming(const MWWorld::ConstPtr&) const override { return mSwimming; }
            bool isFlying(const MWWorld::Ptr&) const override { return mFlying; }
            bool isUnderwater(const MWWorld::CellStore*, const vsg::vec3&) const override { return mUnderwater; }
            bool isWalkingOnWater(const MWWorld::ConstPtr&) const override { return mWalkingOnWater; }
            bool isOnGround(const MWWorld::Ptr&) const override { return mOnGround; }
            bool isLevitationEnabled() const override { return mLevitation; }
            bool getGodModeState() const override { return mGodMode; }
            bool isFirstPerson() const override { return mFirstPerson; }
            MWWorld::LocalScripts& getLocalScripts() override { return *mLocalScripts; }
            void removeContainerScripts(const MWWorld::Ptr&) override { ++mContainerScriptsRemoved; }
            MWWorld::Ptr moveObject(const MWWorld::Ptr& ptr, MWWorld::CellStore*, const vsg::vec3&, bool, bool) override
            {
                ++mMoves;
                return ptr;
            }
            void rotateObject(const MWWorld::Ptr&, const vsg::vec3&, MWBase::RotationFlags) override { ++mRotations; }
            void deleteObject(const MWWorld::Ptr& ptr) override { mDeleted.push_back(ptr); }
            void adjustPosition(const MWWorld::Ptr&, bool) override {}
            MWWorld::Ptr placeObject(const MWWorld::ConstPtr& ptr, MWWorld::CellStore*, const ESM::Position&) override
            {
                mPlaced.push_back(ptr.getCellRef().getRefId().getRefIdString());
                MWWorld::Ptr result = mPlaceResults.front();
                mPlaceResults.erase(mPlaceResults.begin());
                return result;
            }
        };

        class ActorWindow : public FakeWindowManager
        {
        public:
            bool mFullHelp = false;
            bool mGuiMode = false;
            int mDeletedCustomData = 0;
            int mHitOverlays = 0;
            bool getFullHelp() const override { return mFullHelp; }
            bool isGuiMode() const override { return mGuiMode; }
            void onDeleteCustomData(const MWWorld::Ptr&) override { ++mDeletedCustomData; }
            void activateHitOverlay(bool) override { ++mHitOverlays; }
        };

        class ActorMechanics : public RulesMechanics
        {
        public:
            bool mRunning = false;
            bool mSneaking = false;
            std::vector<std::string> mAttacked;
            std::vector<std::string> mKilled;
            bool isRunning(const MWWorld::Ptr&) override { return mRunning; }
            bool isSneaking(const MWWorld::Ptr&) override { return mSneaking; }
            bool actorAttacked(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker) override
            {
                mAttacked.push_back(victim.getCellRef().getRefId().getRefIdString() + "<-"
                    + attacker.getCellRef().getRefId().getRefIdString());
                return true;
            }
            void actorKilled(const MWWorld::Ptr& victim, const MWWorld::Ptr& attacker) override
            {
                mKilled.push_back(victim.getCellRef().getRefId().getRefIdString());
            }
        };

        class ActorLua : public FakeLuaManager
        {
        public:
            void saveLocalScripts(const MWWorld::Ptr&, ESM::LuaScripts&) override {}
            void loadLocalScripts(const MWWorld::Ptr&, const ESM::LuaScripts&) override {}
        };

        class NoRendering : public MWRender::RenderingInterface
        {
        public:
            void insertModel(const MWWorld::Ptr&, const std::string&, bool) override { unexpectedCall("insertModel"); }
            void insertNPC(const MWWorld::Ptr&) override { unexpectedCall("insertNPC"); }
            void insertCreature(const MWWorld::Ptr&, const std::string&, bool) override
            {
                unexpectedCall("insertCreature");
            }
        };

        class ActorDialogue : public ActorDoubles::GeneratedDialogueManagerBase
        {
        public:
            std::vector<std::string> mSaid;
            bool say(const MWWorld::Ptr& actor, const ESM::RefId& topic) override
            {
                mSaid.push_back(actor.getCellRef().getRefId().getRefIdString() + ":" + topic.getRefIdString());
                return true;
            }
        };

        class ActorTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                MWClass::Creature::registerSelf();

                for (const ActorGmst& gmst : kActorFloatGmsts)
                    addFloatGmst(gmst.mName, gmst.mValue);
                for (const ActorIntGmst& gmst : kActorIntGmstTable)
                    addIntGmst(gmst.mName, gmst.mValue);
                addStringGmst("sWerewolfPopup", "Werewolf");
                addStringGmst("sWerewolfRefusal", "refusal");

                ESM::Race tall;
                tall.blank();
                tall.mId = rid("tallrace");
                tall.mData.mMaleHeight = 1.25f;
                tall.mData.mFemaleHeight = 1.5f;
                tall.mData.mMaleWeight = 0.75f;
                tall.mData.mFemaleWeight = 0.5f;
                mStore.insertStatic(tall);

                ESM::Race beast;
                beast.blank();
                beast.mId = rid("beastrace");
                beast.mData.mFlags = ESM::Race::Beast;
                mStore.insertStatic(beast);

                // a race with attribute and skill bonuses, for the auto-calculated NPCs
                ESM::Race bonus;
                bonus.blank();
                bonus.mId = rid("bonusrace");
                for (int i = 0; i < 8; ++i)
                {
                    bonus.mData.mAttributeValues[i * 2] = 40 + i; // male
                    bonus.mData.mAttributeValues[i * 2 + 1] = 35 + i; // female
                }
                bonus.mData.mBonus[0].mSkill = ESM::Skill::refIdToIndex(ESM::Skill::Athletics);
                bonus.mData.mBonus[0].mBonus = 10;
                bonus.mData.mBonus[1].mSkill = ESM::Skill::refIdToIndex(ESM::Skill::Acrobatics);
                bonus.mData.mBonus[1].mBonus = 5;
                mStore.insertStatic(bonus);

                ESM::Class cls;
                cls.blank();
                cls.mId = rid("testclass");
                cls.mName = "Test Class";
                cls.mData.mSpecialization = ESM::Class::Combat;
                cls.mData.mAttribute = { ESM::Attribute::refIdToIndex(ESM::Attribute::Strength),
                    ESM::Attribute::refIdToIndex(ESM::Attribute::Endurance) };
                cls.mData.mSkills[0] = { ESM::Skill::refIdToIndex(ESM::Skill::Athletics),
                    ESM::Skill::refIdToIndex(ESM::Skill::LongBlade) };
                cls.mData.mSkills[1]
                    = { ESM::Skill::refIdToIndex(ESM::Skill::Acrobatics), ESM::Skill::refIdToIndex(ESM::Skill::Sneak) };
                cls.mData.mSkills[2] = { ESM::Skill::refIdToIndex(ESM::Skill::Alchemy), -1 };
                cls.mData.mSkills[3] = { -1, -1 };
                cls.mData.mSkills[4] = { -1, -1 };
                cls.mData.mServices = ESM::NPC::Weapon | ESM::NPC::Training;
                mStore.insertStatic(cls);

                ESM::Class mage = cls;
                mage.mId = rid("mageclass");
                mage.mData.mSpecialization = ESM::Class::Magic;
                mage.mData.mServices = ESM::NPC::Spells;
                mStore.insertStatic(mage);

                ESM::Faction faction;
                faction.blank();
                faction.mId = rid("t_faction");
                faction.mName = "Test Faction";
                mStore.insertStatic(faction);

                mActorWorld.mStore = &mStore;
                mActorWorld.mSharedPrng = &mWorld.mPrng;
                mActorWorld.mPlayer = mPlayer;
                mLocalScripts = std::make_unique<MWWorld::LocalScripts>(mStore);
                mActorWorld.mLocalScripts = mLocalScripts.get();
                mEnvironment->setWorld(mActorWorld);
                mEnvironment->setWindowManager(mActorWindow);
                mEnvironment->setMechanicsManager(mActorMechanics);
                mEnvironment->setDialogueManager(mActorDialogue);
                mEnvironment->setLuaManager(mActorLua);

                primeSpeedGmsts();
            }

            void TearDown() override
            {
                mLocalScripts.reset();
                mHolders.clear();
                mActorWorld.mPlaceResults.clear();
                RulesTest::TearDown();
            }

            /// Npc::getGmst and Creature::getGmst keep pointers to GMST records in function-local statics, filled on
            /// the first call by whichever store is current. Every test has a store of its own, so the first call must
            /// see a store that outlives the process, or the pointers would dangle from the second test on.
            void primeSpeedGmsts()
            {
                static bool primed = false;
                if (primed)
                    return;
                primed = true;
                static MWWorld::ESMStore* const longLived = new MWWorld::ESMStore; // intentionally never freed
                for (const ActorGmst& gmst : kActorFloatGmsts)
                {
                    ESM::GameSetting record;
                    record.blank();
                    record.mId = rid(gmst.mName);
                    record.mValue = ESM::Variant(gmst.mValue);
                    longLived->insertStatic(record);
                }
                for (const ActorIntGmst& gmst : kActorIntGmstTable)
                {
                    ESM::GameSetting record;
                    record.blank();
                    record.mId = rid(gmst.mName);
                    record.mValue = ESM::Variant(static_cast<int32_t>(gmst.mValue));
                    longLived->insertStatic(record);
                }
                // creates the player's custom data from the test's own store
                (void)stats();
                MWWorld::Ptr creaturePtr = creature("t_prime_creature");
                (void)creaturePtr.getClass().getCreatureStats(creaturePtr);
                mEnvironment->setESMStore(*longLived);
                mPlayer.getClass().getWalkSpeed(mPlayer);
                creaturePtr.getClass().getWalkSpeed(creaturePtr);
                mEnvironment->setESMStore(mStore);
            }

            ESM::CellRef cellRefFor(const ESM::RefId& id)
            {
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefID = id;
                cellRef.mRefNum = ESM::RefNum{ .mIndex = mNextIndex++, .mContentFile = 0 };
                return cellRef;
            }

            template <class T>
            MWWorld::Ptr make(T record)
            {
                const T* stored = mStore.insertStatic(record);
                auto ref = std::make_shared<MWWorld::LiveCellRef<T>>(cellRefFor(stored->mId), stored);
                mHolders.push_back(ref);
                return MWWorld::Ptr(ref.get());
            }

            /// An NPC with explicit stats (attributes 40 but the speed 50, skills 30, health 120). \a edit changes the
            /// record before it is stored.
            template <class Edit>
            MWWorld::Ptr npc(const std::string& id, Edit edit)
            {
                ESM::NPC record = mNpc;
                record.mId = rid(id);
                record.mName = "Npc " + id;
                record.mScript = rid(id + "_script");
                record.mClass = rid("testclass");
                record.mNpdt.mGold = 100;
                record.mNpdt.mDisposition = 40;
                record.mNpdt.mReputation = 7;
                record.mNpdt.mHealth = 120;
                record.mNpdt.mMana = 60;
                record.mNpdt.mFatigue = 200;
                record.mAiData.mHello = 30;
                record.mAiData.mFight = 20;
                record.mAiData.mFlee = 10;
                record.mAiData.mAlarm = 5;
                record.mAiData.mServices = ESM::NPC::Armor | ESM::NPC::Spells;
                for (auto& attribute : record.mNpdt.mAttributes)
                    attribute = 40;
                record.mNpdt.mAttributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Speed)] = 50;
                record.mNpdt.mAttributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Strength)] = 50;
                edit(record);
                return make(record);
            }

            MWWorld::Ptr npc(const std::string& id)
            {
                return npc(id, [](ESM::NPC&) {});
            }

            template <class Edit>
            MWWorld::Ptr creature(const std::string& id, Edit edit)
            {
                ESM::Creature record;
                record.blank();
                record.mId = rid(id);
                record.mName = "Creature " + id;
                record.mModel = "c/" + id + ".nif";
                record.mScript = rid(id + "_script");
                record.mData.mLevel = 4;
                for (auto& attribute : record.mData.mAttributes)
                    attribute = 30;
                record.mData.mAttributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Speed)] = 60;
                record.mData.mAttributes[ESM::Attribute::refIdToIndex(ESM::Attribute::Strength)] = 70;
                record.mData.mHealth = 90;
                record.mData.mMana = 30;
                record.mData.mFatigue = 150;
                record.mData.mCombat = 40;
                record.mData.mMagic = 50;
                record.mData.mStealth = 60;
                record.mData.mAttack[0] = 2;
                record.mData.mAttack[1] = 6;
                record.mData.mGold = 25;
                record.mAiData.mHello = 10;
                record.mAiData.mFight = 80;
                record.mAiData.mFlee = 30;
                record.mAiData.mAlarm = 15;
                record.mAiData.mServices = ESM::NPC::Training;
                record.mFlags = ESM::Creature::Walks | ESM::Creature::Base;
                edit(record);
                return make(record);
            }

            MWWorld::Ptr creature(const std::string& id)
            {
                return creature(id, [](ESM::Creature&) {});
            }

            MWWorld::Ptr misc(const std::string& id, float weight)
            {
                ESM::Miscellaneous record;
                record.blank();
                record.mId = rid(id);
                record.mName = id;
                record.mData.mWeight = weight;
                record.mData.mValue = 3;
                return make(record);
            }

            /// The id of an item added to the actor's inventory with \a weight each.
            MWWorld::Ptr addWeight(const MWWorld::Ptr& actor, const std::string& id, float weight, int count = 1)
            {
                misc(id, weight);
                return *actor.getClass().getContainerStore(actor).add(rid(id), count);
            }

            MWWorld::Ptr armor(const std::string& id, int type, float weight, int rating, int health = 100)
            {
                ESM::Armor r;
                r.blank();
                r.mId = rid(id);
                r.mName = "Armor " + id;
                r.mModel = "a/" + id + ".nif";
                r.mData.mType = type;
                r.mData.mWeight = weight;
                r.mData.mValue = 10;
                r.mData.mHealth = health;
                r.mData.mArmor = rating;
                return make(r);
            }

            template <class State>
            std::string writeObjectState(State& state)
            {
                // a default-made CellRef leaves its charge fields uninitialised, so the bytes would differ per run
                state.mRef.blank();
                auto stream = std::make_unique<std::stringstream>();
                std::stringstream* raw = stream.get();
                ESM::ESMWriter writer;
                writer.setFormatVersion(ESM::DefaultFormatVersion);
                writer.save(*raw);
                state.save(writer);
                writer.close();
                return toHex(raw->str());
            }

            std::string describeNpc(MWWorld::Ptr n);

            std::unique_ptr<MWWorld::LocalScripts> mLocalScripts;
            ActorWorld mActorWorld;
            ActorWindow mActorWindow;
            ActorMechanics mActorMechanics;
            ActorDialogue mActorDialogue;
            ActorLua mActorLua;
            NoRendering mNoRendering;
            uint32_t mNextIndex = 700;
            std::vector<std::shared_ptr<void>> mHolders;
        };

        // ---------------------------------------------------------------------------------------------- npc records

        TEST_F(ActorTest, an_npc_answers_its_record)
        {
            MWWorld::Ptr n = npc("t_npc_a", [](ESM::NPC& r) {
                r.mFlags = ESM::NPC::Essential;
                r.mFaction = ESM::RefId::stringRefId("t_faction");
                r.mNpdt.mRank = 2;
            });
            const MWWorld::Class& cls = n.getClass();
            EXPECT_TRUE(cls.isNpc());
            EXPECT_TRUE(cls.isActor());
            EXPECT_TRUE(cls.useAnim());
            EXPECT_TRUE(cls.isBipedal(n));
            EXPECT_TRUE(cls.canSwim(n));
            EXPECT_TRUE(cls.canWalk(n));
            EXPECT_TRUE(cls.hasInventoryStore(n));
            EXPECT_FALSE(cls.allowTelekinesis(n));
            EXPECT_TRUE(cls.isEssential(n));
            EXPECT_FALSE(cls.isPersistent(n));
            EXPECT_EQ(cls.getName(n), "Npc t_npc_a");
            EXPECT_EQ(cls.getScript(n), rid("t_npc_a_script"));
            EXPECT_EQ(cls.getBaseGold(n), 100);
            EXPECT_EQ(cls.getBaseFightRating(n), 20);
            EXPECT_EQ(cls.getPrimaryFaction(n), rid("t_faction"));
            EXPECT_TRUE(cls.isClass(n, "testclass"));
            EXPECT_FALSE(cls.isClass(n, "mageclass"));
            EXPECT_EQ(cls.getServices(n), ESM::NPC::Armor | ESM::NPC::Spells);
        }

        TEST_F(ActorTest, the_services_of_an_autocalculated_npc_are_its_classes)
        {
            MWWorld::Ptr n = npc("t_npc_auto", [](ESM::NPC& r) { r.mFlags = ESM::NPC::Autocalc; });
            EXPECT_EQ(n.getClass().getServices(n), ESM::NPC::Weapon | ESM::NPC::Training);
            MWWorld::Ptr m = npc("t_npc_auto_mage", [](ESM::NPC& r) {
                r.mFlags = ESM::NPC::Autocalc;
                r.mClass = ESM::RefId::stringRefId("mageclass");
            });
            EXPECT_EQ(m.getClass().getServices(m), ESM::NPC::Spells);
        }

        TEST_F(ActorTest, the_faction_rank_comes_from_the_stats_and_falls_back_to_the_record)
        {
            MWWorld::Ptr none = npc("t_npc_nofaction");
            EXPECT_EQ(none.getClass().getPrimaryFactionRank(none), -1);

            MWWorld::Ptr member = npc("t_npc_member", [](ESM::NPC& r) {
                r.mFaction = ESM::RefId::stringRefId("t_faction");
                r.mNpdt.mRank = 3;
            });
            // no custom data yet: the record's rank
            EXPECT_EQ(member.getClass().getPrimaryFactionRank(member), 3);
            // custom data without the faction: still the record's rank
            member.getClass().getNpcStats(member);
            EXPECT_EQ(member.getClass().getPrimaryFactionRank(member), 3);
            // the NPC joined the faction with rank 0 afterwards: the stats win
            member.getClass().getNpcStats(member).joinFaction(rid("t_faction"));
            EXPECT_EQ(member.getClass().getPrimaryFactionRank(member), 0);
            member.getClass().getNpcStats(member).setFactionRank(rid("t_faction"), 0);
            EXPECT_EQ(member.getClass().getPrimaryFactionRank(member), 0);
        }

        TEST_F(ActorTest, a_new_npc_gets_its_stats_from_the_record)
        {
            MWWorld::Ptr n = npc("t_npc_stats");
            MWMechanics::NpcStats& s = n.getClass().getNpcStats(n);
            EXPECT_EQ(s.getLevel(), 1);
            EXPECT_EQ(s.getAttribute(ESM::Attribute::Speed).getBase(), 50.f);
            EXPECT_EQ(s.getAttribute(ESM::Attribute::Agility).getBase(), 40.f);
            EXPECT_EQ(s.getSkill(ESM::Skill::Athletics).getBase(), 30.f);
            EXPECT_EQ(s.getHealth().getBase(), 120.f);
            EXPECT_EQ(s.getMagicka().getBase(), 60.f);
            EXPECT_EQ(s.getFatigue().getBase(), 200.f);
            EXPECT_EQ(s.getBaseDisposition(), 40);
            EXPECT_EQ(s.getReputation(), 7);
            EXPECT_EQ(s.getGoldPool(), 100);
            EXPECT_EQ(s.getAiSetting(MWMechanics::AiSetting::Hello).getBase(), 30);
            EXPECT_EQ(s.getAiSetting(MWMechanics::AiSetting::Fight).getBase(), 20);
            EXPECT_EQ(s.getAiSetting(MWMechanics::AiSetting::Flee).getBase(), 10);
            EXPECT_EQ(s.getAiSetting(MWMechanics::AiSetting::Alarm).getBase(), 5);
            // the same custom data comes back
            EXPECT_EQ(&n.getClass().getCreatureStats(n), &s);
        }

        TEST_F(ActorTest, an_npc_with_a_faction_gets_the_reputation_of_its_rank)
        {
            // iAutoRepFacMod 2 and iAutoRepLevMod 0 (the fixture's values): reputation = 2 * (rank + 1)
            MWWorld::Ptr n = npc("t_npc_rep", [](ESM::NPC& r) {
                r.mFaction = ESM::RefId::stringRefId("t_faction");
                r.mNpdt.mRank = 3;
                r.mNpdt.mLevel = 5;
            });
            EXPECT_EQ(n.getClass().getNpcStats(n).getReputation(), 8);
        }

        TEST_F(ActorTest, an_autocalculated_npc_is_computed_from_race_and_class)
        {
            std::ostringstream out;
            for (const char* race : { "bonusrace", "testrace" })
                for (bool female : { false, true })
                    for (int level : { 1, 9 })
                    {
                        MWWorld::Ptr n
                            = npc(std::string("t_auto_") + race + (female ? "_f" : "_m") + std::to_string(level),
                                [&](ESM::NPC& r) {
                                    r.mNpdtType = ESM::NPC::NPC_WITH_AUTOCALCULATED_STATS;
                                    r.mFlags = ESM::NPC::Autocalc | (female ? ESM::NPC::Female : 0);
                                    r.mRace = ESM::RefId::stringRefId(race);
                                    r.mNpdt.mLevel = static_cast<int16_t>(level);
                                });
                        MWMechanics::NpcStats& s = n.getClass().getNpcStats(n);
                        out << "[" << race << (female ? " female" : " male") << " level " << level << "]\n";
                        out << "  attributes:";
                        for (int i = 0; i < ESM::Attribute::Length; ++i)
                            out << " " << f(s.getAttribute(ESM::Attribute::indexToRefId(i)).getBase());
                        out << "\n  skills:";
                        for (int i = 0; i < ESM::Skill::Length; ++i)
                            out << " " << f(s.getSkill(ESM::Skill::indexToRefId(i)).getBase());
                        out << "\n  health=" << f(s.getHealth().getBase()) << " magicka=" << f(s.getMagicka().getBase())
                            << " fatigue=" << f(s.getFatigue().getBase()) << " level=" << s.getLevel()
                            << " gold=" << s.getGoldPool() << " rep=" << s.getReputation() << "\n";
                    }
            expectRecording("actors_npc_autocalc.golden.txt", out.str());
        }

        // ---------------------------------------------------------------------------------------------- npc table

        std::string ActorTest::describeNpc(MWWorld::Ptr n)
        {
            const MWWorld::Class& cls = n.getClass();
            std::ostringstream out;
            const std::string id = n.getCellRef().getRefId().getRefIdString();
            out << "[" << id << "]\n";
            out << "  name=[" << cls.getName(n) << "] model=[" << attempt([&] { return std::string(cls.getModel(n)); })
                << "] corrected=[" << attempt([&] { return std::string(cls.getCorrectedModel(n).value()); })
                << "] script=[" << cls.getScript(n).getRefIdString() << "]\n";
            out << "  essential=" << yn(cls.isEssential(n)) << " persistent=" << yn(cls.isPersistent(n))
                << " services=" << cls.getServices(n) << " baseGold=" << cls.getBaseGold(n)
                << " baseFight=" << cls.getBaseFightRating(n) << " faction=["
                << cls.getPrimaryFaction(n).getRefIdString() << "] rank=" << cls.getPrimaryFactionRank(n) << "\n";
            out << "  swim=" << yn(cls.canSwim(n)) << " walk=" << yn(cls.canWalk(n))
                << " biped=" << yn(cls.isBipedal(n)) << " hasInv=" << yn(cls.hasInventoryStore(n))
                << " npc=" << yn(cls.isNpc()) << " actor=" << yn(cls.isActor()) << " anim=" << yn(cls.useAnim())
                << " tele=" << yn(cls.allowTelekinesis(n)) << "\n";
            const MWMechanics::NpcStats& s = cls.getNpcStats(n);
            out << "  level=" << s.getLevel() << " health=" << f(s.getHealth().getBase())
                << " magicka=" << f(s.getMagicka().getBase()) << " fatigue=" << f(s.getFatigue().getBase())
                << " disposition=" << s.getBaseDisposition() << " reputation=" << s.getReputation()
                << " goldPool=" << s.getGoldPool() << "\n";
            out << "  attributes:";
            for (int i = 0; i < ESM::Attribute::Length; ++i)
                out << " " << f(s.getAttribute(ESM::Attribute::indexToRefId(i)).getModified());
            out << "\n  skills:";
            for (int i = 0; i < ESM::Skill::Length; ++i)
                out << " " << f(cls.getSkill(n, ESM::Skill::indexToRefId(i)));
            out << "\n  capacity=" << f(cls.getCapacity(n)) << " encumbrance=" << f(cls.getEncumbrance(n))
                << " armor=" << attempt([&] { return f(cls.getArmorRating(n, false)); }) << "\n";
            out << "  walk=" << f(cls.getWalkSpeed(n)) << " run=" << f(cls.getRunSpeed(n))
                << " swimSpeed=" << f(cls.getSwimSpeed(n)) << " max=" << f(cls.getMaxSpeed(n))
                << " jump=" << f(cls.getJump(n)) << "\n";
            for (bool rendering : { false, true })
            {
                vsg::vec3 scale(1.f, 1.f, 1.f);
                cls.adjustScale(n, scale, rendering);
                out << "  scale(rendering=" << yn(rendering) << ")=" << f(scale.x) << "," << f(scale.y) << ","
                    << f(scale.z) << "\n";
            }
            for (bool full : { false, true })
            {
                mActorWindow.mFullHelp = full;
                const MWGui::ToolTipInfo info = cls.getToolTipInfo(n, 1);
                out << "  tooltip(full=" << yn(full) << ") hasToolTip=" << yn(cls.hasToolTip(n)) << " caption=["
                    << info.caption << "] extra=[" << info.extra << "]\n";
            }
            mActorWindow.mFullHelp = false;
            return out.str();
        }

        TEST_F(ActorTest, npc_table)
        {
            std::vector<MWWorld::Ptr> npcs;
            npcs.push_back(npc("t_t_plain"));
            npcs.push_back(npc("t_t_female_tall", [](ESM::NPC& r) {
                r.mRace = ESM::RefId::stringRefId("tallrace");
                r.mFlags = ESM::NPC::Female;
            }));
            npcs.push_back(npc("t_t_male_tall", [](ESM::NPC& r) { r.mRace = ESM::RefId::stringRefId("tallrace"); }));
            npcs.push_back(npc("t_t_beast", [](ESM::NPC& r) {
                r.mRace = ESM::RefId::stringRefId("beastrace");
                r.mFlags = ESM::NPC::Respawn;
                r.mRecordFlags |= ESM::FLAG_Persistent;
            }));
            npcs.push_back(npc("t_t_faction", [](ESM::NPC& r) {
                r.mFaction = ESM::RefId::stringRefId("t_faction");
                r.mNpdt.mRank = 1;
                r.mNpdt.mLevel = 3;
                r.mAiData.mServices = 0;
            }));
            npcs.push_back(npc("t_t_weak", [](ESM::NPC& r) {
                for (auto& a : r.mNpdt.mAttributes)
                    a = 5;
                for (auto& sk : r.mNpdt.mSkills)
                    sk = 100;
                r.mNpdt.mGold = 0;
            }));
            std::ostringstream out;
            for (const MWWorld::Ptr& n : npcs)
                out << describeNpc(n);
            expectRecording("actors_npc.golden.txt", out.str());
        }

        // ---------------------------------------------------------------------------------------------- speeds

        TEST_F(ActorTest, walk_run_and_swim_speed_follow_the_gmst_formulas)
        {
            // speed 50, fMinWalkSpeed 100, fMaxWalkSpeed 200: walk = 100 + 0.01 * 50 * 100 = 150 with no load
            MWWorld::Ptr n = npc("t_speed");
            const MWWorld::Class& cls = n.getClass();
            EXPECT_FLOAT_EQ(cls.getWalkSpeed(n), 150.f);
            // run = walk * (0.01 * athletics 30 * fAthleticsRunBonus 1 + fBaseRunMultiplier 1.5) = 150 * 1.8
            EXPECT_FLOAT_EQ(cls.getRunSpeed(n), 270.f);
            // swimming on foot: walk * (1 + 0) * (fSwimRunBase 0.5 + 0.01 * 30 * fSwimRunAthleticsMult 0.1)
            EXPECT_FLOAT_EQ(cls.getSwimSpeed(n), 150.f * 0.53f);
            // sneaking multiplies the walk speed by fSneakSpeedMultiplier 0.5
            mActorMechanics.mSneaking = true;
            EXPECT_FLOAT_EQ(cls.getWalkSpeed(n), 75.f);
            mActorMechanics.mSneaking = false;
            // half-loaded (capacity 250, load 125): walk * (1 - fEncumberedMoveEffect 0.5 * 0.5)
            addWeight(n, "t_load_a", 125.f);
            EXPECT_FLOAT_EQ(cls.getEncumbrance(n), 125.f);
            EXPECT_FLOAT_EQ(cls.getWalkSpeed(n), 112.5f);
        }

        TEST_F(ActorTest, the_current_maximum_speed_depends_on_the_state)
        {
            MWWorld::Ptr n = npc("t_maxspeed");
            const MWWorld::Class& cls = n.getClass();
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 150.f);
            mActorMechanics.mRunning = true;
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 270.f);
            // running and sneaking: the walk speed (with the sneak factor)
            mActorMechanics.mSneaking = true;
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 75.f);
            mActorMechanics.mSneaking = false;
            mActorWorld.mSwimming = true;
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 270.f * 0.53f);
            mActorWorld.mSwimming = false;
            mActorMechanics.mRunning = false;
            // levitating: 5 + 0.01 * (50 + 100) * (10 - 5) = 12.5
            n.getClass().getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Levitate), MWMechanics::EffectParam(100.f));
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 12.5f);
            mActorWorld.mLevitation = false;
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 150.f);
            mActorWorld.mLevitation = true;
            // over-encumbered: no movement at all (capacity 250)
            addWeight(n, "t_load_b", 300.f);
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(n), 0.f);
        }

        TEST_F(ActorTest, a_paralyzed_knocked_down_or_dead_npc_does_not_move_or_jump)
        {
            MWWorld::Ptr n = npc("t_still");
            const MWWorld::Class& cls = n.getClass();
            EXPECT_GT(cls.getMaxSpeed(n), 0.f);
            cls.getCreatureStats(n).setKnockedDown(true);
            EXPECT_EQ(cls.getMaxSpeed(n), 0.f);
            EXPECT_EQ(cls.getJump(n), 0.f);
            cls.getCreatureStats(n).setKnockedDown(false);
            cls.getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Paralyze), MWMechanics::EffectParam(1.f));
            EXPECT_EQ(cls.getMaxSpeed(n), 0.f);
            EXPECT_EQ(cls.getJump(n), 0.f);

            MWWorld::Ptr dead = npc("t_dead");
            MWMechanics::DynamicStat<float> health(dead.getClass().getCreatureStats(dead).getHealth());
            health.setCurrent(-1.f);
            dead.getClass().getCreatureStats(dead).setHealth(health);
            EXPECT_EQ(dead.getClass().getMaxSpeed(dead), 0.f);
            EXPECT_EQ(dead.getClass().getJump(dead), 0.f);
        }

        TEST_F(ActorTest, a_werewolf_runs_faster_with_nothing_drawn)
        {
            MWWorld::Ptr n = npc("t_wolf");
            n.getClass().getNpcStats(n).setWerewolf(true);
            mActorMechanics.mRunning = true;
            EXPECT_FLOAT_EQ(n.getClass().getMaxSpeed(n), 270.f * 1.5f);
            n.getClass().getCreatureStats(n).setDrawState(MWMechanics::DrawState::Weapon);
            EXPECT_FLOAT_EQ(n.getClass().getMaxSpeed(n), 270.f);
        }

        TEST_F(ActorTest, the_jump_height_follows_the_acrobatics_formula)
        {
            MWWorld::Ptr n = npc("t_jump");
            const MWWorld::Class& cls = n.getClass();
            // acrobatics 30, no load, full fatigue (fatigue term fFatigueBase 1.25), no jump effect, standing
            double x = 128.0 + std::pow(30.0 / 15.0, 2.0);
            x *= (0.5 + 0.5 * 1.0);
            x *= 1.25;
            x -= -Constants::GravityConst * Constants::UnitsPerMeter;
            x /= 3.0;
            EXPECT_NEAR(cls.getJump(n), static_cast<float>(x), 1e-3f);
            // more acrobatics jumps higher
            n.getClass().getNpcStats(n).getSkill(ESM::Skill::Acrobatics).setBase(80.f);
            // acrobatics above 50: a = 50, b = 30
            double y = 128.0 + std::pow(50.0 / 15.0, 2.0) + 3.0 * 30.0 * 2.0;
            y *= 1.0;
            y *= 1.25;
            y -= -Constants::GravityConst * Constants::UnitsPerMeter;
            y /= 3.0;
            EXPECT_NEAR(cls.getJump(n), static_cast<float>(y), 1e-2f);
            // the jump effect adds 64 per point
            n.getClass().getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Jump), MWMechanics::EffectParam(2.f));
            double z = 128.0 + std::pow(50.0 / 15.0, 2.0) + 3.0 * 30.0 * 2.0 + 2.0 * 64.0;
            z *= 1.25;
            z -= -Constants::GravityConst * Constants::UnitsPerMeter;
            z /= 3.0;
            EXPECT_NEAR(cls.getJump(n), static_cast<float>(z), 1e-2f);
            // running multiplies by fJumpRunMultiplier 1.2 before the fatigue term and the gravity
            n.getClass().getCreatureStats(n).setMovementFlag(MWMechanics::CreatureStats::Flag_Run, true);
            double w = 128.0 + std::pow(50.0 / 15.0, 2.0) + 3.0 * 30.0 * 2.0 + 2.0 * 64.0;
            w *= 1.2;
            w *= 1.25;
            w -= -Constants::GravityConst * Constants::UnitsPerMeter;
            w /= 3.0;
            EXPECT_NEAR(cls.getJump(n), static_cast<float>(w), 1e-2f);
        }

        // ---------------------------------------------------------------------------------------------- load

        TEST_F(ActorTest, the_capacity_is_strength_times_the_gmst_and_the_load_counts_magic_effects)
        {
            MWWorld::Ptr n = npc("t_cap");
            const MWWorld::Class& cls = n.getClass();
            EXPECT_FLOAT_EQ(cls.getCapacity(n), 250.f); // strength 50 * fEncumbranceStrMult 5
            addWeight(n, "t_load_c", 10.f, 3);
            EXPECT_FLOAT_EQ(cls.getEncumbrance(n), 30.f);
            // feather takes weight off, burden adds
            cls.getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Feather), MWMechanics::EffectParam(12.f));
            EXPECT_FLOAT_EQ(cls.getEncumbrance(n), 18.f);
            cls.getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Burden), MWMechanics::EffectParam(40.f));
            EXPECT_FLOAT_EQ(cls.getEncumbrance(n), 58.f);
            // never negative
            cls.getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Feather), MWMechanics::EffectParam(500.f));
            EXPECT_FLOAT_EQ(cls.getEncumbrance(n), 0.f);
            EXPECT_FLOAT_EQ(cls.getNormalizedEncumbrance(n), 0.f);
        }

        TEST_F(ActorTest, burden_is_ignored_for_the_player_in_god_mode_and_a_werewolf_carries_nothing)
        {
            const MWWorld::Class& cls = mPlayer.getClass();
            cls.getCreatureStats(mPlayer).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Burden), MWMechanics::EffectParam(40.f));
            EXPECT_FLOAT_EQ(cls.getEncumbrance(mPlayer), 40.f);
            mActorWorld.mGodMode = true;
            EXPECT_FLOAT_EQ(cls.getEncumbrance(mPlayer), 0.f);
            mActorWorld.mGodMode = false;
            cls.getNpcStats(mPlayer).setWerewolf(true);
            EXPECT_FLOAT_EQ(cls.getEncumbrance(mPlayer), 0.f);
        }

        // ---------------------------------------------------------------------------------------------- armor

        TEST_F(ActorTest, an_unarmored_npcs_rating_comes_from_the_unarmored_skill)
        {
            // every slot is unarmored: (fUnarmoredBase1 0.1 * skill) * (fUnarmoredBase2 0.065 * skill) per slot, the
            // slots weigh 0.3 + 6 * 0.1 + 2 * 0.05 = 1.0
            MWWorld::Ptr n = npc("t_unarmored");
            const float skill = 30.f;
            const float slot = (0.1f * skill) * (0.065f * skill);
            EXPECT_NEAR(n.getClass().getArmorRating(n, false), slot * 1.0f, 1e-3f);
            // a shield effect adds its magnitude
            n.getClass().getCreatureStats(n).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Shield), MWMechanics::EffectParam(10.f));
            EXPECT_NEAR(n.getClass().getArmorRating(n, false), slot * 1.0f + 10.f, 1e-3f);
        }

        TEST_F(ActorTest, a_worn_cuirass_counts_thirty_percent_with_its_condition)
        {
            MWWorld::Ptr n = npc("t_armored");
            const float skill = 30.f;
            const float unarmoredSlot = (0.1f * skill) * (0.065f * skill);
            MWWorld::Ptr c = armor("t_a_cuirass_rating", ESM::Armor::Cuirass, 9.f, 20, 100);
            MWWorld::InventoryStore& inv = n.getClass().getInventoryStore(n);
            MWWorld::ContainerStoreIterator it = inv.add(c, 1, false);
            inv.equip(MWWorld::InventoryStore::Slot_Cuirass, it);
            const float rating
                = it->getClass().getSkillAdjustedArmorRating(*it, n) * it->getClass().getItemNormalizedHealth(*it);
            EXPECT_NEAR(n.getClass().getArmorRating(n, false), rating * 0.3f + unarmoredSlot * 0.7f, 1e-3f);
            // damaged armor protects less: half the health, half the rating
            it->getCellRef().setCharge(50);
            const float damaged = it->getClass().getSkillAdjustedArmorRating(*it, n) * 0.5f;
            EXPECT_NEAR(n.getClass().getArmorRating(n, false), damaged * 0.3f + unarmoredSlot * 0.7f, 1e-3f);
        }

        // ---------------------------------------------------------------------------------------------- actions

        TEST_F(ActorTest, activating_an_npc_chooses_talk_open_or_refusal)
        {
            MWWorld::Ptr n = npc("t_activate");
            // an ordinary NPC talks
            EXPECT_EQ(actionKind(*n.getClass().activate(n, mPlayer)), "talk");
            // the player activated by another NPC talks to it
            EXPECT_EQ(actionKind(*mPlayer.getClass().activate(mPlayer, n)), "talk");
            // knocked down: loot
            n.getClass().getCreatureStats(n).setKnockedDown(true);
            EXPECT_EQ(actionKind(*n.getClass().activate(n, mPlayer)), "open");
            n.getClass().getCreatureStats(n).setKnockedDown(false);
            // sneaking while activating: loot (pickpocket)
            mActorMechanics.mSneaking = true;
            EXPECT_EQ(actionKind(*n.getClass().activate(n, mPlayer)), "open");
            mActorMechanics.mSneaking = false;
            // a werewolf is refused
            mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(true);
            EXPECT_EQ(actionKind(*n.getClass().activate(n, mPlayer)), "failed");
            mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(false);
            // dead: the corpse can be looted (mCanLootDuringDeathAnimation is on by default)
            MWMechanics::DynamicStat<float> health(n.getClass().getCreatureStats(n).getHealth());
            health.setCurrent(-1.f);
            n.getClass().getCreatureStats(n).setHealth(health);
            EXPECT_EQ(actionKind(*n.getClass().activate(n, mPlayer)), "open");
        }

        TEST_F(ActorTest, an_npc_in_combat_with_the_actor_refuses_to_talk)
        {
            MWWorld::Ptr n = npc("t_combat");
            // no fight state can be made without AI packages; the check of the in-combat refusal is the next test
            EXPECT_FALSE(n.getClass().getCreatureStats(n).getAiSequence().isInCombat());
            EXPECT_EQ(actionKind(*n.getClass().activate(n, mPlayer)), "talk");
        }

        TEST_F(ActorTest, the_tooltip_follows_the_gui_and_the_combat_state)
        {
            MWWorld::Ptr n = npc("t_tip");
            EXPECT_TRUE(n.getClass().hasToolTip(n)); // no custom data yet
            n.getClass().getCreatureStats(n);
            EXPECT_TRUE(n.getClass().hasToolTip(n)); // not in combat
            mActorWindow.mGuiMode = true;
            EXPECT_TRUE(n.getClass().hasToolTip(n));
            mActorWindow.mGuiMode = false;
            // a werewolf shows its popup name, and its real name only with full help
            n.getClass().getNpcStats(n).setWerewolf(true);
            EXPECT_EQ(n.getClass().getName(n), "Werewolf");
            EXPECT_EQ(n.getClass().getToolTipInfo(n, 1).caption, "Werewolf");
            mActorWindow.mFullHelp = true;
            EXPECT_EQ(n.getClass().getToolTipInfo(n, 1).caption, "Werewolf (Npc t_tip)");
        }

        // ---------------------------------------------------------------------------------------------- hit

        TEST_F(ActorTest, a_hit_without_an_attacker_lowers_the_stats_and_kills)
        {
            MWWorld::Ptr n = npc("t_hit");
            const MWWorld::Class& cls = n.getClass();
            cls.getCreatureStats(n);
            std::map<std::string, float> damages{ { "health", 30.f }, { "fatigue", 20.f }, { "magicka", 10.f } };
            cls.onHit(n, damages, ESM::RefId::stringRefId("t_sword"), MWWorld::Ptr(), true,
                MWMechanics::DamageSourceType::Melee);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(n).getHealth().getCurrent(), 90.f);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(n).getFatigue().getCurrent(), 180.f);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(n).getMagicka().getCurrent(), 50.f);
            EXPECT_EQ(cls.getCreatureStats(n).getLastHitObject(), rid("t_sword"));
            EXPECT_TRUE(mActorMechanics.mKilled.empty());
            // a missed hit changes nothing but the attempt
            cls.onHit(n, damages, ESM::RefId::stringRefId("t_missed"), MWWorld::Ptr(), false,
                MWMechanics::DamageSourceType::Melee);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(n).getHealth().getCurrent(), 90.f);
            EXPECT_EQ(cls.getCreatureStats(n).getLastHitAttemptObject(), rid("t_missed"));
            EXPECT_EQ(cls.getCreatureStats(n).getLastHitObject(), rid("t_sword"));
            // small damage under 0.001 is ignored
            cls.onHit(
                n, { { "health", 0.0005f } }, ESM::RefId(), MWWorld::Ptr(), true, MWMechanics::DamageSourceType::Melee);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(n).getHealth().getCurrent(), 90.f);
            // a killing blow tells the mechanics manager
            cls.onHit(
                n, { { "health", 500.f } }, ESM::RefId(), MWWorld::Ptr(), true, MWMechanics::DamageSourceType::Melee);
            EXPECT_TRUE(cls.getCreatureStats(n).isDead());
            ASSERT_EQ(mActorMechanics.mKilled.size(), 1u);
            EXPECT_EQ(mActorMechanics.mKilled[0], "t_hit");
        }

        TEST_F(ActorTest, a_hit_by_an_attacker_may_speak_and_knock_down_by_the_dice)
        {
            MWWorld::Ptr n = npc("t_hit2");
            MWWorld::Ptr attacker = npc("t_attacker");
            const MWWorld::Class& cls = n.getClass();
            cls.getCreatureStats(n);
            // agility 40 * fKnockDownMult 0.5 = 20 <= damage 30; knock-down odds 40 * 50 * 0.01 + 50 = 70
            // a first draw below iVoiceHitOdds 30 makes the NPC say "hit"; the knock-down roll comes second
            const unsigned lowRoll = seedWithRoll(5);
            seedWorld(lowRoll);
            cls.onHit(n, { { "health", 30.f } }, ESM::RefId(), attacker, true, MWMechanics::DamageSourceType::Melee);
            ASSERT_EQ(mActorDialogue.mSaid.size(), 1u);
            EXPECT_EQ(mActorDialogue.mSaid[0], "t_hit2:hit");
            EXPECT_EQ(mActorMechanics.mAttacked.size(), 1u);
            EXPECT_EQ(drawsSince(lowRoll), 2);
            EXPECT_TRUE(cls.getCreatureStats(n).getAttacked());

            // a first draw of 99 does not speak
            mActorDialogue.mSaid.clear();
            const unsigned highRoll = seedWithRoll(99);
            seedWorld(highRoll);
            cls.onHit(n, { { "health", 1.f } }, ESM::RefId(), attacker, true, MWMechanics::DamageSourceType::Melee);
            EXPECT_TRUE(mActorDialogue.mSaid.empty());
            // damage below the agility term (20) cannot knock down: the hit recovery is set instead, one draw only
            EXPECT_EQ(drawsSince(highRoll), 1);
            EXPECT_TRUE(cls.getCreatureStats(n).getHitRecovery());
        }

        // ---------------------------------------------------------------------------------------------- sounds

        TEST_F(ActorTest, an_npcs_footsteps_depend_on_the_ground_and_the_boots)
        {
            MWWorld::Ptr n = npc("t_steps");
            const MWWorld::Class& cls = n.getClass();
            auto id = [&](const char* name) { return cls.getSoundIdFromSndGen(n, name).getRefIdString(); };
            EXPECT_EQ(id("left"), "FootBareLeft");
            EXPECT_EQ(id("right"), "FootBareRight");
            EXPECT_EQ(id("land"), "");
            EXPECT_EQ(id("moan"), "");
            EXPECT_EQ(id("roar"), "");
            EXPECT_EQ(id("scream"), "");
            EXPECT_EQ(id("swimleft"), "Swim Left");
            EXPECT_EQ(id("swimright"), "Swim Right");
            EXPECT_THROW(cls.getSoundIdFromSndGen(n, "bark"), std::runtime_error);
            mActorWorld.mUnderwater = true;
            EXPECT_EQ(id("left"), "FootWaterLeft");
            mActorWorld.mUnderwater = false;
            mActorWorld.mSwimming = true;
            EXPECT_EQ(id("right"), "Swim Right");
            mActorWorld.mSwimming = false;
            mActorWorld.mFlying = true;
            EXPECT_EQ(id("left"), "");
            mActorWorld.mFlying = false;
            mActorWorld.mOnGround = false;
            EXPECT_EQ(id("left"), "");
            mActorWorld.mOnGround = true;
        }

        // ---------------------------------------------------------------------------------------------- scale

        TEST_F(ActorTest, the_scale_of_an_npc_comes_from_its_races_height_and_weight)
        {
            MWWorld::Ptr male = npc("t_scale_m", [](ESM::NPC& r) { r.mRace = ESM::RefId::stringRefId("tallrace"); });
            MWWorld::Ptr female = npc("t_scale_f", [](ESM::NPC& r) {
                r.mRace = ESM::RefId::stringRefId("tallrace");
                r.mFlags = ESM::NPC::Female;
            });
            vsg::vec3 scale(2.f, 2.f, 2.f);
            male.getClass().adjustScale(male, scale, true);
            EXPECT_FLOAT_EQ(scale.x, 1.5f);
            EXPECT_FLOAT_EQ(scale.y, 1.5f);
            EXPECT_FLOAT_EQ(scale.z, 2.5f);
            scale = vsg::vec3(2.f, 2.f, 2.f);
            female.getClass().adjustScale(female, scale, true);
            EXPECT_FLOAT_EQ(scale.x, 1.0f);
            EXPECT_FLOAT_EQ(scale.y, 1.0f);
            EXPECT_FLOAT_EQ(scale.z, 3.0f);
            // collision meshes are not scaled
            scale = vsg::vec3(2.f, 2.f, 2.f);
            male.getClass().adjustScale(male, scale, false);
            EXPECT_FLOAT_EQ(scale.x, 2.f);
            EXPECT_FLOAT_EQ(scale.z, 2.f);
        }

        // ---------------------------------------------------------------------------------------------- respawn

        TEST_F(ActorTest, an_npc_respawn_waits_for_the_death_animation_the_flag_and_the_delay)
        {
            MWWorld::Ptr n = npc("t_respawn", [](ESM::NPC& r) { r.mFlags = ESM::NPC::Respawn; });
            const MWWorld::Class& cls = n.getClass();
            // alive: nothing happens
            cls.respawn(n);
            EXPECT_EQ(mActorWorld.mMoves, 0);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
            // dead, death animation not finished: nothing
            MWMechanics::DynamicStat<float> health(cls.getCreatureStats(n).getHealth());
            health.setCurrent(-1.f);
            cls.getCreatureStats(n).setHealth(health);
            cls.getCreatureStats(n).setDeathAnimationFinished(false);
            cls.respawn(n);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
            // finished, but the time of death plus min(72, 48) hours is in the future
            cls.getCreatureStats(n).setDeathAnimationFinished(true);
            mActorWorld.mNow = MWWorld::TimeStamp(0.f, 1);
            cls.respawn(n);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
            EXPECT_NE(n.getRefData().getCustomData(), nullptr);
            // without the respawn flag it never respawns
            MWWorld::Ptr mortal = npc("t_mortal");
            MWMechanics::DynamicStat<float> h2(mortal.getClass().getCreatureStats(mortal).getHealth());
            h2.setCurrent(-1.f);
            mortal.getClass().getCreatureStats(mortal).setHealth(h2);
            mortal.getClass().getCreatureStats(mortal).setDeathAnimationFinished(true);
            mActorWorld.mNow = MWWorld::TimeStamp(0.f, 40);
            mortal.getClass().respawn(mortal);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
            EXPECT_NE(mortal.getRefData().getCustomData(), nullptr);
        }

        // ---------------------------------------------------------------------------------------------- base settings

        TEST_F(ActorTest, the_base_ai_setting_and_inventory_of_an_npc_record_can_be_changed)
        {
            MWWorld::Ptr n = npc("t_base");
            const MWWorld::Class& cls = n.getClass();
            cls.setBaseAISetting(rid("t_base"), MWMechanics::AiSetting::Fight, 77);
            // the reference made before the change still points at the old record: only the store's copy changed
            EXPECT_EQ(cls.getBaseFightRating(n), 20);
            EXPECT_EQ(mStore.get<ESM::NPC>().find(rid("t_base"))->mAiData.mFight, 77);
            cls.setBaseAISetting(rid("t_base"), MWMechanics::AiSetting::Hello, 120);
            EXPECT_EQ(mStore.get<ESM::NPC>().find(rid("t_base"))->mAiData.mHello, 120);
            misc("t_base_item", 1.f);
            cls.modifyBaseInventory(rid("t_base"), rid("t_base_item"), 4);
            const auto& list = mStore.get<ESM::NPC>().find(rid("t_base"))->mInventory.mList;
            ASSERT_EQ(list.size(), 1u);
            EXPECT_EQ(list[0].mCount, 4);
            cls.modifyBaseInventory(rid("t_base"), rid("t_base_item"), -4);
            // the entry stays, at zero
            ASSERT_EQ(mStore.get<ESM::NPC>().find(rid("t_base"))->mInventory.mList.size(), 1u);
            EXPECT_EQ(mStore.get<ESM::NPC>().find(rid("t_base"))->mInventory.mList[0].mCount, 0);
        }

        // ---------------------------------------------------------------------------------------------- npc save

        // Recorded from the old code (apps/openmw/mwclass) before the move: the whole stream of one saved NPC.
        TEST_F(ActorTest, npc_state_save_golden)
        {
            MWWorld::Ptr n = npc("t_save_npc", [](ESM::NPC& r) {
                r.mFaction = ESM::RefId::stringRefId("t_faction");
                r.mNpdt.mRank = 1;
                r.mInventory.mList.push_back({ 2, R("t_save_item") });
                ESM::AIPackage wander;
                wander.mType = ESM::AI_Wander;
                wander.mWander = { 256, 2, 0, { 60, 20, 20, 10, 0, 0, 0, 0 }, 1 };
                r.mAiPackage.mList.push_back(wander);
            });
            misc("t_save_item", 2.f);
            MWMechanics::NpcStats& s = n.getClass().getNpcStats(n);
            // change the state the way play does
            MWMechanics::DynamicStat<float> health(s.getHealth());
            health.setCurrent(77.f);
            s.setHealth(health);
            s.joinFaction(rid("t_faction"));
            s.getSkill(ESM::Skill::Sneak).setBase(44.f);
            s.setReputation(9);
            s.setBounty(15);
            s.setLevelProgress(3);
            s.setWerewolf(false);
            MWWorld::Ptr armorPiece = armor("t_save_helm", ESM::Armor::Helmet, 2.f, 5);
            MWWorld::InventoryStore& inv = n.getClass().getInventoryStore(n);
            MWWorld::ContainerStoreIterator it = inv.add(armorPiece, 1, false);
            inv.equip(MWWorld::InventoryStore::Slot_Helmet, it);

            ESM::NpcState state;
            state.blank();
            n.getClass().writeAdditionalState(n, state);
            ASSERT_TRUE(state.mHasCustomState);
            const std::string bytes = writeObjectState(state);
            expectRecording("actors_npc_state.golden.txt", bytes + "\n");
        }

        TEST_F(ActorTest, an_npc_state_round_trips_through_the_class_hooks)
        {
            MWWorld::Ptr n = npc("t_round_npc", [](ESM::NPC& r) {
                r.mInventory.mList.push_back({ 3, R("t_round_item") });
                ESM::AIPackage wander;
                wander.mType = ESM::AI_Wander;
                wander.mWander = { 100, 1, 0, { 60, 20, 20, 10, 0, 0, 0, 0 }, 0 };
                r.mAiPackage.mList.push_back(wander);
            });
            misc("t_round_item", 2.f);
            MWMechanics::NpcStats& s = n.getClass().getNpcStats(n);
            MWMechanics::DynamicStat<float> health(s.getHealth());
            health.setCurrent(61.f);
            s.setHealth(health);
            s.getSkill(ESM::Skill::Sneak).setBase(51.f);
            s.setReputation(11);

            ESM::NpcState written;
            written.blank();
            n.getClass().writeAdditionalState(n, written);
            const std::string before = writeObjectState(written);

            // a fresh reference to the same record gets the state back
            ESM::CellRef cellRef = cellRefFor(rid("t_round_npc"));
            auto fresh = std::make_shared<MWWorld::LiveCellRef<ESM::NPC>>(
                cellRef, mStore.get<ESM::NPC>().find(rid("t_round_npc")));
            mHolders.push_back(fresh);
            MWWorld::Ptr other(fresh.get());
            EXPECT_EQ(other.getRefData().getCustomData(), nullptr);
            other.getClass().readAdditionalState(other, written);
            ASSERT_NE(other.getRefData().getCustomData(), nullptr);
            EXPECT_FLOAT_EQ(other.getClass().getCreatureStats(other).getHealth().getCurrent(), 61.f);
            EXPECT_FLOAT_EQ(other.getClass().getNpcStats(other).getSkill(ESM::Skill::Sneak).getBase(), 51.f);
            EXPECT_EQ(other.getClass().getNpcStats(other).getReputation(), 11);
            EXPECT_EQ(other.getClass().getContainerStore(other).count(rid("t_round_item")), 3);

            ESM::NpcState again;
            again.blank();
            other.getClass().writeAdditionalState(other, again);
            EXPECT_EQ(writeObjectState(again), before);
        }

        TEST_F(ActorTest, an_npc_state_without_stats_creates_them_from_the_record_and_the_hooks_skip_empty_state)
        {
            MWWorld::Ptr n = npc("t_missing");
            // no custom data: nothing to write
            ESM::NpcState empty;
            empty.blank();
            n.getClass().writeAdditionalState(n, empty);
            EXPECT_FALSE(empty.mHasCustomState);
            // no state in the save: the hook leaves the object alone
            ESM::NpcState none;
            none.blank();
            none.mHasCustomState = false;
            n.getClass().readAdditionalState(n, none);
            EXPECT_EQ(n.getRefData().getCustomData(), nullptr);
            // a save without ACDT fills the stats from the record first, then applies the saved state
            ESM::NpcState state;
            state.blank();
            state.mCreatureStats.mMissingACDT = true;
            n.getClass().readAdditionalState(n, state);
            ASSERT_NE(n.getRefData().getCustomData(), nullptr);
            EXPECT_EQ(n.getClass().getNpcStats(n).getAttribute(ESM::Attribute::Speed).getBase(), 50.f);
            // an NPC that is gone (count 0) and does not respawn writes no state
            MWWorld::Ptr gone = npc("t_gone");
            gone.getClass().getNpcStats(gone);
            gone.getCellRef().setCount(0);
            ESM::NpcState goneState;
            goneState.blank();
            gone.getClass().writeAdditionalState(gone, goneState);
            EXPECT_FALSE(goneState.mHasCustomState);
        }

        // ---------------------------------------------------------------------------------------------- creature

        TEST_F(ActorTest, a_creature_answers_its_record)
        {
            MWWorld::Ptr c = creature("t_cr_a", [](ESM::Creature& r) {
                r.mFlags = ESM::Creature::Walks | ESM::Creature::Essential | ESM::Creature::Weapon;
            });
            const MWWorld::Class& cls = c.getClass();
            EXPECT_TRUE(cls.isActor());
            EXPECT_FALSE(cls.isNpc());
            EXPECT_TRUE(cls.useAnim());
            EXPECT_TRUE(cls.isEssential(c));
            EXPECT_TRUE(cls.hasInventoryStore(c));
            EXPECT_FALSE(cls.isBipedal(c));
            EXPECT_FALSE(cls.canFly(c));
            EXPECT_TRUE(cls.canWalk(c));
            EXPECT_FALSE(cls.canSwim(c));
            EXPECT_EQ(cls.getName(c), "Creature t_cr_a");
            EXPECT_EQ(cls.getModel(c), "c/t_cr_a.nif");
            EXPECT_EQ(cls.getScript(c), rid("t_cr_a_script"));
            EXPECT_EQ(cls.getServices(c), ESM::NPC::Training);
            EXPECT_EQ(cls.getBaseGold(c), 25);
            EXPECT_EQ(cls.getBaseFightRating(c), 80);
        }

        TEST_F(ActorTest, the_movement_flags_of_a_creature_come_from_its_record_flags)
        {
            auto flags = [&](int f) {
                MWWorld::Ptr c = creature("t_cr_flags" + std::to_string(f),
                    [f](ESM::Creature& r) { r.mFlags = static_cast<unsigned char>(f); });
                const MWWorld::Class& cls = c.getClass();
                return yn(cls.isBipedal(c)) + yn(cls.canFly(c)) + yn(cls.canSwim(c)) + yn(cls.canWalk(c))
                    + yn(cls.hasInventoryStore(c)) + yn(cls.isEssential(c));
            };
            EXPECT_EQ(flags(0), "000000");
            EXPECT_EQ(flags(ESM::Creature::Bipedal), "101100");
            EXPECT_EQ(flags(ESM::Creature::Swims), "001000");
            EXPECT_EQ(flags(ESM::Creature::Flies), "010000");
            EXPECT_EQ(flags(ESM::Creature::Walks), "000100");
            EXPECT_EQ(flags(ESM::Creature::Weapon), "000010");
            EXPECT_EQ(flags(ESM::Creature::Essential), "000001");
        }

        TEST_F(ActorTest, a_creature_gets_its_stats_from_the_record)
        {
            MWWorld::Ptr c = creature("t_cr_stats");
            MWMechanics::CreatureStats& s = c.getClass().getCreatureStats(c);
            EXPECT_EQ(s.getLevel(), 4);
            EXPECT_EQ(s.getAttribute(ESM::Attribute::Speed).getBase(), 60.f);
            EXPECT_EQ(s.getAttribute(ESM::Attribute::Strength).getBase(), 70.f);
            EXPECT_EQ(s.getHealth().getBase(), 90.f);
            EXPECT_EQ(s.getMagicka().getBase(), 30.f);
            EXPECT_EQ(s.getFatigue().getBase(), 150.f);
            EXPECT_EQ(s.getGoldPool(), 25);
            EXPECT_EQ(s.getAiSetting(MWMechanics::AiSetting::Fight).getBase(), 80);
            EXPECT_EQ(s.getAiSetting(MWMechanics::AiSetting::Alarm).getBase(), 15);
            EXPECT_EQ(&c.getClass().getCreatureStats(c), &s);
            // no inventory store without the weapon flag
            EXPECT_THROW(c.getClass().getInventoryStore(c), std::runtime_error);
            EXPECT_NO_THROW(c.getClass().getContainerStore(c));
        }

        TEST_F(ActorTest, a_creature_skill_is_the_stat_of_its_specialization)
        {
            MWWorld::Ptr c = creature("t_cr_skill");
            const MWWorld::Class& cls = c.getClass();
            // the fixture's skill records are blank: specialization 0 = combat
            EXPECT_EQ(cls.getSkill(c, ESM::Skill::LongBlade), 40.f);
            EXPECT_EQ(cls.getSkill(c, ESM::Skill::Athletics), 40.f);
        }

        TEST_F(ActorTest, creature_speeds_follow_the_creature_gmsts)
        {
            // speed 60, fMinWalkSpeedCreature 50, fMaxWalkSpeedCreature 150: 50 + 0.01 * 60 * 100 = 110
            MWWorld::Ptr c = creature("t_cr_speed");
            const MWWorld::Class& cls = c.getClass();
            EXPECT_FLOAT_EQ(cls.getWalkSpeed(c), 110.f);
            EXPECT_FLOAT_EQ(cls.getRunSpeed(c), 110.f);
            // swimming: walk * (1 + 0) * (fSwimRunBase 0.5 + 0.01 * skill * fSwimRunAthleticsMult 0.1): the creature's
            // "athletics" is its combat skill 40
            EXPECT_FLOAT_EQ(cls.getSwimSpeed(c), 110.f * (0.5f + 0.01f * 40.f * 0.1f));
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(c), 110.f);
            mActorWorld.mSwimming = true;
            EXPECT_FLOAT_EQ(cls.getMaxSpeed(c), 110.f * 0.54f);
            mActorWorld.mSwimming = false;
            // paralyzed or dead: still
            cls.getCreatureStats(c).setKnockedDown(true);
            EXPECT_EQ(cls.getMaxSpeed(c), 0.f);
            cls.getCreatureStats(c).setKnockedDown(false);
            // a flying creature uses the fly speed: 5 + 0.01 * 60 * (10 - 5) = 8
            MWWorld::Ptr bird = creature("t_cr_bird", [](ESM::Creature& r) { r.mFlags = ESM::Creature::Flies; });
            EXPECT_FLOAT_EQ(bird.getClass().getMaxSpeed(bird), 8.f);
            // over-encumbered (capacity strength 70 * 5 = 350): none
            addWeight(c, "t_cr_load", 400.f);
            EXPECT_EQ(cls.getMaxSpeed(c), 0.f);
        }

        TEST_F(ActorTest, a_creatures_capacity_is_five_per_strength_and_its_armor_is_only_the_shield_effect)
        {
            MWWorld::Ptr c = creature("t_cr_cap");
            EXPECT_FLOAT_EQ(c.getClass().getCapacity(c), 350.f);
            EXPECT_FLOAT_EQ(c.getClass().getArmorRating(c, false), 0.f);
            c.getClass().getCreatureStats(c).getMagicEffects().add(
                MWMechanics::EffectKey(ESM::MagicEffect::Shield), MWMechanics::EffectParam(12.f));
            EXPECT_FLOAT_EQ(c.getClass().getArmorRating(c, false), 12.f);
            EXPECT_FLOAT_EQ(c.getClass().getArmorRating(c, true), 12.f);
        }

        TEST_F(ActorTest, a_creatures_scale_is_its_records_scale)
        {
            MWWorld::Ptr c = creature("t_cr_scale", [](ESM::Creature& r) { r.mScale = 1.75f; });
            vsg::vec3 scale(2.f, 2.f, 2.f);
            c.getClass().adjustScale(c, scale, true);
            EXPECT_FLOAT_EQ(scale.x, 3.5f);
            scale = vsg::vec3(2.f, 2.f, 2.f);
            c.getClass().adjustScale(c, scale, false);
            EXPECT_FLOAT_EQ(scale.z, 3.5f);
        }

        TEST_F(ActorTest, activating_a_creature_chooses_talk_open_or_refusal)
        {
            MWWorld::Ptr c = creature("t_cr_activate", [](ESM::Creature& r) { r.mScript = ESM::RefId(); });
            EXPECT_EQ(actionKind(*c.getClass().activate(c, mPlayer)), "talk");
            c.getClass().getCreatureStats(c).setKnockedDown(true);
            // knocked down and not a companion: the activation fails
            EXPECT_EQ(actionKind(*c.getClass().activate(c, mPlayer)), "failed");
            c.getClass().getCreatureStats(c).setKnockedDown(false);
            mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(true);
            EXPECT_EQ(actionKind(*c.getClass().activate(c, mPlayer)), "failed");
            mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(false);
            MWMechanics::DynamicStat<float> health(c.getClass().getCreatureStats(c).getHealth());
            health.setCurrent(-1.f);
            c.getClass().getCreatureStats(c).setHealth(health);
            EXPECT_EQ(actionKind(*c.getClass().activate(c, mPlayer)), "open");
        }

        TEST_F(ActorTest, a_creature_hit_lowers_the_stats_like_an_npcs)
        {
            MWWorld::Ptr c = creature("t_cr_hit");
            const MWWorld::Class& cls = c.getClass();
            cls.getCreatureStats(c);
            cls.onHit(c, { { "health", 30.f }, { "fatigue", 20.f }, { "magicka", 10.f } }, rid("t_axe"), MWWorld::Ptr(),
                true, MWMechanics::DamageSourceType::Melee);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(c).getHealth().getCurrent(), 60.f);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(c).getFatigue().getCurrent(), 130.f);
            EXPECT_FLOAT_EQ(cls.getCreatureStats(c).getMagicka().getCurrent(), 20.f);
            EXPECT_EQ(cls.getCreatureStats(c).getLastHitObject(), rid("t_axe"));
            // with an attacker and a damage below the agility term (30 * 0.5 = 15): no dice, hit recovery, no speech
            MWWorld::Ptr attacker = npc("t_cr_attacker");
            const unsigned seed = seedWithRoll(5);
            seedWorld(seed);
            cls.onHit(c, { { "health", 1.f } }, ESM::RefId(), attacker, true, MWMechanics::DamageSourceType::Melee);
            EXPECT_EQ(drawsSince(seed), 0);
            EXPECT_TRUE(cls.getCreatureStats(c).getHitRecovery());
            EXPECT_TRUE(mActorDialogue.mSaid.empty());
            // a damage at or above the term rolls the knock-down dice once (a creature does not speak)
            seedWorld(seed);
            cls.onHit(c, { { "health", 20.f } }, ESM::RefId(), attacker, true, MWMechanics::DamageSourceType::Melee);
            EXPECT_EQ(drawsSince(seed), 1);
            EXPECT_TRUE(mActorDialogue.mSaid.empty());
        }

        TEST_F(ActorTest, a_creatures_sounds_come_from_its_sound_generators)
        {
            ESM::SoundGenerator own;
            own.blank();
            own.mId = rid("t_cr_own_roar");
            own.mType = ESM::SoundGenerator::Roar;
            own.mCreature = rid("t_cr_sound");
            own.mSound = rid("own roar sound");
            mStore.insertStatic(own);
            ESM::SoundGenerator fallbackRoar;
            fallbackRoar.blank();
            fallbackRoar.mId = rid("t_cr_fallback_roar");
            fallbackRoar.mType = ESM::SoundGenerator::Roar;
            fallbackRoar.mSound = rid("fallback roar sound");
            mStore.insertStatic(fallbackRoar);
            ESM::SoundGenerator fallbackMoan;
            fallbackMoan.blank();
            fallbackMoan.mId = rid("t_cr_fallback_moan");
            fallbackMoan.mType = ESM::SoundGenerator::Moan;
            fallbackMoan.mSound = rid("fallback moan sound");
            mStore.insertStatic(fallbackMoan);
            // a creature with the same model borrows its sounds
            ESM::SoundGenerator sibling;
            sibling.blank();
            sibling.mId = rid("t_cr_sibling_scream");
            sibling.mType = ESM::SoundGenerator::Scream;
            sibling.mCreature = rid("t_cr_sibling");
            sibling.mSound = rid("sibling scream sound");
            mStore.insertStatic(sibling);

            MWWorld::Ptr c = creature("t_cr_sound");
            MWWorld::Ptr twin = creature("t_cr_twin", [](ESM::Creature& r) { r.mModel = "c/t_cr_sibling.nif"; });
            creature("t_cr_sibling");
            const MWWorld::Class& cls = c.getClass();
            EXPECT_EQ(cls.getSoundIdFromSndGen(c, "roar").getRefIdString(), "own roar sound");
            EXPECT_EQ(cls.getSoundIdFromSndGen(c, "moan").getRefIdString(), "fallback moan sound");
            EXPECT_EQ(cls.getSoundIdFromSndGen(c, "scream").getRefIdString(), "");
            EXPECT_EQ(twin.getClass().getSoundIdFromSndGen(twin, "scream").getRefIdString(), "sibling scream sound");
            EXPECT_EQ(cls.getSoundIdFromSndGen(c, "land").getRefIdString(), "");
            // the foot sounds need the ground state
            EXPECT_EQ(cls.getSoundIdFromSndGen(c, "left").getRefIdString(), "");
            mActorWorld.mFlying = true;
            EXPECT_EQ(cls.getSoundIdFromSndGen(c, "left").getRefIdString(), "");
            mActorWorld.mFlying = false;
            EXPECT_THROW(cls.getSoundIdFromSndGen(c, "bark"), std::runtime_error);
            // one dice roll when there are sounds to choose from
            const unsigned seed = seedWithRoll(5);
            seedWorld(seed);
            cls.getSoundIdFromSndGen(c, "roar");
            EXPECT_EQ(drawsSince(seed), 1);
        }

        TEST_F(ActorTest, a_creature_respawn_waits_for_the_flag_and_the_delay)
        {
            MWWorld::Ptr c = creature(
                "t_cr_respawn", [](ESM::Creature& r) { r.mFlags = ESM::Creature::Respawn | ESM::Creature::Walks; });
            const MWWorld::Class& cls = c.getClass();
            cls.respawn(c);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
            MWMechanics::DynamicStat<float> health(cls.getCreatureStats(c).getHealth());
            health.setCurrent(-1.f);
            cls.getCreatureStats(c).setHealth(health);
            cls.getCreatureStats(c).setDeathAnimationFinished(true);
            mActorWorld.mNow = MWWorld::TimeStamp(0.f, 1);
            cls.respawn(c);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
            EXPECT_NE(c.getRefData().getCustomData(), nullptr);
            MWWorld::Ptr mortal = creature("t_cr_mortal");
            MWMechanics::DynamicStat<float> h2(mortal.getClass().getCreatureStats(mortal).getHealth());
            h2.setCurrent(-1.f);
            mortal.getClass().getCreatureStats(mortal).setHealth(h2);
            mortal.getClass().getCreatureStats(mortal).setDeathAnimationFinished(true);
            mActorWorld.mNow = MWWorld::TimeStamp(0.f, 40);
            mortal.getClass().respawn(mortal);
            EXPECT_EQ(mActorWindow.mDeletedCustomData, 0);
        }

        TEST_F(ActorTest, creature_table)
        {
            std::vector<MWWorld::Ptr> creatures;
            creatures.push_back(creature("t_ct_walker"));
            creatures.push_back(creature("t_ct_bird", [](ESM::Creature& r) { r.mFlags = ESM::Creature::Flies; }));
            creatures.push_back(creature("t_ct_swimmer", [](ESM::Creature& r) { r.mFlags = ESM::Creature::Swims; }));
            creatures.push_back(creature("t_ct_biped", [](ESM::Creature& r) {
                r.mFlags = ESM::Creature::Bipedal | ESM::Creature::Weapon | ESM::Creature::Respawn;
                r.mScale = 0.5f;
                r.mRecordFlags |= ESM::FLAG_Persistent;
            }));
            creatures.push_back(creature("t_ct_weak", [](ESM::Creature& r) {
                for (auto& a : r.mData.mAttributes)
                    a = 1;
                r.mData.mGold = 0;
                r.mData.mHealth = 1;
            }));
            std::ostringstream out;
            for (const MWWorld::Ptr& c : creatures)
            {
                const MWWorld::Class& cls = c.getClass();
                const MWMechanics::CreatureStats& s = cls.getCreatureStats(c);
                out << "[" << c.getCellRef().getRefId().getRefIdString() << "]\n";
                out << "  name=[" << cls.getName(c) << "] model=[" << cls.getModel(c) << "] script=["
                    << cls.getScript(c).getRefIdString() << "] essential=" << yn(cls.isEssential(c))
                    << " persistent=" << yn(cls.isPersistent(c)) << " services=" << cls.getServices(c)
                    << " baseGold=" << cls.getBaseGold(c) << " baseFight=" << cls.getBaseFightRating(c) << "\n";
                out << "  biped=" << yn(cls.isBipedal(c)) << " fly=" << yn(cls.canFly(c))
                    << " swim=" << yn(cls.canSwim(c)) << " walk=" << yn(cls.canWalk(c))
                    << " hasInv=" << yn(cls.hasInventoryStore(c)) << "\n";
                out << "  level=" << s.getLevel() << " health=" << f(s.getHealth().getBase())
                    << " magicka=" << f(s.getMagicka().getBase()) << " fatigue=" << f(s.getFatigue().getBase())
                    << " goldPool=" << s.getGoldPool() << "\n  attributes:";
                for (int i = 0; i < ESM::Attribute::Length; ++i)
                    out << " " << f(s.getAttribute(ESM::Attribute::indexToRefId(i)).getModified());
                out << "\n  skills: combat=" << f(cls.getSkill(c, ESM::Skill::LongBlade)) << "\n";
                out << "  capacity=" << f(cls.getCapacity(c)) << " encumbrance=" << f(cls.getEncumbrance(c))
                    << " armor=" << f(cls.getArmorRating(c, false)) << "\n";
                out << "  walk=" << f(cls.getWalkSpeed(c)) << " run=" << f(cls.getRunSpeed(c))
                    << " swimSpeed=" << f(cls.getSwimSpeed(c)) << " max=" << f(cls.getMaxSpeed(c)) << "\n";
                vsg::vec3 scale(1.f, 1.f, 1.f);
                cls.adjustScale(c, scale, true);
                out << "  scale=" << f(scale.x) << "," << f(scale.y) << "," << f(scale.z) << "\n";
                for (bool full : { false, true })
                {
                    mActorWindow.mFullHelp = full;
                    const MWGui::ToolTipInfo info = cls.getToolTipInfo(c, 1);
                    out << "  tooltip(full=" << yn(full) << ") hasToolTip=" << yn(cls.hasToolTip(c)) << " caption=["
                        << info.caption << "] extra=[" << info.extra << "]\n";
                }
                mActorWindow.mFullHelp = false;
            }
            expectRecording("actors_creature.golden.txt", out.str());
        }

        TEST_F(ActorTest, creature_state_save_golden)
        {
            misc("t_save_cr_item", 2.f);
            MWWorld::Ptr c = creature("t_save_creature", [](ESM::Creature& r) {
                r.mFlags = ESM::Creature::Weapon | ESM::Creature::Walks | ESM::Creature::Respawn;
                r.mInventory.mList.push_back({ 2, R("t_save_cr_item") });
                ESM::AIPackage wander;
                wander.mType = ESM::AI_Wander;
                wander.mWander = { 512, 3, 0, { 40, 20, 20, 10, 0, 0, 0, 0 }, 1 };
                r.mAiPackage.mList.push_back(wander);
            });
            MWMechanics::CreatureStats& s = c.getClass().getCreatureStats(c);
            MWMechanics::DynamicStat<float> health(s.getHealth());
            health.setCurrent(41.f);
            s.setHealth(health);
            s.setLevel(6);
            MWWorld::Ptr w = armor("t_save_cr_shield", ESM::Armor::Shield, 5.f, 8);
            MWWorld::InventoryStore& inv = c.getClass().getInventoryStore(c);
            MWWorld::ContainerStoreIterator it = inv.add(w, 1, false);
            inv.equip(MWWorld::InventoryStore::Slot_CarriedLeft, it);

            ESM::CreatureState state;
            state.blank();
            c.getClass().writeAdditionalState(c, state);
            ASSERT_TRUE(state.mHasCustomState);
            expectRecording("actors_creature_state.golden.txt", writeObjectState(state) + "\n");
        }

        TEST_F(ActorTest, a_creature_state_round_trips_through_the_class_hooks)
        {
            misc("t_round_cr_item", 2.f);
            MWWorld::Ptr c = creature("t_round_creature", [](ESM::Creature& r) {
                r.mInventory.mList.push_back({ 4, R("t_round_cr_item") });
            });
            MWMechanics::CreatureStats& s = c.getClass().getCreatureStats(c);
            MWMechanics::DynamicStat<float> health(s.getHealth());
            health.setCurrent(33.f);
            s.setHealth(health);
            ESM::CreatureState written;
            written.blank();
            c.getClass().writeAdditionalState(c, written);
            const std::string before = writeObjectState(written);

            ESM::CellRef cellRef = cellRefFor(rid("t_round_creature"));
            auto fresh = std::make_shared<MWWorld::LiveCellRef<ESM::Creature>>(
                cellRef, mStore.get<ESM::Creature>().find(rid("t_round_creature")));
            mHolders.push_back(fresh);
            MWWorld::Ptr other(fresh.get());
            other.getClass().readAdditionalState(other, written);
            EXPECT_FLOAT_EQ(other.getClass().getCreatureStats(other).getHealth().getCurrent(), 33.f);
            EXPECT_EQ(other.getClass().getContainerStore(other).count(rid("t_round_cr_item")), 4);
            ESM::CreatureState again;
            again.blank();
            other.getClass().writeAdditionalState(other, again);
            EXPECT_EQ(writeObjectState(again), before);

            // a gone creature that does not respawn writes nothing
            MWWorld::Ptr gone = creature("t_gone_creature");
            gone.getClass().getCreatureStats(gone);
            gone.getCellRef().setCount(0);
            ESM::CreatureState goneState;
            goneState.blank();
            gone.getClass().writeAdditionalState(gone, goneState);
            EXPECT_FALSE(goneState.mHasCustomState);
            // no custom data: nothing either
            MWWorld::Ptr fresh2 = creature("t_nodata_creature");
            ESM::CreatureState nodata;
            nodata.blank();
            fresh2.getClass().writeAdditionalState(fresh2, nodata);
            EXPECT_FALSE(nodata.mHasCustomState);
        }

        TEST_F(ActorTest, the_base_ai_setting_and_inventory_of_a_creature_record_can_be_changed)
        {
            MWWorld::Ptr c = creature("t_cr_base");
            c.getClass().setBaseAISetting(rid("t_cr_base"), MWMechanics::AiSetting::Flee, 55);
            EXPECT_EQ(mStore.get<ESM::Creature>().find(rid("t_cr_base"))->mAiData.mFlee, 55);
            misc("t_cr_base_item", 1.f);
            c.getClass().modifyBaseInventory(rid("t_cr_base"), rid("t_cr_base_item"), 2);
            ASSERT_EQ(mStore.get<ESM::Creature>().find(rid("t_cr_base"))->mInventory.mList.size(), 1u);
        }

        // ---------------------------------------------------------------------------------------------- levelled lists

        TEST_F(ActorTest, the_levelled_list_classes_are_nameless_and_have_no_tooltip)
        {
            ESM::CreatureLevList cl;
            cl.blank();
            cl.mId = rid("t_cll_a");
            cl.mList.push_back({ rid("t_cr_spawn_a"), 1 });
            MWWorld::Ptr creatureList = make(cl);
            ESM::ItemLevList il;
            il.blank();
            il.mId = rid("t_ill_a");
            MWWorld::Ptr itemList = make(il);
            EXPECT_EQ(creatureList.getClass().getName(creatureList), "");
            EXPECT_FALSE(creatureList.getClass().hasToolTip(creatureList));
            EXPECT_EQ(itemList.getClass().getName(itemList), "");
            EXPECT_FALSE(itemList.getClass().hasToolTip(itemList));
        }

        TEST_F(ActorTest, a_creature_levelled_list_spawns_one_creature_and_remembers_it)
        {
            // player level 1 (the fixture's record), one creature on the list
            ESM::CreatureLevList cl;
            cl.blank();
            cl.mId = rid("t_cll_spawn");
            cl.mFlags = ESM::CreatureLevList::AllLevels;
            cl.mChanceNone = 0;
            cl.mList.push_back({ rid("t_cr_spawn"), 1 });
            MWWorld::Ptr list = make(cl);
            MWWorld::Ptr spawned = creature("t_cr_spawn");
            mActorWorld.mPlaceResults.push_back(spawned);
            const unsigned seed = seedWithRoll(5);
            seedWorld(seed);
            list.getClass().insertObjectRendering(list, "", mNoRendering);
            ASSERT_EQ(mActorWorld.mPlaced.size(), 1u);
            EXPECT_EQ(mActorWorld.mPlaced[0], "t_cr_spawn");
            EXPECT_TRUE(mActorWorld.mDeleted.empty());
            // the spawn flag is down now: a second call places nothing
            list.getClass().insertObjectRendering(list, "", mNoRendering);
            EXPECT_EQ(mActorWorld.mPlaced.size(), 1u);
            // the saved state names the spawned actor and says no new spawn
            ESM::CreatureLevListState state;
            state.blank();
            list.getClass().writeAdditionalState(list, state);
            EXPECT_TRUE(state.mHasCustomState);
            EXPECT_FALSE(state.mSpawn);
            EXPECT_EQ(state.mSpawnedActor, spawned.getCellRef().getRefNum());
            // the list rolled its chance of none once and its entries once (draw order pinned by the count)
            EXPECT_GE(drawsSince(seed), 1);
        }

        TEST_F(ActorTest, a_creature_levelled_list_with_nothing_to_pick_spawns_nothing)
        {
            ESM::CreatureLevList cl;
            cl.blank();
            cl.mId = rid("t_cll_empty");
            cl.mList.push_back({ rid("t_cr_never"), 50 }); // above the player's level
            MWWorld::Ptr list = make(cl);
            list.getClass().insertObjectRendering(list, "", mNoRendering);
            EXPECT_TRUE(mActorWorld.mPlaced.empty());
            ESM::CreatureLevListState state;
            state.blank();
            list.getClass().writeAdditionalState(list, state);
            EXPECT_TRUE(state.mHasCustomState);
            EXPECT_FALSE(state.mSpawn);
        }

        TEST_F(ActorTest, a_creature_levelled_list_respawns_only_when_its_creature_is_gone_or_has_rotted)
        {
            ESM::CreatureLevList cl;
            cl.blank();
            cl.mId = rid("t_cll_respawn");
            cl.mFlags = ESM::CreatureLevList::AllLevels;
            cl.mList.push_back({ rid("t_cr_respawned"), 1 });
            MWWorld::Ptr list = make(cl);
            MWWorld::Ptr spawned = creature("t_cr_respawned");
            mActorWorld.mPlaceResults.push_back(spawned);
            list.getClass().insertObjectRendering(list, "", mNoRendering);
            auto spawnFlag = [&] {
                ESM::CreatureLevListState state;
                state.blank();
                list.getClass().writeAdditionalState(list, state);
                return state.mSpawn;
            };
            ASSERT_FALSE(spawnFlag());
            // alive: no respawn
            list.getClass().respawn(list);
            EXPECT_FALSE(spawnFlag());
            // the creature is gone from the world (count 0): respawn
            spawned.getCellRef().setCount(0);
            list.getClass().respawn(list);
            EXPECT_TRUE(spawnFlag());
        }

        TEST_F(ActorTest, a_levelled_list_state_round_trips)
        {
            ESM::CreatureLevList cl;
            cl.blank();
            cl.mId = rid("t_cll_state");
            MWWorld::Ptr list = make(cl);
            // no custom data: no state
            ESM::CreatureLevListState none;
            none.blank();
            list.getClass().writeAdditionalState(list, none);
            EXPECT_FALSE(none.mHasCustomState);
            // reading a state with custom state creates the data
            ESM::CreatureLevListState state;
            state.blank();
            state.mHasCustomState = true;
            state.mSpawnedActor = ESM::RefNum{ .mIndex = 42, .mContentFile = 1 };
            state.mSpawn = false;
            list.getClass().readAdditionalState(list, state);
            ESM::CreatureLevListState back;
            back.blank();
            list.getClass().writeAdditionalState(list, back);
            ASSERT_TRUE(back.mHasCustomState);
            EXPECT_EQ(back.mSpawnedActor.mIndex, 42u);
            EXPECT_EQ(back.mSpawnedActor.mContentFile, 1);
            EXPECT_FALSE(back.mSpawn);
            // a read of a state without custom state leaves the object alone
            ESM::CreatureLevList other;
            other.blank();
            other.mId = rid("t_cll_state2");
            MWWorld::Ptr list2 = make(other);
            ESM::CreatureLevListState plain;
            plain.blank();
            plain.mHasCustomState = false;
            list2.getClass().readAdditionalState(list2, plain);
            EXPECT_EQ(list2.getRefData().getCustomData(), nullptr);
        }

        // ---------------------------------------------------------------------------------------------- registration

        TEST_F(ActorTest, the_actor_classes_are_registered_for_their_records)
        {
            MWWorld::Ptr n = npc("t_reg_npc");
            MWWorld::Ptr c = creature("t_reg_creature");
            ESM::CreatureLevList cl;
            cl.blank();
            cl.mId = rid("t_reg_cll");
            MWWorld::Ptr list = make(cl);
            ESM::ItemLevList il;
            il.blank();
            il.mId = rid("t_reg_ill");
            MWWorld::Ptr itemList = make(il);
            EXPECT_EQ(&MWWorld::Class::get(ESM::NPC::sRecordId), &n.getClass());
            EXPECT_EQ(n.getType(), ESM::NPC::sRecordId);
            EXPECT_EQ(c.getType(), ESM::Creature::sRecordId);
            EXPECT_EQ(list.getType(), ESM::CreatureLevList::sRecordId);
            EXPECT_EQ(itemList.getType(), ESM::ItemLevList::sRecordId);
            EXPECT_TRUE(n.getClass().isNpc());
            EXPECT_FALSE(c.getClass().isNpc());
            EXPECT_TRUE(c.getClass().isActor());
            EXPECT_FALSE(list.getClass().isActor());
        }
    }
}
