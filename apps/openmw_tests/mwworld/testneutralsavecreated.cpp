#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include <openeden/game/world_model.hpp>
#include <openeden/game/world_source_service.hpp>

#include "apps/openmw/mwworld/neutralsave.hpp"

// Task 0202: objects a script created (PlaceAtMe) in the neutral save, NWLD version 2. Written from the spec before the
// code and run red against a stub that writes version 1 only and a save helper that answers nothing.

namespace
{
    using namespace MWWorld;
    using openeden::sdk::game::kCreatedObjectBase;

    NeutralSaveCellKey interior(std::uint32_t cell)
    {
        NeutralSaveCellKey key;
        key.mKind = NeutralSaveCellKey::Kind::Interior;
        key.mGameId = "skyrimse";
        key.mCell = cell;
        return key;
    }

    NeutralCreatedInfo barrelInfo()
    {
        NeutralCreatedInfo info;
        info.mBase = 0x0001'2345;
        info.mKind = static_cast<std::uint8_t>(openeden::sdk::game::ObjectKind::Static);
        info.mEditorId = "Barrel02";
        info.mModel = "clutter\\barrel02.nif";
        info.mHasBounds = true;
        info.mBoundsMin[0] = -10.f;
        info.mBoundsMin[1] = -10.f;
        info.mBoundsMin[2] = 0.f;
        info.mBoundsMax[0] = 10.f;
        info.mBoundsMax[1] = 10.f;
        info.mBoundsMax[2] = 40.f;
        return info;
    }

    NeutralObjectNow nowAt(float x, float y, float z)
    {
        NeutralObjectNow now;
        now.mPosition.pos[0] = x;
        now.mPosition.pos[1] = y;
        now.mPosition.pos[2] = z;
        now.mPosition.rot[2] = 0.5f;
        return now;
    }

    NeutralSavedObject created(std::uint32_t index, float x)
    {
        return saveCreatedNeutralObject(
            static_cast<std::uint32_t>(kCreatedObjectBase) + index, barrelInfo(), nowAt(x, 2.f, 3.f));
    }

    NeutralSaveState withCreated()
    {
        NeutralSaveState s;
        s.mPlayerCell = interior(0x1A2B3C);
        NeutralSavedCell cell;
        cell.mKey = interior(0x1A2B3C);
        NeutralSavedObject packMoved;
        packMoved.mPlacement = 10;
        packMoved.mFlags = NeutralSaveMoved;
        packMoved.mPos[0] = 5.f;
        cell.mObjects.push_back(packMoved);
        cell.mObjects.push_back(created(1, 100.f));
        cell.mObjects.push_back(created(2, 200.f));
        s.mCells.push_back(cell);
        s.mNextCreatedIndex = 3;
        return s;
    }

    std::vector<std::byte> bytesOf(const NeutralSaveState& s)
    {
        return encodeNeutralSave(s);
    }

    std::uint32_t versionOf(const std::vector<std::byte>& bytes)
    {
        return static_cast<std::uint32_t>(bytes[4]) | static_cast<std::uint32_t>(bytes[5]) << 8;
    }

    TEST(NeutralSaveCreated, aStateWithoutCreatedObjectsStillWritesVersionOne)
    {
        NeutralSaveState s;
        s.mPlayerCell = interior(7);
        NeutralSavedCell cell;
        cell.mKey = interior(7);
        NeutralSavedObject o;
        o.mPlacement = 4;
        o.mFlags = NeutralSaveEnabledChanged;
        cell.mObjects.push_back(o);
        s.mCells.push_back(cell);
        const std::vector<std::byte> bytes = bytesOf(s);
        EXPECT_EQ(versionOf(bytes), 1u);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        EXPECT_EQ(back, s);
    }

