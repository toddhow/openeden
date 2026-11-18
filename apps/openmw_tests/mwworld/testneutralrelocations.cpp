// Task 0286: placements a game pack moved into another cell, kept by the world and written into the neutral save
// (version 4). Written from the spec before the code; the first run was against a stub store and a codec that
// ignores relocations.
//
// In plain words: a relocation says "this placement belongs to cell A in the pack's record but stands in cell C now,
// here". The world keeps one per moved placement (the origin never changes, a move back to it forgets the entry), the
// save writes them in a fixed order, and a decoder reads them back strictly. A save written before this task
// (version 1 to 3) still loads with none.
#include "apps/openmw/mwworld/neutralrelocations.hpp"
#include "apps/openmw/mwworld/neutralsave.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace
{
    using namespace MWWorld;

    NeutralSaveCellKey interior(std::uint32_t id)
    {
        NeutralSaveCellKey k;
        k.mKind = NeutralSaveCellKey::Kind::Interior;
        k.mGameId = "skyrimse";
        k.mCell = id;
        return k;
    }

    NeutralSaveCellKey exterior(std::int32_t x, std::int32_t y)
    {
        NeutralSaveCellKey k;
        k.mKind = NeutralSaveCellKey::Kind::Exterior;
        k.mGameId = "skyrimse";
        k.mWorld = 60;
        k.mX = x;
        k.mY = y;
        return k;
    }

    const float kPos[3] = { 100.5f, -20.25f, 3000.f };
    const float kRot[3] = { 0.f, 0.f, 1.5f };

    NeutralRelocation entry(std::uint32_t placement, NeutralSaveCellKey from, NeutralSaveCellKey to)
    {
        NeutralRelocation r;
        r.mGameId = "skyrimse";
        r.mPlacement = placement;
        r.mOrigin = std::move(from);
        r.mDestination = std::move(to);
        for (int i = 0; i < 3; ++i)
        {
            r.mPos[i] = kPos[i];
            r.mRot[i] = kRot[i];
        }
        return r;
    }

    NeutralSaveState stateWith(std::vector<NeutralRelocation> relocations)
    {
        NeutralSaveState state;
        state.mPlayerCell = interior(7);
        state.mRelocations = std::move(relocations);
        return state;
    }

    std::uint32_t versionOf(const std::vector<std::byte>& bytes)
    {
        return static_cast<std::uint32_t>(bytes.at(4)) | static_cast<std::uint32_t>(bytes.at(5)) << 8;
    }

    bool decodes(const std::vector<std::byte>& bytes, std::string* error = nullptr, NeutralSaveState* out = nullptr)
    {
        NeutralSaveState state;
        std::string e;
        const bool ok = decodeNeutralSave(bytes, state, e);
        if (error != nullptr)
            *error = e;
        if (out != nullptr)
            *out = state;
        return ok;
    }

    // ---- the world's store ----

    TEST(NeutralRelocationStore, aMoveIsKeptAndFoundByGameAndPlacement)
    {
        NeutralRelocations store;
        EXPECT_TRUE(store.note("skyrimse", 0x1A66E, interior(1), exterior(3, -4), kPos, kRot));
        const NeutralRelocation* found = store.find("skyrimse", 0x1A66E);
        ASSERT_NE(found, nullptr);
        EXPECT_EQ(found->mOrigin, interior(1));
        EXPECT_EQ(found->mDestination, exterior(3, -4));
        EXPECT_FLOAT_EQ(found->mPos[2], 3000.f);
        EXPECT_FLOAT_EQ(found->mRot[2], 1.5f);
        EXPECT_EQ(store.find("skyrimse", 0x1A66F), nullptr);
        EXPECT_EQ(store.find("fallout4", 0x1A66E), nullptr);
        EXPECT_EQ(store.size(), 1u);
    }

    TEST(NeutralRelocationStore, aSecondMoveKeepsTheFirstOriginAndTakesTheNewDestination)
    {
        NeutralRelocations store;
        ASSERT_TRUE(store.note("skyrimse", 5, interior(1), interior(2), kPos, kRot));
        // the caller passes the cell it was in as "origin"; the store keeps the one it already knew
        ASSERT_TRUE(store.note("skyrimse", 5, interior(2), exterior(0, 0), kPos, kRot));
        ASSERT_EQ(store.size(), 1u);
        EXPECT_EQ(store.find("skyrimse", 5)->mOrigin, interior(1));
        EXPECT_EQ(store.find("skyrimse", 5)->mDestination, exterior(0, 0));
    }

    TEST(NeutralRelocationStore, aMoveBackToTheOriginForgetsTheEntry)
    {
        NeutralRelocations store;
        ASSERT_TRUE(store.note("skyrimse", 5, interior(1), interior(2), kPos, kRot));
        EXPECT_TRUE(store.note("skyrimse", 5, interior(2), interior(1), kPos, kRot));
        EXPECT_EQ(store.size(), 0u);
        EXPECT_EQ(store.find("skyrimse", 5), nullptr);
    }

    TEST(NeutralRelocationStore, aFirstMoveWithTheSameCellIsNotAMove)
    {
        NeutralRelocations store;
        EXPECT_TRUE(store.note("skyrimse", 5, interior(1), interior(1), kPos, kRot));
        EXPECT_EQ(store.size(), 0u);
    }

    TEST(NeutralRelocationStore, erasingRemovesOnlyThatEntry)
    {
        NeutralRelocations store;
        ASSERT_TRUE(store.note("skyrimse", 5, interior(1), interior(2), kPos, kRot));
        ASSERT_TRUE(store.note("skyrimse", 6, interior(1), interior(2), kPos, kRot));
        EXPECT_TRUE(store.erase("skyrimse", 5));
        EXPECT_FALSE(store.erase("skyrimse", 5));
        EXPECT_EQ(store.find("skyrimse", 5), nullptr);
        EXPECT_NE(store.find("skyrimse", 6), nullptr);
    }

    TEST(NeutralRelocationStore, badNotesAreRefusedAndNothingIsKept)
    {
        NeutralRelocations store;
        EXPECT_FALSE(store.note("", 1, interior(1), interior(2), kPos, kRot));
        EXPECT_FALSE(store.note("skyrimse", 1, NeutralSaveCellKey{}, interior(2), kPos, kRot));
        EXPECT_FALSE(store.note("skyrimse", 1, interior(1), NeutralSaveCellKey{}, kPos, kRot));
        const float nanPos[3] = { std::numeric_limits<float>::quiet_NaN(), 0.f, 0.f };
        EXPECT_FALSE(store.note("skyrimse", 1, interior(1), interior(2), nanPos, kRot));
        const float infRot[3] = { 0.f, std::numeric_limits<float>::infinity(), 0.f };
        EXPECT_FALSE(store.note("skyrimse", 1, interior(1), interior(2), kPos, infRot));
        EXPECT_EQ(store.size(), 0u);
    }

    TEST(NeutralRelocationStore, theStoreIsBounded)
    {
        NeutralRelocations store;
        for (std::uint32_t i = 0; i < kNeutralSaveMaxRelocations; ++i)
            ASSERT_TRUE(store.note("skyrimse", i + 1, interior(1), interior(2), kPos, kRot)) << i;
        EXPECT_FALSE(store.note("skyrimse", kNeutralSaveMaxRelocations + 1, interior(1), interior(2), kPos, kRot));
        // an entry that exists can still be changed when the store is full
        EXPECT_TRUE(store.note("skyrimse", 1, interior(2), exterior(1, 1), kPos, kRot));
    }

    TEST(NeutralRelocationStore, collectIsInKeyOrderAndLoadRestoresIt)
    {
        NeutralRelocations store;
        ASSERT_TRUE(store.note("skyrimse", 30, interior(1), interior(2), kPos, kRot));
        ASSERT_TRUE(store.note("skyrimse", 10, interior(1), exterior(1, 2), kPos, kRot));
        ASSERT_TRUE(store.note("fallout4", 20, interior(1), interior(2), kPos, kRot));
        const std::vector<NeutralRelocation> all = store.collect();
        ASSERT_EQ(all.size(), 3u);
        EXPECT_EQ(all[0].mGameId, "fallout4");
        EXPECT_EQ(all[1].mPlacement, 10u);
        EXPECT_EQ(all[2].mPlacement, 30u);
        NeutralRelocations other;
        other.load(all);
        EXPECT_EQ(other.collect(), all);
        other.clear();
        EXPECT_EQ(other.size(), 0u);
    }

    // ---- the save ----

    TEST(NeutralRelocationSave, aStateWithRelocationsRoundTripsAsVersionFour)
    {
        const NeutralSaveState state = stateWith({ entry(10, interior(1), exterior(3, -4)),
            entry(11, exterior(5, 5), interior(9)), entry(12, interior(2), interior(3)) });
        const std::vector<std::byte> bytes = encodeNeutralSave(state);
        EXPECT_EQ(versionOf(bytes), kNeutralSaveVersionRelocations);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodes(bytes, &error, &back)) << error;
        EXPECT_EQ(back.mRelocations, state.mRelocations);
        EXPECT_EQ(back, state);
        EXPECT_EQ(encodeNeutralSave(back), bytes) << "canonical: the same bytes again";
    }

    TEST(NeutralRelocationSave, theBytesDoNotDependOnTheOrderTheyWereCollectedIn)
    {
        const NeutralSaveState a = stateWith({ entry(10, interior(1), interior(2)), entry(11, interior(1), interior(2)) });
        const NeutralSaveState b = stateWith({ entry(11, interior(1), interior(2)), entry(10, interior(1), interior(2)) });
        EXPECT_EQ(encodeNeutralSave(a), encodeNeutralSave(b));
    }

    TEST(NeutralRelocationSave, aStateWithoutRelocationsKeepsItsOldVersionAndBytes)
    {
        NeutralSaveState state = stateWith({});
        EXPECT_EQ(versionOf(encodeNeutralSave(state)), kNeutralSaveVersion);
        state.mNextCreatedIndex = 3;
        EXPECT_EQ(versionOf(encodeNeutralSave(state)), kNeutralSaveVersionCreated);
    }

    TEST(NeutralRelocationSave, aVersionFourSaveAlsoKeepsTheCounterAndPoses)
    {
        NeutralSaveState state = stateWith({ entry(10, interior(1), interior(2)) });
        state.mNextCreatedIndex = 9;
        NeutralCorpsePose pose;
        pose.mGameId = "skyrimse";
        pose.mPlacement = 77;
        NeutralCorpseBone bone;
        bone.mName = "Pelvis";
        pose.mBones.push_back(bone);
        state.mCorpsePoses.push_back(pose);
        const std::vector<std::byte> bytes = encodeNeutralSave(state);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodes(bytes, &error, &back)) << error;
        EXPECT_EQ(back, state);
        EXPECT_EQ(back.mNextCreatedIndex, 9u);
        EXPECT_EQ(back.mCorpsePoses.size(), 1u);
    }

    TEST(NeutralRelocationSave, everyPrefixOfAVersionFourSaveIsRefused)
    {
        const std::vector<std::byte> bytes = encodeNeutralSave(stateWith({ entry(10, interior(1), exterior(1, 1)) }));
        for (std::size_t n = 0; n < bytes.size(); ++n)
            EXPECT_FALSE(decodes(std::vector<std::byte>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n))))
                << n;
        EXPECT_TRUE(decodes(bytes));
    }

    TEST(NeutralRelocationSave, aRefusalOfAVersionFourSaveNamesTheVersion)
    {
        std::vector<std::byte> bytes = encodeNeutralSave(stateWith({ entry(10, interior(1), exterior(1, 1)) }));
        bytes.pop_back();
        std::string error;
        EXPECT_FALSE(decodes(bytes, &error));
        EXPECT_NE(error.find("version"), std::string::npos) << error;
    }

    TEST(NeutralRelocationSave, trailingBytesAreRefused)
    {
        std::vector<std::byte> bytes = encodeNeutralSave(stateWith({ entry(10, interior(1), exterior(1, 1)) }));
        bytes.push_back(std::byte{ 0 });
        EXPECT_FALSE(decodes(bytes));
    }

    TEST(NeutralRelocationSave, aStateTheDecoderCouldNotReadIsNotWritten)
    {
        // origin equals destination
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ entry(10, interior(1), interior(1)) })), std::exception);
        // no game id, no cell kind, a position that is not finite
        NeutralRelocation noGame = entry(10, interior(1), interior(2));
        noGame.mGameId.clear();
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ noGame })), std::exception);
        NeutralRelocation noKind = entry(10, NeutralSaveCellKey{}, interior(2));
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ noKind })), std::exception);
        NeutralRelocation nan = entry(10, interior(1), interior(2));
        nan.mPos[0] = std::numeric_limits<float>::quiet_NaN();
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ nan })), std::exception);
    }

    TEST(NeutralRelocationSave, tooManyRelocationsAreNotWritten)
    {
        std::vector<NeutralRelocation> many;
        for (std::uint32_t i = 0; i <= kNeutralSaveMaxRelocations; ++i)
            many.push_back(entry(i + 1, interior(1), interior(2)));
        EXPECT_THROW((void)encodeNeutralSave(stateWith(std::move(many))), std::exception);
    }

    TEST(NeutralRelocationSave, aCountThatTheBytesCannotHoldIsRefused)
    {
        std::vector<std::byte> bytes = encodeNeutralSave(stateWith({ entry(10, interior(1), exterior(1, 1)) }));
        // the relocation count is the u32 right after the pose count, which is the last 4 bytes before the entry
        // (the entry is: game string, placement u32, two keys, six floats). Find it by re-encoding with another count.
        const std::size_t entryBytes = 2 + 8 + 4 + (1 + 2 + 8 + 4) + (1 + 2 + 8 + 4 + 4 + 4) + 24;
        ASSERT_GT(bytes.size(), entryBytes + 4);
        const std::size_t countAt = bytes.size() - entryBytes - 4;
        for (int i = 0; i < 4; ++i)
            bytes[countAt + static_cast<std::size_t>(i)] = std::byte{ 0xFF };
        EXPECT_FALSE(decodes(bytes));
    }

    TEST(NeutralRelocationSave, aVersionThreeSaveHasNoRelocationsAndStillLoads)
    {
        NeutralSaveState state = stateWith({});
        NeutralCorpsePose pose;
        pose.mGameId = "skyrimse";
        pose.mPlacement = 77;
        NeutralCorpseBone bone;
        bone.mName = "Pelvis";
        pose.mBones.push_back(bone);
        state.mCorpsePoses.push_back(pose);
        const std::vector<std::byte> bytes = encodeNeutralSave(state);
        EXPECT_EQ(versionOf(bytes), kNeutralSaveVersionPoses);
        NeutralSaveState back;
        ASSERT_TRUE(decodes(bytes, nullptr, &back));
        EXPECT_TRUE(back.mRelocations.empty());
    }
}
