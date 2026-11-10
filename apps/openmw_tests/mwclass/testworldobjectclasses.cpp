// Task 0098, move 8, stage 8a: characterization tests of the world-object classes (door, container, activator, static,
// body part). They run the REAL MWClass::Door, Container, Activator, Static and BodyPart classes (registered the way
// the game registers them) on records in an in-memory ESMStore, behind test doubles of the MWBase managers, and pin
// what the classes answer and which actions they choose, so that moving the classes into the Morrowind pack cannot
// change a result. Written against the OLD code (apps/openmw/mwclass) first; the file is not edited by the move.
//
// Records and numbers are TEST INPUTS, not claimed to be Morrowind's data.
#include <algorithm>
#include <cstring>
#include <sstream>

#include <gtest/gtest.h>

#include <components/esm3/containerstate.hpp>
#include <components/esm3/doorstate.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadacti.hpp>
#include <components/esm3/loadbody.hpp>
#include <components/esm3/loadcont.hpp>
#include <components/esm3/loadcrea.hpp>
#include <components/esm3/loaddoor.hpp>
#include <components/esm3/loadmisc.hpp>
#include <components/esm3/loadsndg.hpp>
#include <components/esm3/loadstat.hpp>

#include "apps/openmw/mwbase/soundmanager.hpp"
#include "apps/openmw/mwclass/activator.hpp"
#include "apps/openmw/mwclass/bodypart.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/door.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwworld/actiondoor.hpp"
#include "apps/openmw/mwworld/actionharvest.hpp"
#include "apps/openmw/mwworld/actionopen.hpp"
#include "apps/openmw/mwworld/actionteleport.hpp"
#include "apps/openmw/mwworld/actiontrap.hpp"
#include "apps/openmw/mwworld/failedaction.hpp"
#include "apps/openmw/mwworld/localscripts.hpp"
#include "apps/openmw/mwworld/nullaction.hpp"

#include "apps/openmw_tests/mwmechanics/rulesfixture.hpp"

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4100)
#endif

// A namespace of its own: CraftingTest declares a struct Sound that the generated double would find instead of
// MWSound::Sound.
namespace WorldObjectDoubles
{
    using namespace MWBase;
    using CraftingTest::unexpectedCall;
#include "apps/openmw_tests/mwmechanics/fakes/soundmanager.inc"
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
        // Recorded from the old code (apps/openmw/mwclass) before the move: the whole stream of one saved object.
        const char* const kDoorStateGolden
            = "54455333340100000000000000000000484544522c01000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000046524d520800000000000000ffffffff4e414d450100000000444154411800000000000000000000"
              "0000000000000000000000000000000000454e41420100000000414e494d0400000001000000";
        const char* const kContainerStateGolden
            = "54455333340100000000000000000000484544522c01000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000000"
              "00000000000000000000000046524d520800000000000000ffffffff4e414d450100000000444154411800000000000000000000"
              "0000000000000000000000000000000000454e4142010000000049434e54040000000100000046524d520800000002000000ffff"
              "ffff4e414d450700000072696e675f65004e414d390400000004000000";

        class WorldObjectSound : public WorldObjectDoubles::GeneratedSoundManagerBase
        {
        public:
            std::vector<std::string> mPlayed3D;
            std::vector<std::string> mFaded;

            MWSound::Sound* playSound3D(const MWWorld::ConstPtr& reference, const ESM::RefId& soundId, float volume,
                float pitch, MWSound::Type type, MWSound::PlayMode mode, float offset) override
            {
                mPlayed3D.push_back(soundId.getRefIdString());
                return nullptr;
            }
            MWSound::Sound* playSound3D(const vsg::vec3& initialPos, const ESM::RefId& soundId, float volume,
                float pitch, MWSound::Type type, MWSound::PlayMode mode, float offset) override
            {
                mPlayed3D.push_back(soundId.getRefIdString());
                return nullptr;
            }
            void fadeOutSound3D(const MWWorld::ConstPtr& reference, const ESM::RefId& soundId, float duration) override
            {
                mFaded.push_back(soundId.getRefIdString());
            }
        };