    TEST(NeutralSaveCreated, aCreatedObjectIsASavedWholeEntry)
    {
        const NeutralSavedObject o = created(5, 1.f);
        EXPECT_EQ(o.mPlacement, static_cast<std::uint32_t>(kCreatedObjectBase) + 5);
        EXPECT_NE(o.mFlags & NeutralSaveCreated, 0);
        EXPECT_NE(o.mFlags & NeutralSaveMoved, 0) << "a created object always saves where it is";
        EXPECT_EQ(o.mFlags & NeutralSaveScaled, 0) << "scale 1 is the default";
        EXPECT_EQ(o.mFlags & NeutralSaveEnabledChanged, 0) << "enabled is the default";
        EXPECT_EQ(o.mPos[0], 1.f);
        EXPECT_EQ(o.mRot[2], 0.5f);
        ASSERT_TRUE(o.mCreated.has_value());
        EXPECT_EQ(*o.mCreated, barrelInfo());
    }

    TEST(NeutralSaveCreated, aCreatedObjectAtTheOriginIsStillMoved)
    {
        const NeutralSavedObject o = saveCreatedNeutralObject(
            static_cast<std::uint32_t>(kCreatedObjectBase) + 1, barrelInfo(), NeutralObjectNow{});
        EXPECT_NE(o.mFlags & NeutralSaveMoved, 0);
    }

    TEST(NeutralSaveCreated, aScaleAndADisabledStateAreSaved)
    {
        NeutralObjectNow now = nowAt(1.f, 2.f, 3.f);
        now.mScale = 2.5f;
        now.mEnabled = false;
        const NeutralSavedObject o = saveCreatedNeutralObject(
            static_cast<std::uint32_t>(kCreatedObjectBase) + 1, barrelInfo(), now);
        EXPECT_NE(o.mFlags & NeutralSaveScaled, 0);
        EXPECT_EQ(o.mScale, 2.5f);
        EXPECT_NE(o.mFlags & NeutralSaveEnabledChanged, 0);
        EXPECT_EQ(o.mFlags & NeutralSaveEnabled, 0);
        NeutralObjectNow applied;
        applyNeutralSavedObject(o, applied);
        EXPECT_EQ(applied.mPosition.pos[1], 2.f);
        EXPECT_EQ(applied.mScale, 2.5f);
        EXPECT_FALSE(applied.mEnabled);
    }

    TEST(NeutralSaveCreated, aStateWithCreatedObjectsWritesVersionTwoAndRoundTripsExactly)
    {
        const NeutralSaveState s = withCreated();
        const std::vector<std::byte> bytes = bytesOf(s);
        EXPECT_EQ(versionOf(bytes), 2u);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        EXPECT_EQ(back, s);
        EXPECT_EQ(back.mNextCreatedIndex, 3u);
        EXPECT_EQ(bytesOf(back), bytes) << "canonical: the bytes of a decoded state are the same";
    }

    TEST(NeutralSaveCreated, theCounterAloneAlsoMakesVersionTwo)
    {
        NeutralSaveState s;
        s.mPlayerCell = interior(7);
        s.mNextCreatedIndex = 9; // objects were created and are all gone
        const std::vector<std::byte> bytes = bytesOf(s);
        EXPECT_EQ(versionOf(bytes), 2u);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        EXPECT_EQ(back.mNextCreatedIndex, 9u);
    }

    TEST(NeutralSaveCreated, everyPrefixOfAVersionTwoSaveIsRefused)
    {
        const std::vector<std::byte> bytes = bytesOf(withCreated());
        for (std::size_t n = 0; n < bytes.size(); ++n)
        {
            NeutralSaveState back;
            std::string error;
            EXPECT_FALSE(decodeNeutralSave(std::span<const std::byte>(bytes.data(), n), back, error)) << n;
            EXPECT_FALSE(error.empty());
        }
    }

    TEST(NeutralSaveCreated, aCreatedEntryInAVersionOneStreamIsRefused)
    {
        std::vector<std::byte> bytes = bytesOf(withCreated());
        bytes[4] = std::byte{ 1 };
        NeutralSaveState back;
        std::string error;
        EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
    }

