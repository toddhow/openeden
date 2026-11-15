// Task 0177 (neutral world saves, stage N6): what the engine does around the pure blob of testneutralsave.cpp.
// Written first; before the code the first test did not compile (no allowNeutralStubWrite) and the second pinned the
// refusal that must stay when the setting is off.
#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadnpc.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutrallock.hpp"
#include "apps/openmw/mwworld/neutralsave.hpp"
#include "apps/openmw/mwworld/neutralstub.hpp"

namespace
{
    std::string writeStore(const MWWorld::ESMStore& store)
    {
        std::stringstream stream;
        ESM::ESMWriter writer;
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.save(stream);
        Loading::Listener listener;
        store.write(writer, listener);
        writer.close();
        return stream.str();
    }

    TEST(NeutralSaveWorld, aStoreWithTheStubPlayerIsWrittenOnlyWhenNeutralSavesAllowIt)
    {
        MWWorld::ESMStore store;
        ASSERT_TRUE(MWWorld::insertNeutralStubPlayer(store));
        store.setUp();
        store.movePlayerRecord();
        EXPECT_THROW((void)writeStore(store), std::runtime_error) << "the default is the refusal of task 0105";
        store.allowNeutralStubWrite();
        std::string bytes;
        ASSERT_NO_THROW(bytes = writeStore(store));
        EXPECT_NE(bytes.find("NPC_"), std::string::npos) << "the player record is part of the save";
    }

    TEST(NeutralSaveWorld, theRecordTypeIsNotOneOfTheSaveRecordsThatAlreadyExist)
    {
        EXPECT_NE(MWWorld::kNeutralSaveRecord, static_cast<unsigned int>(ESM::REC_GPCH));
        EXPECT_NE(MWWorld::kNeutralSaveRecord, static_cast<unsigned int>(ESM::REC_NEUT));
        EXPECT_NE(MWWorld::kNeutralSaveRecord, static_cast<unsigned int>(ESM::REC_PLAY));
        EXPECT_NE(MWWorld::kNeutralSaveRecord, static_cast<unsigned int>(ESM::REC_CSTA));
        EXPECT_EQ(MWWorld::kNeutralSaveRecord, ESM::esm3Recname("NWLD"));
    }

    TEST(NeutralSaveWorld, theLockStateListsWhatItHoldsInOrder)
    {
        MWWorld::NeutralLockState state;
        state.giveKey("skyrimse", 0x20);
        state.giveKey("skyrimse", 0x10);
        state.unlock("skyrimse", 0x99);
        const auto keys = state.keyEntries();
        ASSERT_EQ(keys.size(), 2u);
        EXPECT_EQ(keys[0].second, 0x10u);
        EXPECT_EQ(keys[1].second, 0x20u);
        ASSERT_EQ(state.unlockedEntries().size(), 1u);
        EXPECT_EQ(state.unlockedEntries()[0].first, "skyrimse");
        state.clear();
        EXPECT_TRUE(state.keyEntries().empty());
        EXPECT_TRUE(state.unlockedEntries().empty());
    }
}