        class WorldObjectWorld : public FakeWorld
        {
        public:
            float mDistanceToFocus = 0.f;
            float mMaxActivationDistance = 100.f;
            MWWorld::LocalScripts* mLocalScripts = nullptr;
            MWWorld::LocalScripts& getLocalScripts() override { return *mLocalScripts; }
            int mContainerScriptsAdded = 0;
            int mContainerScriptsRemoved = 0;

            float getDistanceToFocusObject() override { return mDistanceToFocus; }
            float getMaxActivationDistance() const override { return mMaxActivationDistance; }
            void addContainerScripts(const MWWorld::Ptr&, MWWorld::CellStore*) override { ++mContainerScriptsAdded; }
            void removeContainerScripts(const MWWorld::Ptr&) override { ++mContainerScriptsRemoved; }
            void activateDoor(const MWWorld::Ptr&) override { ++mDoorActivations; }
            int mDoorActivations = 0;
            MWAnim::Object* getAnimation(const MWWorld::Ptr&) override { return nullptr; }
            const MWAnim::Object* getAnimation(const MWWorld::ConstPtr&) const override { return nullptr; }
        };

        class WorldObjectLua : public FakeLuaManager
        {
        public:
            void saveLocalScripts(const MWWorld::Ptr&, ESM::LuaScripts&) override {}
            void loadLocalScripts(const MWWorld::Ptr&, const ESM::LuaScripts&) override {}
        };

        class WorldObjectWindow : public FakeWindowManager
        {
        public:
            bool mInventoryAllowed = true;
            bool isAllowed(MWGui::GuiWindow wnd) const override { return mInventoryAllowed; }
            bool getFullHelp() const override { return false; }
            bool isGuiMode() const override { return false; }
        };

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

        class WorldObjectTest : public RulesTest
        {
        protected:
            void SetUp() override
            {
                RulesTest::SetUp();
                MWClass::Door::registerSelf();
                MWClass::Activator::registerSelf();
                MWClass::Static::registerSelf();
                MWClass::BodyPart::registerSelf();
                mLocalScripts = std::make_unique<MWWorld::LocalScripts>(mStore);
                mObjectWorld.mLocalScripts = mLocalScripts.get();
                mObjectWorld.mStore = &mStore;
                mObjectWorld.mPlayer = mPlayer;
                mEnvironment->setWorld(mObjectWorld);
                mEnvironment->setWindowManager(mObjectWindow);
                mEnvironment->setSoundManager(mSound);
                mEnvironment->setLuaManager(mObjectLua);
            }

            void TearDown() override
            {
                mLocalScripts.reset();
                mDoorRefs.clear();
                mActivatorRefs.clear();
                mStaticRefs.clear();
                mBodyRefs.clear();
                mContainerRefs.clear();
                RulesTest::TearDown();
            }

            ESM::CellRef cellRefFor(const ESM::RefId& id)
            {
                ESM::CellRef cellRef;
                cellRef.blank();
                cellRef.mRefID = id;
                cellRef.mRefNum = ESM::RefNum{ .mIndex = mNextIndex++, .mContentFile = 0 };
                return cellRef;
            }

            MWWorld::Ptr makeDoor(const std::string& id, bool teleport = false, int lockLevel = 0,
                const std::string& key = "", const std::string& trap = "")
            {
                ESM::Door record;
                record.mId = rid(id);
                record.mName = "Door " + id;
                record.mModel = "d/" + id + ".nif";
                record.mScript = rid(id + "_script");
                record.mOpenSound = rid("open_" + id);
                record.mCloseSound = rid("close_" + id);
                const ESM::Door* stored = mStore.insertStatic(record);
                ESM::CellRef cellRef = cellRefFor(stored->mId);
                cellRef.mTeleport = teleport;
                if (!key.empty())
                    cellRef.mKey = rid(key);
                mDoorRefs.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::Door>>(cellRef, stored));
                MWWorld::Ptr ptr(mDoorRefs.back().get());
                if (lockLevel != 0)
                    ptr.getCellRef().lock(lockLevel);
                if (!trap.empty())
                    ptr.getCellRef().setTrap(rid(trap));
                return ptr;
            }