    TEST(NeutralSaveCreated, aCreatedPlacementBelowTheCreatedRangeIsRefused)
    {
        NeutralSaveState s = withCreated();
        s.mCells[0].mObjects.back().mPlacement = 0x00FFFFFF;
        // the encoder may throw or write it; whichever, the decoder must not accept it
        try
        {
            const std::vector<std::byte> bytes = bytesOf(s);
            NeutralSaveState back;
            std::string error;
            EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
        }
        catch (const std::exception&)
        {
            SUCCEED();
        }
    }

    TEST(NeutralSaveCreated, aCounterAtOrBelowACreatedIndexIsRefused)
    {
        NeutralSaveState s = withCreated();
        s.mNextCreatedIndex = 2; // object 2 exists
        try
        {
            const std::vector<std::byte> bytes = bytesOf(s);
            NeutralSaveState back;
            std::string error;
            EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
        }
        catch (const std::exception&)
        {
            SUCCEED();
        }
    }

    TEST(NeutralSaveCreated, aCreatedEntryThatIsNotMovedIsRefused)
    {
        NeutralSaveState s = withCreated();
        s.mCells[0].mObjects.back().mFlags = NeutralSaveCreated;
        try
        {
            const std::vector<std::byte> bytes = bytesOf(s);
            NeutralSaveState back;
            std::string error;
            EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
        }
        catch (const std::exception&)
        {
            SUCCEED();
        }
    }

    TEST(NeutralSaveCreated, aKindPastTheKnownOnesIsRefused)
    {
        NeutralSaveState s = withCreated();
        s.mCells[0].mObjects.back().mCreated->mKind = 200;
        try
        {
            const std::vector<std::byte> bytes = bytesOf(s);
            NeutralSaveState back;
            std::string error;
            EXPECT_FALSE(decodeNeutralSave(bytes, back, error));
        }
        catch (const std::exception&)
        {
            SUCCEED();
        }
    }

    TEST(NeutralSaveCreated, aLongModelPathCannotBeWritten)
    {
        NeutralSaveState s = withCreated();
        s.mCells[0].mObjects.back().mCreated->mModel = std::string(kNeutralSaveMaxString + 1, 'a');
        EXPECT_THROW((void)encodeNeutralSave(s), std::length_error);
    }

    TEST(NeutralSaveCreated, theNumberOfCreatedObjectsIsBounded)
    {
        NeutralSaveState s;
        s.mPlayerCell = interior(7);
        NeutralSavedCell cell;
        cell.mKey = interior(7);
        for (std::size_t i = 0; i < kNeutralSaveMaxCreated + 1; ++i)
            cell.mObjects.push_back(created(static_cast<std::uint32_t>(i + 1), 1.f));
        s.mCells.push_back(std::move(cell));
        s.mNextCreatedIndex = static_cast<std::uint32_t>(kNeutralSaveMaxCreated + 2);
        EXPECT_THROW((void)encodeNeutralSave(s), std::length_error);
    }

    TEST(NeutralSaveCreated, aMutationFuzzNeverCrashesAndNeverAcceptsWhatItCannotWriteBack)
    {
        const std::vector<std::byte> good = bytesOf(withCreated());
        std::mt19937 rng(2026);
        for (int i = 0; i < 4000; ++i)
        {
            std::vector<std::byte> bytes = good;
            const int flips = 1 + static_cast<int>(rng() % 3);
            for (int f = 0; f < flips; ++f)
                bytes[rng() % bytes.size()] = static_cast<std::byte>(rng() & 0xFF);
            NeutralSaveState back;
            std::string error;
            if (decodeNeutralSave(bytes, back, error))
            {
                EXPECT_EQ(bytesOf(back), bytes) << "an accepted blob must be canonical";
            }
        }
    }
}
