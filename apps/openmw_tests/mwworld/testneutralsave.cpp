#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

#include "apps/openmw/mwworld/neutralsave.hpp"

// Task 0177 (neutral world saves, stage N6): the pure part of a neutral session's save. Written from the spec before the
// code and run red against a stub that answers nothing.

namespace
{
    using namespace MWWorld;

    NeutralSaveCellKey interior(std::string game, std::uint32_t cell)
    {
        NeutralSaveCellKey key;
        key.mKind = NeutralSaveCellKey::Kind::Interior;
        key.mGameId = std::move(game);
        key.mCell = cell;
        return key;
    }

    NeutralSaveCellKey exterior(std::string game, std::uint32_t world, std::int32_t x, std::int32_t y)
    {
        NeutralSaveCellKey key;
        key.mKind = NeutralSaveCellKey::Kind::Exterior;
        key.mGameId = std::move(game);
        key.mWorld = world;
        key.mX = x;
        key.mY = y;
        return key;
    }

    NeutralSavedObject moved(std::uint32_t placement, float x, float y, float z)
    {
        NeutralSavedObject o;
        o.mPlacement = placement;
        o.mFlags = NeutralSaveMoved;
        o.mPos[0] = x;
        o.mPos[1] = y;
        o.mPos[2] = z;
        o.mRot[2] = 1.5f;
        return o;
    }

    NeutralSaveState sample()
    {
        NeutralSaveState s;
        s.mPlayerCell = interior("skyrimse", 0x1A2B3C);
        NeutralSavedCell a;
        a.mKey = interior("skyrimse", 0x1A2B3C);
        a.mObjects.push_back(moved(10, 1.f, 2.f, 3.f));
        NeutralSavedObject off;
        off.mPlacement = 20;
        off.mFlags = NeutralSaveEnabledChanged; // disabled
        a.mObjects.push_back(off);
        NeutralSavedObject on;
        on.mPlacement = 30;
        on.mFlags = NeutralSaveEnabledChanged | NeutralSaveEnabled;
        a.mObjects.push_back(on);
        NeutralSavedObject gone;
        gone.mPlacement = 40;
        gone.mFlags = NeutralSaveDeleted;
        a.mObjects.push_back(gone);
        NeutralSavedObject big;
        big.mPlacement = 50;
        big.mFlags = NeutralSaveScaled;
        big.mScale = 2.5f;
        a.mObjects.push_back(big);
        s.mCells.push_back(a);
        NeutralSavedCell b;
        b.mKey = exterior("skyrimse", 0x3C, -7, 12);
        b.mObjects.push_back(moved(5, -100.f, 4096.5f, 0.25f));
        s.mCells.push_back(b);
        s.mUnlocked = { { "skyrimse", 0x777 }, { "skyrimse", 0x778 } };
        s.mKeys = { { "skyrimse", 0x0F } };
        return s;
    }

    std::vector<std::byte> bytesOf(const NeutralSaveState& s)
    {
        return encodeNeutralSave(s);
    }

    ESM::CellRef original(float x, float y, float z, float scale = 1.f)
    {
        ESM::CellRef ref;
        ref.mPos.pos[0] = x;
        ref.mPos.pos[1] = y;
        ref.mPos.pos[2] = z;
        ref.mPos.rot[0] = 0.f;
        ref.mPos.rot[1] = 0.25f;
        ref.mPos.rot[2] = 0.5f;
        ref.mScale = scale;
        return ref;
    }

    NeutralObjectNow nowFrom(const ESM::CellRef& ref, bool enabled = true)
    {
        NeutralObjectNow now;
        now.mPosition = ref.mPos;
        now.mScale = ref.mScale;
        now.mEnabled = enabled;
        return now;
    }

    TEST(NeutralSave, aStateRoundTripsExactly)
    {
        NeutralSaveState s = sample();
        const std::vector<std::byte> bytes = bytesOf(s);
        ASSERT_FALSE(bytes.empty());
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        canonicaliseNeutralSave(s);
        EXPECT_EQ(back, s);
    }