            MWWorld::Ptr makeContainer(const std::string& id, int flags = 0, int lockLevel = 0,
                const std::string& key = "", const std::string& trap = "",
                const std::vector<std::pair<std::string, int>>& contents = {})
            {
                ESM::Container record;
                record.blank();
                record.mId = rid(id);
                record.mName = "Chest " + id;
                record.mModel = "c/" + id + ".nif";
                record.mScript = rid(id + "_script");
                record.mWeight = 77.f;
                record.mFlags = flags;
                for (const auto& [item, count] : contents)
                    record.mInventory.mList.push_back({ count, rid(item) });
                const ESM::Container* stored = mStore.insertStatic(record);
                ESM::CellRef cellRef = cellRefFor(stored->mId);
                if (!key.empty())
                    cellRef.mKey = rid(key);
                mContainerRefs.push_back(std::make_unique<MWWorld::LiveCellRef<ESM::Container>>(cellRef, stored));
                MWWorld::Ptr ptr(mContainerRefs.back().get());
                if (lockLevel != 0)
                    ptr.getCellRef().lock(lockLevel);
                if (!trap.empty())
                    ptr.getCellRef().setTrap(rid(trap));
                return ptr;
            }

            MWWorld::Ptr makeActivator(const std::string& id, const std::string& name, const std::string& model = "")
            {
                ESM::Activator record;
                record.mId = rid(id);
                record.mName = name;
                record.mModel = model;
                record.mScript = rid(id + "_script");
                const ESM::Activator* stored = mStore.insertStatic(record);
                mActivatorRefs.push_back(
                    std::make_unique<MWWorld::LiveCellRef<ESM::Activator>>(cellRefFor(stored->mId), stored));
                return MWWorld::Ptr(mActivatorRefs.back().get());
            }

            MWWorld::Ptr makeStatic(const std::string& id, const std::string& model)
            {
                ESM::Static record;
                record.mId = rid(id);
                record.mModel = model;
                const ESM::Static* stored = mStore.insertStatic(record);
                mStaticRefs.push_back(
                    std::make_unique<MWWorld::LiveCellRef<ESM::Static>>(cellRefFor(stored->mId), stored));
                return MWWorld::Ptr(mStaticRefs.back().get());
            }

            MWWorld::Ptr makeBodyPart(const std::string& id, const std::string& model)
            {
                ESM::BodyPart record;
                record.mId = rid(id);
                record.mModel = model;
                const ESM::BodyPart* stored = mStore.insertStatic(record);
                mBodyRefs.push_back(
                    std::make_unique<MWWorld::LiveCellRef<ESM::BodyPart>>(cellRefFor(stored->mId), stored));
                return MWWorld::Ptr(mBodyRefs.back().get());
            }

            static std::string kind(const MWWorld::Action& action)
            {
                if (dynamic_cast<const MWWorld::ActionDoor*>(&action))
                    return "door";
                if (dynamic_cast<const MWWorld::ActionTeleport*>(&action))
                    return "teleport";
                if (dynamic_cast<const MWWorld::ActionTrap*>(&action))
                    return "trap";
                if (dynamic_cast<const MWWorld::ActionOpen*>(&action))
                    return "open";
                if (dynamic_cast<const MWWorld::ActionHarvest*>(&action))
                    return "harvest";
                if (dynamic_cast<const MWWorld::FailedAction*>(&action))
                    return "failed";
                if (dynamic_cast<const MWWorld::NullAction*>(&action))
                    return "null";
                return "other";
            }

            /// The sound an action plays when it is executed by  actor ("" for none). Only for the actions whose
            /// execution needs nothing but the sound manager (a refusal, an animated door).
            std::string soundOfExecution(MWWorld::Action& action, const MWWorld::Ptr& actor)
            {
                mSound.mPlayed3D.clear();
                action.execute(actor);
                return mSound.mPlayed3D.empty() ? std::string() : mSound.mPlayed3D.back();
            }

            std::string doorActivation(
                const MWWorld::Ptr& door, const MWWorld::Ptr& actor, std::string* sound = nullptr)
            {
                auto action = door.getClass().activate(door, actor);
                const std::string result = kind(*action);
                if (sound)
                    *sound = (result == "door" || result == "failed") ? soundOfExecution(*action, actor) : "";
                return result;
            }

            template <class State>
            std::string writeObjectState(State state)
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

            std::unique_ptr<MWWorld::LocalScripts> mLocalScripts;
            WorldObjectWorld mObjectWorld;
            WorldObjectWindow mObjectWindow;
            WorldObjectSound mSound;
            WorldObjectLua mObjectLua;
            uint32_t mNextIndex = 500;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Door>>> mDoorRefs;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Container>>> mContainerRefs;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Activator>>> mActivatorRefs;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::Static>>> mStaticRefs;
            std::vector<std::unique_ptr<MWWorld::LiveCellRef<ESM::BodyPart>>> mBodyRefs;
        };

        // ---------------------------------------------------------------------------------------------- doors

        TEST_F(WorldObjectTest, a_door_answers_its_record)
        {
            MWWorld::Ptr door = makeDoor("door_a");
            const MWWorld::Class& cls = door.getClass();
            EXPECT_TRUE(cls.isDoor());
            EXPECT_TRUE(cls.useAnim());
            EXPECT_TRUE(cls.canLock(door));
            EXPECT_EQ(cls.getName(door), "Door door_a");
            EXPECT_EQ(cls.getModel(door), "d/door_a.nif");
            EXPECT_EQ(cls.getScript(door), rid("door_a_script"));
        }

        TEST_F(WorldObjectTest, an_unlocked_animated_door_opens)
        {
            MWWorld::Ptr door = makeDoor("door_b");
            EXPECT_EQ(doorActivation(door, mPlayer), "door");
        }

        TEST_F(WorldObjectTest, a_locked_door_without_a_key_refuses_with_the_locked_sound)
        {
            MWWorld::Ptr door = makeDoor("door_c", false, 30);
            std::string sound;
            EXPECT_EQ(doorActivation(door, mPlayer, &sound), "failed");
            EXPECT_EQ(sound, "LockedDoor");
            EXPECT_TRUE(door.getCellRef().isLocked());
        }

        TEST_F(WorldObjectTest, the_key_unlocks_the_door_says_so_and_opens_it)
        {
            addMisc("key_d", 1);
            give(rid("key_d"), 1);
            MWWorld::Ptr door = makeDoor("door_d", false, 30, "key_d");
            EXPECT_EQ(doorActivation(door, mPlayer), "door");
            EXPECT_FALSE(door.getCellRef().isLocked());
            ASSERT_EQ(mObjectWindow.mMessages.size(), 1u);
            EXPECT_EQ(mObjectWindow.mMessages[0], "key_d #{sKeyUsed}");
        }

        TEST_F(WorldObjectTest, the_key_also_disarms_the_trap_with_a_sound)
        {
            addMisc("key_e", 1);
            give(rid("key_e"), 1);
            MWWorld::Ptr door = makeDoor("door_e", false, 30, "key_e", "trap_spell");
            EXPECT_EQ(doorActivation(door, mPlayer), "door");
            EXPECT_TRUE(door.getCellRef().getTrap().empty());
            ASSERT_EQ(mSound.mPlayed3D.size(), 1u);
            EXPECT_EQ(mSound.mPlayed3D[0], "Disarm Trap");
        }

        TEST_F(WorldObjectTest, an_unlocked_trapped_door_springs_the_trap)
        {
            MWWorld::Ptr door = makeDoor("door_f", false, 0, "", "trap_spell");
            EXPECT_EQ(doorActivation(door, mPlayer), "trap");
        }

        TEST_F(WorldObjectTest, a_teleport_door_takes_the_player_and_refuses_an_npc)
        {
            MWWorld::Ptr door = makeDoor("door_g", true);
            std::string sound;
            EXPECT_EQ(doorActivation(door, mPlayer), "teleport");
            MWWorld::Ptr npc = makeNpc("walker");
            EXPECT_EQ(doorActivation(door, npc, &sound), "failed");
            EXPECT_EQ(sound, "LockedDoor");
        }

        TEST_F(WorldObjectTest, a_teleport_door_used_by_telekinesis_fails)
        {
            MWWorld::Ptr door = makeDoor("door_h", true);
            mObjectWorld.mDistanceToFocus = 500.f;
            EXPECT_EQ(doorActivation(door, mPlayer), "failed");
        }

        TEST_F(WorldObjectTest, telekinesis_is_allowed_unless_the_door_is_a_plain_teleport)
        {
            MWWorld::Ptr first = makeDoor("door_i");
            const MWWorld::Class& cls = first.getClass();
            EXPECT_TRUE(cls.allowTelekinesis(first));
            EXPECT_FALSE(cls.allowTelekinesis(makeDoor("door_k", true)));
            EXPECT_TRUE(cls.allowTelekinesis(makeDoor("door_l", true, 20)));
            EXPECT_TRUE(cls.allowTelekinesis(makeDoor("door_m", true, 0, "", "trap_spell")));
        }

        TEST_F(WorldObjectTest, an_opening_door_closes_when_activated_and_an_idle_one_opens)
        {
            MWWorld::Ptr door = makeDoor("door_n");
            std::string sound;
            doorActivation(door, mPlayer, &sound);
            EXPECT_EQ(sound, "open_door_n");
            EXPECT_EQ(mSound.mFaded.back(), "close_door_n");
            door.getClass().setDoorState(door, MWWorld::DoorState::Opening);
            doorActivation(door, mPlayer, &sound);
            EXPECT_EQ(sound, "close_door_n");
            EXPECT_EQ(mSound.mFaded.back(), "open_door_n");
        }

        TEST_F(WorldObjectTest, the_door_state_defaults_to_idle_is_kept_and_a_teleport_door_cannot_move)
        {
            MWWorld::Ptr door = makeDoor("door_o");
            EXPECT_EQ(door.getClass().getDoorState(door), MWWorld::DoorState::Idle);
            door.getClass().setDoorState(door, MWWorld::DoorState::Closing);
            EXPECT_EQ(door.getClass().getDoorState(door), MWWorld::DoorState::Closing);
            MWWorld::Ptr teleport = makeDoor("door_p", true);
            EXPECT_THROW(teleport.getClass().setDoorState(teleport, MWWorld::DoorState::Opening), std::runtime_error);
        }

        TEST_F(WorldObjectTest, the_door_state_round_trips_through_the_object_state_with_golden_bytes)
        {
            MWWorld::Ptr door = makeDoor("door_q");
            ESM::DoorState idle;
            door.getClass().writeAdditionalState(door, idle);
            EXPECT_FALSE(idle.mHasCustomState);

            door.getClass().setDoorState(door, MWWorld::DoorState::Opening);
            ESM::DoorState written;
            door.getClass().writeAdditionalState(door, written);
            EXPECT_EQ(written.mDoorState, static_cast<int>(MWWorld::DoorState::Opening));

            MWWorld::Ptr other = makeDoor("door_r");
            ESM::DoorState state;
            state.mHasCustomState = true;
            state.mDoorState = static_cast<int>(MWWorld::DoorState::Closing);
            other.getClass().readAdditionalState(other, state);
            EXPECT_EQ(other.getClass().getDoorState(other), MWWorld::DoorState::Closing);

            ESM::DoorState without;
            without.mHasCustomState = false;
            MWWorld::Ptr third = makeDoor("door_s");
            third.getClass().readAdditionalState(third, without);
            EXPECT_EQ(third.getClass().getDoorState(third), MWWorld::DoorState::Idle);

            written.mHasCustomState = true;
            EXPECT_EQ(writeObjectState(written), kDoorStateGolden);
        }

        // ------------------------------------------------------------------------------------------ containers

        TEST_F(WorldObjectTest, a_container_answers_its_record)
        {
            addMisc("ring_c", 10);
            MWWorld::Ptr chest = makeContainer("chest_a", 0, 0, "", "", { { "ring_c", 3 } });
            const MWWorld::Class& cls = chest.getClass();
            EXPECT_EQ(cls.getName(chest), "Chest chest_a");
            EXPECT_EQ(cls.getModel(chest), "c/chest_a.nif");
            EXPECT_EQ(cls.getScript(chest), rid("chest_a_script"));
            EXPECT_FLOAT_EQ(cls.getCapacity(chest), 77.f);
            EXPECT_TRUE(cls.useAnim());
            EXPECT_TRUE(cls.canLock(chest));
            EXPECT_TRUE(cls.hasToolTip(chest));
            EXPECT_EQ(cls.getContainerStore(chest).count(rid("ring_c")), 3);
            EXPECT_FLOAT_EQ(cls.getEncumbrance(chest), 3.f);
            EXPECT_EQ(mObjectWorld.mContainerScriptsAdded, 1);
        }

        TEST_F(WorldObjectTest, an_organic_container_cannot_be_locked)
        {
            MWWorld::Ptr plant = makeContainer("plant_a", ESM::Container::Organic);
            EXPECT_FALSE(plant.getClass().canLock(plant));
        }

        TEST_F(WorldObjectTest, an_open_container_opens_and_a_locked_one_refuses)
        {
            MWWorld::Ptr chest = makeContainer("chest_b");
            EXPECT_EQ(kind(*chest.getClass().activate(chest, mPlayer)), "open");

            MWWorld::Ptr locked = makeContainer("chest_c", 0, 40);
            auto action = locked.getClass().activate(locked, mPlayer);
            EXPECT_EQ(kind(*action), "failed");
            EXPECT_EQ(soundOfExecution(*action, mPlayer), "LockedChest");
        }

        TEST_F(WorldObjectTest, the_key_opens_a_locked_container_and_disarms_its_trap)
        {
            addMisc("key_f", 1);
            give(rid("key_f"), 1);
            MWWorld::Ptr chest = makeContainer("chest_d", 0, 40, "key_f", "trap_spell");
            EXPECT_EQ(kind(*chest.getClass().activate(chest, mPlayer)), "open");
            EXPECT_FALSE(chest.getCellRef().isLocked());
            EXPECT_TRUE(chest.getCellRef().getTrap().empty());
            ASSERT_EQ(mObjectWindow.mMessages.size(), 1u);
            EXPECT_EQ(mObjectWindow.mMessages[0], "key_f #{sKeyUsed}");
            ASSERT_EQ(mSound.mPlayed3D.size(), 1u);
            EXPECT_EQ(mSound.mPlayed3D[0], "Disarm Trap");
        }

        TEST_F(WorldObjectTest, a_trapped_container_springs_the_trap)
        {
            MWWorld::Ptr chest = makeContainer("chest_e", 0, 0, "", "trap_spell");
            auto action = chest.getClass().activate(chest, mPlayer);
            EXPECT_EQ(kind(*action), "trap");
        }

        TEST_F(WorldObjectTest, a_container_does_nothing_while_the_inventory_is_not_allowed)
        {
            MWWorld::Ptr chest = makeContainer("chest_f");
            mObjectWindow.mInventoryAllowed = false;
            EXPECT_EQ(kind(*chest.getClass().activate(chest, mPlayer)), "null");
        }

        TEST_F(WorldObjectTest, a_werewolf_is_refused_by_a_container_and_an_activator)
        {
            MWWorld::Ptr chest = makeContainer("chest_g");
            MWWorld::Ptr acti = makeActivator("acti_w", "Lever");
            mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(true);
            EXPECT_EQ(kind(*chest.getClass().activate(chest, mPlayer)), "failed");
            EXPECT_EQ(kind(*acti.getClass().activate(acti, mPlayer)), "failed");
            mPlayer.getClass().getNpcStats(mPlayer).setWerewolf(false);
        }

        TEST_F(WorldObjectTest, respawn_clears_only_a_touched_respawning_container)
        {
            addMisc("ring_d", 10);
            MWWorld::Ptr respawning = makeContainer("chest_h", ESM::Container::Respawn, 0, "", "", { { "ring_d", 2 } });
            MWWorld::Ptr fixed = makeContainer("chest_i", 0, 0, "", "", { { "ring_d", 2 } });
            respawning.getClass().respawn(respawning);
            EXPECT_EQ(mObjectWorld.mContainerScriptsRemoved, 0); // never touched: no custom data
            respawning.getClass().getContainerStore(respawning);
            fixed.getClass().getContainerStore(fixed);
            respawning.getClass().respawn(respawning);
            fixed.getClass().respawn(fixed);
            EXPECT_EQ(mObjectWorld.mContainerScriptsRemoved, 1);
            EXPECT_EQ(respawning.getRefData().getCustomData(), nullptr);
            EXPECT_NE(fixed.getRefData().getCustomData(), nullptr);
        }

        TEST_F(WorldObjectTest, the_container_inventory_round_trips_through_the_object_state)
        {
            addMisc("ring_e", 10);
            MWWorld::Ptr chest = makeContainer("chest_j", 0, 0, "", "", { { "ring_e", 4 } });
            ESM::ContainerState untouched;
            chest.getClass().writeAdditionalState(chest, untouched);
            EXPECT_FALSE(untouched.mHasCustomState);

            {
                MWWorld::ContainerStore& store = chest.getClass().getContainerStore(chest);
                EXPECT_EQ(store.remove(rid("ring_e"), 0), 0); // removing resolves the store first
            }
            ESM::ContainerState written;
            chest.getClass().writeAdditionalState(chest, written);
            ASSERT_EQ(written.mInventory.mItems.size(), 1u);
            EXPECT_EQ(written.mInventory.mItems[0].mRef.mCount, 4);

            written.mHasCustomState = true;
            MWWorld::Ptr other = makeContainer("chest_k");
            other.getClass().readAdditionalState(other, written);
            EXPECT_EQ(other.getClass().getContainerStore(other).count(rid("ring_e")), 4);

            ESM::ContainerState none;
            none.mHasCustomState = false;
            MWWorld::Ptr third = makeContainer("chest_l");
            third.getClass().readAdditionalState(third, none);
            EXPECT_EQ(third.getRefData().getCustomData(), nullptr);

            EXPECT_EQ(writeObjectState(written), kContainerStateGolden);
        }

        // ------------------------------------------------------------------------- activators, statics, bodies

        TEST_F(WorldObjectTest, an_activator_answers_its_record_and_hides_an_empty_tooltip)
        {
            MWWorld::Ptr lever = makeActivator("acti_a", "Lever", "a/lever.nif");
            MWWorld::Ptr unnamed = makeActivator("acti_b", "");
            const MWWorld::Class& cls = lever.getClass();
            EXPECT_TRUE(cls.isActivator());
            EXPECT_TRUE(cls.useAnim());
            EXPECT_EQ(cls.getName(lever), "Lever");
            EXPECT_EQ(cls.getModel(lever), "a/lever.nif");
            EXPECT_EQ(cls.getScript(lever), rid("acti_a_script"));
            EXPECT_TRUE(cls.hasToolTip(lever));
            EXPECT_FALSE(cls.hasToolTip(unnamed));
            EXPECT_EQ(kind(*cls.activate(lever, mPlayer)), "null");
        }

        TEST_F(WorldObjectTest, an_activator_sound_generator_follows_the_creature_with_the_same_model)
        {
            ESM::Creature creature;
            creature.blank();
            creature.mId = rid("beast_a");
            creature.mModel = "c/beast.nif";
            mStore.insertStatic(creature);
            auto addGen = [&](const char* id, int type, const char* owner, const char* sound) {
                ESM::SoundGenerator gen;
                gen.mId = rid(id);
                gen.mType = type;
                gen.mCreature = owner[0] ? rid(owner) : ESM::RefId();
                gen.mSound = rid(sound);
                mStore.insertStatic(gen);
            };
            addGen("gen_own", ESM::SoundGenerator::Roar, "beast_a", "roar_own");
            addGen("gen_default", ESM::SoundGenerator::Roar, "", "roar_default");
            addGen("gen_moan", ESM::SoundGenerator::Moan, "", "moan_default");

            MWWorld::Ptr beast = makeActivator("acti_c", "Beast", "C/Beast.nif");
            EXPECT_EQ(beast.getClass().getSoundIdFromSndGen(beast, "roar"), rid("roar_own"));
            EXPECT_EQ(beast.getClass().getSoundIdFromSndGen(beast, "moan"), rid("moan_default"));
            EXPECT_EQ(beast.getClass().getSoundIdFromSndGen(beast, "scream"), ESM::RefId());

            MWWorld::Ptr stranger = makeActivator("acti_d", "Stranger", "x/other.nif");
            EXPECT_EQ(stranger.getClass().getSoundIdFromSndGen(stranger, "roar"), rid("roar_default"));
            EXPECT_THROW(stranger.getClass().getSoundIdFromSndGen(stranger, "bark"), std::runtime_error);
        }

        TEST_F(WorldObjectTest, a_static_and_a_body_part_have_a_model_and_no_name_or_tooltip)
        {
            MWWorld::Ptr rock = makeStatic("stat_a", "s/rock.nif");
            EXPECT_EQ(rock.getClass().getModel(rock), "s/rock.nif");
            EXPECT_TRUE(rock.getClass().getName(rock).empty());
            EXPECT_FALSE(rock.getClass().hasToolTip(rock));

            MWWorld::Ptr hand = makeBodyPart("body_a", "b/hand.nif");
            EXPECT_EQ(hand.getClass().getModel(hand), "b/hand.nif");
            EXPECT_TRUE(hand.getClass().getName(hand).empty());
            EXPECT_FALSE(hand.getClass().hasToolTip(hand));
        }

        TEST_F(WorldObjectTest, the_classes_are_registered_under_their_record_types)
        {
            EXPECT_TRUE(typeid(makeDoor("door_z").getClass()) == typeid(MWClass::Door));
            EXPECT_TRUE(typeid(makeContainer("chest_z").getClass()) == typeid(MWClass::Container));
            EXPECT_TRUE(typeid(makeActivator("acti_z", "Z").getClass()) == typeid(MWClass::Activator));
            EXPECT_TRUE(typeid(makeStatic("stat_z", "z.nif").getClass()) == typeid(MWClass::Static));
            EXPECT_TRUE(typeid(makeBodyPart("body_z", "z.nif").getClass()) == typeid(MWClass::BodyPart));
        }
    }
}