    TEST(NeutralSave, anEmptyStateRoundTrips)
    {
        NeutralSaveState s;
        const std::vector<std::byte> bytes = bytesOf(s);
        ASSERT_FALSE(bytes.empty());
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        EXPECT_EQ(back, s);
    }

    TEST(NeutralSave, equalStatesGiveEqualBytesWhateverTheCollectionOrder)
    {
        const NeutralSaveState a = sample();
        NeutralSaveState b = sample();
        std::reverse(b.mCells.begin(), b.mCells.end());
        for (NeutralSavedCell& cell : b.mCells)
            std::reverse(cell.mObjects.begin(), cell.mObjects.end());
        std::reverse(b.mUnlocked.begin(), b.mUnlocked.end());
        EXPECT_EQ(bytesOf(a), bytesOf(b));
    }

    TEST(NeutralSave, canonicalisingSortsAndRemovesDuplicates)
    {
        NeutralSaveState s = sample();
        s.mUnlocked.push_back({ "skyrimse", 0x777 });
        s.mCells.push_back(s.mCells.front());
        canonicaliseNeutralSave(s);
        EXPECT_EQ(s.mUnlocked.size(), 2u);
        EXPECT_EQ(s.mCells.size(), 2u);
        EXPECT_TRUE(std::is_sorted(s.mUnlocked.begin(), s.mUnlocked.end()));
    }

    TEST(NeutralSave, aNegativeGridSquareKeepsItsSign)
    {
        NeutralSaveState s;
        s.mPlayerCell = exterior("skyrimse", 60, -3, -2147483647);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytesOf(s), back, error)) << error;
        EXPECT_EQ(back.mPlayerCell.mX, -3);
        EXPECT_EQ(back.mPlayerCell.mY, -2147483647);
    }

    TEST(NeutralSave, everyTruncationIsRefusedWithAnError)
    {
        const std::vector<std::byte> bytes = bytesOf(sample());
        ASSERT_FALSE(bytes.empty());
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            NeutralSaveState back;
            std::string error;
            EXPECT_FALSE(decodeNeutralSave(std::span<const std::byte>(bytes.data(), n), back, error)) << n;
            EXPECT_FALSE(error.empty()) << n;
            EXPECT_EQ(back, NeutralSaveState{}) << "left empty at " << n;
        }
    }

    TEST(NeutralSave, trailingBytesAreRefused)
    {
        std::vector<std::byte> bytes = bytesOf(sample());
        ASSERT_FALSE(bytes.empty());
        bytes.push_back(std::byte{ 0 });
        NeutralSaveState back;
        std::string error;
        EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
    }

    TEST(NeutralSave, aNewerVersionIsRefusedByName)
    {
        std::vector<std::byte> bytes = bytesOf(sample());
        ASSERT_GE(bytes.size(), 8u);
        // the version is the u32 after the 4 byte magic
        bytes[4] = std::byte{ 0x7F };
        NeutralSaveState back;
        std::string error;
        EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
        EXPECT_NE(error.find("version"), std::string::npos) << error;
    }

    TEST(NeutralSave, aWrongMagicIsRefused)
    {
        std::vector<std::byte> bytes = bytesOf(sample());
        ASSERT_FALSE(bytes.empty());
        bytes[0] = std::byte{ 'X' };
        NeutralSaveState back;
        std::string error;
        EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
    }

    TEST(NeutralSave, aHugeCountWithFewBytesIsRefusedBeforeAllocating)
    {
        // magic, version, then a player cell of kind None, then a cell count of 0xFFFFFFFF and nothing else
        std::vector<std::byte> bytes = bytesOf(NeutralSaveState{});
        ASSERT_GE(bytes.size(), 9u);
        // an empty state ends in three zero counts (cells, unlocked, keys): make the first one huge
        for (std::size_t i = bytes.size() - 12; i < bytes.size() - 8; ++i)
            bytes[i] = std::byte{ 0xFF };
        NeutralSaveState back;
        std::string error;
        EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
        EXPECT_FALSE(error.empty());
    }

    TEST(NeutralSave, aNonCanonicalOrderIsRefused)
    {
        // objects out of order inside a cell: build the bytes of a valid state and swap two placement ids in place is
        // not possible without knowing the layout, so use two states whose bytes differ only by the order and check
        // the decoder rejects the reversed one by flipping the placement ids of a two object cell.
        NeutralSaveState s;
        NeutralSavedCell cell;
        cell.mKey = interior("g", 1);
        cell.mObjects.push_back(moved(1, 0, 0, 0));
        cell.mObjects.push_back(moved(2, 0, 0, 0));
        s.mCells.push_back(cell);
        std::vector<std::byte> bytes = bytesOf(s);
        ASSERT_FALSE(bytes.empty());
        // find the two placement ids 1 and 2 (little endian u32) and swap them
        std::size_t first = 0, second = 0;
        for (std::size_t i = 0; i + 4 <= bytes.size(); ++i)
        {
            const std::uint32_t v = static_cast<std::uint32_t>(bytes[i]) | static_cast<std::uint32_t>(bytes[i + 1]) << 8
                | static_cast<std::uint32_t>(bytes[i + 2]) << 16 | static_cast<std::uint32_t>(bytes[i + 3]) << 24;
            if (v == 1 && bytes[i + 4 < bytes.size() ? i + 4 : i] == std::byte{ static_cast<unsigned char>(NeutralSaveMoved) })
                first = i;
            if (v == 2 && i + 4 < bytes.size() && bytes[i + 4] == std::byte{ static_cast<unsigned char>(NeutralSaveMoved) })
                second = i;
        }
        ASSERT_NE(first, 0u);
        ASSERT_NE(second, 0u);
        std::swap(bytes[first], bytes[second]);
        NeutralSaveState back;
        std::string error;
        EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
    }

    TEST(NeutralSave, aStringOverTheCapIsRefusedWhenWritten)
    {
        NeutralSaveState s;
        s.mPlayerCell = interior(std::string(kNeutralSaveMaxString + 1, 'a'), 1);
        EXPECT_THROW((void)encodeNeutralSave(s), std::length_error);
    }

    TEST(NeutralSave, randomMutationsNeverCrashAndAcceptedOnesAreCanonical)
    {
        const std::vector<std::byte> good = bytesOf(sample());
        ASSERT_FALSE(good.empty());
        std::mt19937 rng(177);
        int accepted = 0;
        for (int iteration = 0; iteration < 3000; ++iteration)
        {
            std::vector<std::byte> bytes = good;
            const int flips = 1 + static_cast<int>(rng() % 3);
            for (int i = 0; i < flips; ++i)
                bytes[rng() % bytes.size()] = static_cast<std::byte>(rng() & 0xFF);
            NeutralSaveState back;
            std::string error;
            if (decodeNeutralSave(bytes, back, error))
            {
                ++accepted;
                EXPECT_EQ(bytesOf(back), bytes) << "an accepted blob must be the canonical encoding of what it holds";
            }
            else
            {
                EXPECT_FALSE(error.empty());
            }
        }
        EXPECT_LT(accepted, 3000);
    }

    TEST(NeutralDiff, anUntouchedObjectHasNoDifference)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        EXPECT_FALSE(diffNeutralObject(7, ref, true, nowFrom(ref)).has_value());
        EXPECT_FALSE(diffNeutralObject(7, ref, false, nowFrom(ref, false)).has_value());
    }

    TEST(NeutralDiff, aMovedObjectKeepsItsPositionAndRotation)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref);
        now.mPosition.pos[2] = 9.f;
        const auto diff = diffNeutralObject(7, ref, true, now);
        ASSERT_TRUE(diff.has_value());
        EXPECT_EQ(diff->mPlacement, 7u);
        EXPECT_EQ(diff->mFlags, NeutralSaveMoved);
        EXPECT_EQ(diff->mPos[2], 9.f);
        EXPECT_EQ(diff->mRot[1], 0.25f);
    }

    TEST(NeutralDiff, aTurnedObjectCountsAsMoved)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref);
        now.mPosition.rot[2] += 0.5f;
        const auto diff = diffNeutralObject(7, ref, true, now);
        ASSERT_TRUE(diff.has_value());
        EXPECT_NE(diff->mFlags & NeutralSaveMoved, 0);
    }

    TEST(NeutralDiff, aScaledObjectKeepsItsScaleOnly)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref);
        now.mScale = 3.f;
        const auto diff = diffNeutralObject(7, ref, true, now);
        ASSERT_TRUE(diff.has_value());
        EXPECT_EQ(diff->mFlags, NeutralSaveScaled);
        EXPECT_EQ(diff->mScale, 3.f);
    }

    TEST(NeutralDiff, disablingAnEnabledObjectIsAChangeToDisabled)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref, false);
        const auto diff = diffNeutralObject(7, ref, true, now);
        ASSERT_TRUE(diff.has_value());
        EXPECT_EQ(diff->mFlags, NeutralSaveEnabledChanged);
    }

    TEST(NeutralDiff, enablingAnInitiallyDisabledObjectIsAChangeToEnabled)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref, true);
        const auto diff = diffNeutralObject(7, ref, false, now);
        ASSERT_TRUE(diff.has_value());
        EXPECT_EQ(diff->mFlags, NeutralSaveEnabledChanged | NeutralSaveEnabled);
    }

    TEST(NeutralDiff, aDeletedObjectIsOnlyMarkedDeleted)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref);
        now.mDeleted = true;
        now.mPosition.pos[0] = 99.f; // whatever it held when it died is not saved
        const auto diff = diffNeutralObject(7, ref, true, now);
        ASSERT_TRUE(diff.has_value());
        EXPECT_EQ(diff->mFlags, NeutralSaveDeleted);
    }

    TEST(NeutralDiff, anObjectMovedBackToItsPlaceHasNoDifference)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref);
        now.mPosition.pos[0] = 5.f;
        now.mPosition.pos[0] = 1.f;
        EXPECT_FALSE(diffNeutralObject(7, ref, true, now).has_value());
    }

    TEST(NeutralDiff, applyingTheDifferenceGivesBackTheCurrentState)
    {
        const ESM::CellRef ref = original(1, 2, 3);
        NeutralObjectNow now = nowFrom(ref, false);
        now.mPosition.pos[1] = -40.f;
        now.mPosition.rot[0] = 0.75f;
        now.mScale = 0.5f;
        const auto diff = diffNeutralObject(7, ref, true, now);
        ASSERT_TRUE(diff.has_value());
        NeutralObjectNow rebuilt = nowFrom(ref, true); // the pack's own state
        applyNeutralSavedObject(*diff, rebuilt);
        EXPECT_EQ(rebuilt.mPosition.pos[1], -40.f);
        EXPECT_EQ(rebuilt.mPosition.rot[0], 0.75f);
        EXPECT_EQ(rebuilt.mScale, 0.5f);
        EXPECT_FALSE(rebuilt.mEnabled);
        EXPECT_FALSE(rebuilt.mDeleted);
    }

    TEST(NeutralDiff, applyingADeletionDeletes)
    {
        NeutralSavedObject saved;
        saved.mFlags = NeutralSaveDeleted;
        NeutralObjectNow now = nowFrom(original(1, 2, 3));
        applyNeutralSavedObject(saved, now);
        EXPECT_TRUE(now.mDeleted);
    }

    TEST(NeutralDiff, applyingAnEnableOfAnInitiallyDisabledObjectEnablesIt)
    {
        NeutralSavedObject saved;
        saved.mFlags = NeutralSaveEnabledChanged | NeutralSaveEnabled;
        NeutralObjectNow now = nowFrom(original(1, 2, 3), false);
        applyNeutralSavedObject(saved, now);
        EXPECT_TRUE(now.mEnabled);
    }
}
