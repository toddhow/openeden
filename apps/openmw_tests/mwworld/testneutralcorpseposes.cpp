// Task 0263: the poses of settled corpses, kept by the world and written into the neutral save (version 3). Written
// from the spec before the code.
//
// In plain words: a pose is a short list of named bones with a rotation and a position. The world keeps one per dead
// actor, the save writes them in a fixed order, a decoder reads them back strictly, and a save written before this
// task (version 1 or 2) still loads with no poses.
#include "apps/openmw/mwworld/neutralcorpseposes.hpp"
#include "apps/openmw/mwworld/neutralsave.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace MWWorld;
    using MWPhysics::RagdollBoneState;

    std::vector<std::string> names(std::size_t n)
    {
        std::vector<std::string> result;
        for (std::size_t i = 0; i < n; ++i)
            result.push_back("Bone" + std::to_string(i));
        return result;
    }

    std::vector<RagdollBoneState> states(std::size_t n, double shift = 0.0)
    {
        std::vector<RagdollBoneState> result(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const double a = 0.1 * static_cast<double>(i + 1) + shift;
            result[i].mPosition[0] = 100.0 + static_cast<double>(i) + shift;
            result[i].mPosition[1] = -200.5;
            result[i].mPosition[2] = 30.25 * static_cast<double>(i);
            result[i].mRotation[0] = std::sin(a / 2) * 0.6;
            result[i].mRotation[1] = std::sin(a / 2) * 0.8;
            result[i].mRotation[2] = 0.0;
            result[i].mRotation[3] = std::cos(a / 2);
        }
        return result;
    }

    NeutralCorpsePose poseOf(const std::string& game, std::uint32_t placement, std::size_t bones = 3)
    {
        NeutralCorpsePoses store;
        const std::vector<std::string> n = names(bones);
        const std::vector<RagdollBoneState> s = states(bones, static_cast<double>(placement) * 0.01);
        EXPECT_TRUE(store.put(game, placement, n, s));
        const NeutralCorpsePose* found = store.find(game, placement);
        EXPECT_NE(found, nullptr);
        return found != nullptr ? *found : NeutralCorpsePose{};
    }

    NeutralSaveState stateWith(std::vector<NeutralCorpsePose> poses)
    {
        NeutralSaveState state;
        state.mPlayerCell.mKind = NeutralSaveCellKey::Kind::Interior;
        state.mPlayerCell.mGameId = "skyrimse";
        state.mPlayerCell.mCell = 7;
        state.mCorpsePoses = std::move(poses);
        return state;
    }

    std::uint32_t versionOf(const std::vector<std::byte>& bytes)
    {
        return static_cast<std::uint32_t>(bytes.at(4)) | static_cast<std::uint32_t>(bytes.at(5)) << 8;
    }

    // ---- the world's store ----

    TEST(CorpsePoseStore, aPoseIsKeptAndFoundByGameAndPlacement)
    {
        NeutralCorpsePoses store;
        EXPECT_TRUE(store.put("skyrimse", 0x1A66E, names(3), states(3)));
        ASSERT_NE(store.find("skyrimse", 0x1A66E), nullptr);
        EXPECT_EQ(store.find("skyrimse", 0x1A66E)->mBones.size(), 3u);
        EXPECT_EQ(store.find("skyrimse", 0x1A66F), nullptr);
        EXPECT_EQ(store.find("fallout4", 0x1A66E), nullptr);
        EXPECT_EQ(store.size(), 1u);
    }

    TEST(CorpsePoseStore, aSecondPutReplacesTheFirst)
    {
        NeutralCorpsePoses store;
        ASSERT_TRUE(store.put("skyrimse", 5, names(3), states(3, 0.0)));
        ASSERT_TRUE(store.put("skyrimse", 5, names(2), states(2, 1.0)));
        EXPECT_EQ(store.size(), 1u);
        EXPECT_EQ(store.find("skyrimse", 5)->mBones.size(), 2u);
    }

    TEST(CorpsePoseStore, erasingRemovesOnlyThatPose)
    {
        NeutralCorpsePoses store;
        ASSERT_TRUE(store.put("skyrimse", 5, names(3), states(3)));
        ASSERT_TRUE(store.put("skyrimse", 6, names(3), states(3)));
        EXPECT_TRUE(store.erase("skyrimse", 5));
        EXPECT_FALSE(store.erase("skyrimse", 5));
        EXPECT_EQ(store.find("skyrimse", 5), nullptr);
        EXPECT_NE(store.find("skyrimse", 6), nullptr);
    }

    TEST(CorpsePoseStore, badPosesAreRefusedAndNothingIsKept)
    {
        NeutralCorpsePoses store;
        EXPECT_FALSE(store.put("", 1, names(3), states(3)));
        EXPECT_FALSE(store.put("skyrimse", 1, names(0), states(0)));
        EXPECT_FALSE(store.put("skyrimse", 1, names(kNeutralSaveMaxPoseBones + 1), states(kNeutralSaveMaxPoseBones + 1)));
        EXPECT_FALSE(store.put("skyrimse", 1, names(3), states(2)));
        std::vector<std::string> longName = names(3);
        longName[1] = std::string(kNeutralSaveMaxPoseName + 1, 'x');
        EXPECT_FALSE(store.put("skyrimse", 1, longName, states(3)));
        std::vector<std::string> emptyName = names(3);
        emptyName[2].clear();
        EXPECT_FALSE(store.put("skyrimse", 1, emptyName, states(3)));
        std::vector<RagdollBoneState> bad = states(3);
        bad[1].mRotation[2] = std::numeric_limits<double>::quiet_NaN();
        EXPECT_FALSE(store.put("skyrimse", 1, names(3), bad));
        bad = states(3);
        bad[0].mPosition[0] = std::numeric_limits<double>::infinity();
        EXPECT_FALSE(store.put("skyrimse", 1, names(3), bad));
        EXPECT_EQ(store.size(), 0u);
    }

    TEST(CorpsePoseStore, aFullStoreRefusesANewActorButStillReplacesAKnownOne)
    {
        NeutralCorpsePoses store;
        for (std::uint32_t i = 0; i < kNeutralSaveMaxPoses; ++i)
            ASSERT_TRUE(store.put("skyrimse", i, names(1), states(1))) << i;
        EXPECT_FALSE(store.put("skyrimse", static_cast<std::uint32_t>(kNeutralSaveMaxPoses), names(1), states(1)));
        EXPECT_TRUE(store.put("skyrimse", 17, names(2), states(2)));
        EXPECT_EQ(store.size(), kNeutralSaveMaxPoses);
    }

    TEST(CorpsePoseStore, aPoseComesBackAsTheNamesAndStatesItWasMadeFrom)
    {
        NeutralCorpsePoses store;
        const std::vector<std::string> n = names(4);
        const std::vector<RagdollBoneState> s = states(4);
        ASSERT_TRUE(store.put("skyrimse", 9, n, s));
        std::vector<std::string> backNames;
        std::vector<RagdollBoneState> backStates;
        ASSERT_NE(store.find("skyrimse", 9), nullptr);
        ASSERT_TRUE(NeutralCorpsePoses::toStates(*store.find("skyrimse", 9), backNames, backStates));
        EXPECT_EQ(backNames, n);
        ASSERT_EQ(backStates.size(), s.size());
        for (std::size_t i = 0; i < s.size(); ++i)
        {
            for (int k = 0; k < 3; ++k)
                EXPECT_NEAR(backStates[i].mPosition[k], s[i].mPosition[k], 1e-3);
            for (int k = 0; k < 4; ++k)
                EXPECT_NEAR(backStates[i].mRotation[k], s[i].mRotation[k], 1e-6);
        }
    }

    TEST(CorpsePoseStore, aPoseWithNoBonesGivesNoStates)
    {
        std::vector<std::string> n;
        std::vector<RagdollBoneState> s;
        EXPECT_FALSE(NeutralCorpsePoses::toStates(NeutralCorpsePose{}, n, s));
    }

    TEST(CorpsePoseStore, collectGivesKeyOrderAndLoadReplacesEverything)
    {
        NeutralCorpsePoses store;
        ASSERT_TRUE(store.put("skyrimse", 30, names(2), states(2)));
        ASSERT_TRUE(store.put("fallout4", 40, names(2), states(2)));
        ASSERT_TRUE(store.put("skyrimse", 10, names(2), states(2)));
        const std::vector<NeutralCorpsePose> all = store.collect();
        ASSERT_EQ(all.size(), 3u);
        EXPECT_EQ(all[0].mGameId, "fallout4");
        EXPECT_EQ(all[1].mPlacement, 10u);
        EXPECT_EQ(all[2].mPlacement, 30u);

        NeutralCorpsePoses other;
        ASSERT_TRUE(other.put("skyrimse", 99, names(1), states(1)));
        other.load(all);
        EXPECT_EQ(other.size(), 3u);
        EXPECT_EQ(other.find("skyrimse", 99), nullptr);
        EXPECT_NE(other.find("skyrimse", 30), nullptr);
        other.load({});
        EXPECT_EQ(other.size(), 0u);
    }

    // ---- the save ----

    TEST(CorpsePoseSave, aStateWithPosesRoundTripsAsVersionThree)
    {
        const NeutralSaveState state = stateWith({ poseOf("skyrimse", 10), poseOf("skyrimse", 30, 16) });
        const std::vector<std::byte> bytes = encodeNeutralSave(state);
        EXPECT_EQ(versionOf(bytes), kNeutralSaveVersionPoses);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        EXPECT_EQ(back.mCorpsePoses, state.mCorpsePoses);
        EXPECT_EQ(back.mPlayerCell, state.mPlayerCell);
    }

    TEST(CorpsePoseSave, aStateWithNoPosesKeepsItsOldVersionAndBytes)
    {
        NeutralSaveState state = stateWith({});
        EXPECT_EQ(versionOf(encodeNeutralSave(state)), kNeutralSaveVersion);
        state.mNextCreatedIndex = 4;
        EXPECT_EQ(versionOf(encodeNeutralSave(state)), kNeutralSaveVersionCreated);
    }

    TEST(CorpsePoseSave, aVersionThreeSaveAlsoKeepsTheCreatedObjectCounter)
    {
        NeutralSaveState state = stateWith({ poseOf("skyrimse", 10) });
        state.mNextCreatedIndex = 6;
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(encodeNeutralSave(state), back, error)) << error;
        EXPECT_EQ(back.mNextCreatedIndex, 6u);
        EXPECT_EQ(back.mCorpsePoses.size(), 1u);
    }

    TEST(CorpsePoseSave, anOldSaveLoadsWithNoPoses)
    {
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(encodeNeutralSave(stateWith({})), back, error)) << error;
        EXPECT_TRUE(back.mCorpsePoses.empty());
    }

    TEST(CorpsePoseSave, theWriterSortsAndDropsDuplicates)
    {
        const NeutralSaveState state
            = stateWith({ poseOf("skyrimse", 30), poseOf("skyrimse", 10), poseOf("skyrimse", 30), poseOf("fallout4", 5) });
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(encodeNeutralSave(state), back, error)) << error;
        ASSERT_EQ(back.mCorpsePoses.size(), 3u);
        EXPECT_EQ(back.mCorpsePoses[0].mGameId, "fallout4");
        EXPECT_EQ(back.mCorpsePoses[1].mPlacement, 10u);
        EXPECT_EQ(back.mCorpsePoses[2].mPlacement, 30u);
    }

    TEST(CorpsePoseSave, equalStatesGiveEqualBytesWhateverTheOrderTheyWereCollectedIn)
    {
        const std::vector<std::byte> a
            = encodeNeutralSave(stateWith({ poseOf("skyrimse", 10), poseOf("skyrimse", 30) }));
        const std::vector<std::byte> b
            = encodeNeutralSave(stateWith({ poseOf("skyrimse", 30), poseOf("skyrimse", 10) }));
        EXPECT_EQ(a, b);
    }

    TEST(CorpsePoseSave, thePosesAreWrittenInFixedWidthSoTheSizeIsBounded)
    {
        // 4096 poses of 32 bones stay far below the decoder's cap of 64 MiB.
        std::vector<NeutralCorpsePose> poses;
        for (std::uint32_t i = 0; i < kNeutralSaveMaxPoses; ++i)
            poses.push_back(poseOf("skyrimse", i, kNeutralSaveMaxPoseBones));
        const std::vector<std::byte> bytes = encodeNeutralSave(stateWith(poses));
        EXPECT_LT(bytes.size(), kNeutralSaveMaxBytes);
        NeutralSaveState back;
        std::string error;
        ASSERT_TRUE(decodeNeutralSave(bytes, back, error)) << error;
        EXPECT_EQ(back.mCorpsePoses.size(), kNeutralSaveMaxPoses);
    }

    TEST(CorpsePoseSave, theWriterRefusesBadPosesWithAnError)
    {
        NeutralCorpsePose noBones;
        noBones.mGameId = "skyrimse";
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ noBones })), std::invalid_argument);
        NeutralCorpsePose noGame = poseOf("skyrimse", 1);
        noGame.mGameId.clear();
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ noGame })), std::invalid_argument);
        NeutralCorpsePose tooMany = poseOf("skyrimse", 1, kNeutralSaveMaxPoseBones);
        tooMany.mBones.push_back(tooMany.mBones.back());
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ tooMany })), std::invalid_argument);
        NeutralCorpsePose badName = poseOf("skyrimse", 1);
        badName.mBones[0].mName = std::string(kNeutralSaveMaxPoseName + 1, 'x');
        EXPECT_THROW((void)encodeNeutralSave(stateWith({ badName })), std::invalid_argument);
        std::vector<NeutralCorpsePose> many;
        for (std::uint32_t i = 0; i <= kNeutralSaveMaxPoses; ++i)
            many.push_back(poseOf("skyrimse", i, 1));
        EXPECT_THROW((void)encodeNeutralSave(stateWith(many)), std::length_error);
    }

    // The decoder's refusals: each starts from good bytes and breaks one thing.
    std::vector<std::byte> goodBytes()
    {
        return encodeNeutralSave(stateWith({ poseOf("skyrimse", 10, 2) }));
    }

    bool decodes(const std::vector<std::byte>& bytes, std::string* errorOut = nullptr)
    {
        NeutralSaveState back;
        std::string error;
        const bool ok = decodeNeutralSave(bytes, back, error);
        if (errorOut != nullptr)
            *errorOut = error;
        if (!ok)
        {
            EXPECT_TRUE(back.mCorpsePoses.empty());
        }
        return ok;
    }

    TEST(CorpsePoseSave, everyTruncationIsRefused)
    {
        const std::vector<std::byte> bytes = goodBytes();
        ASSERT_TRUE(decodes(bytes));
        for (std::size_t n = 0; n < bytes.size(); ++n)
            EXPECT_FALSE(decodes(std::vector<std::byte>(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(n)))) << n;
    }

    TEST(CorpsePoseSave, bytesAfterTheEndAreRefused)
    {
        std::vector<std::byte> bytes = goodBytes();
        bytes.push_back(std::byte{ 0 });
        EXPECT_FALSE(decodes(bytes));
    }

    TEST(CorpsePoseSave, aVersionThreeSaveWithNoPoseIsNotCanonical)
    {
        std::vector<std::byte> bytes = encodeNeutralSave(stateWith({}));
        // version 1 bytes with the version changed to 3 and the counter and pose count appended
        bytes[4] = std::byte{ 3 };
        for (int i = 0; i < 8; ++i)
            bytes.push_back(std::byte{ 0 });
        std::string error;
        EXPECT_FALSE(decodes(bytes, &error));
        EXPECT_NE(error.find("pose"), std::string::npos) << error;
    }

    TEST(CorpsePoseSave, aHugeCountWithFewBytesIsRefusedBeforeAllocating)
    {
        std::vector<std::byte> bytes = encodeNeutralSave(stateWith({}));
        bytes[4] = std::byte{ 3 };
        for (int i = 0; i < 4; ++i)
            bytes.push_back(std::byte{ 0 }); // the created counter
        for (int i = 0; i < 4; ++i)
            bytes.push_back(std::byte{ 0xFF }); // the pose count
        std::string error;
        EXPECT_FALSE(decodes(bytes, &error));
    }

    TEST(CorpsePoseSave, anUnknownNewerVersionIsStillRefusedByName)
    {
        std::vector<std::byte> bytes = goodBytes();
        bytes[4] = std::byte{ 4 };
        std::string error;
        EXPECT_FALSE(decodes(bytes, &error));
        EXPECT_NE(error.find("version"), std::string::npos) << error;
    }

    TEST(CorpsePoseSave, posesOutOfOrderOrDuplicatedAreRefused)
    {
        // Two poses written by hand in the wrong order: write two states, swap the placements in the bytes.
        NeutralSaveState state = stateWith({ poseOf("skyrimse", 10, 1), poseOf("skyrimse", 11, 1) });
        std::vector<std::byte> bytes = encodeNeutralSave(state);
        // find the two placements (little endian u32 right after each game id string) and swap the low bytes
        std::vector<std::size_t> at;
        for (std::size_t i = 0; i + 4 < bytes.size(); ++i)
            if ((bytes[i] == std::byte{ 10 } || bytes[i] == std::byte{ 11 }) && bytes[i + 1] == std::byte{ 0 }
                && bytes[i + 2] == std::byte{ 0 } && bytes[i + 3] == std::byte{ 0 } && i > 40)
                at.push_back(i);
        ASSERT_GE(at.size(), 2u);
        std::swap(bytes[at[0]], bytes[at[1]]);
        EXPECT_FALSE(decodes(bytes));
        // a duplicate: make both 10
        bytes = encodeNeutralSave(state);
        bytes[at[1]] = std::byte{ 10 };
        EXPECT_FALSE(decodes(bytes));
    }

    TEST(CorpsePoseSave, aRotationThatIsNotFiniteIsRefused)
    {
        NeutralSaveState state = stateWith({ poseOf("skyrimse", 10, 1) });
        std::vector<std::byte> bytes = encodeNeutralSave(state);
        // the last 16 bytes are the single bone's rotation: make the last float a NaN
        const std::size_t n = bytes.size();
        bytes[n - 1] = std::byte{ 0x7F };
        bytes[n - 2] = std::byte{ 0xC0 };
        bytes[n - 3] = std::byte{ 0 };
        bytes[n - 4] = std::byte{ 0 };
        EXPECT_FALSE(decodes(bytes));
    }

    TEST(CorpsePoseSave, aBoneCountOfZeroOrOverTheCapIsRefused)
    {
        std::vector<std::byte> bytes = encodeNeutralSave(stateWith({ poseOf("skyrimse", 10, 1) }));
        // the bone count byte follows the game id string and the placement: u16 length + "skyrimse" + u32 placement
        // after u32 pose count; find it by the name "Bone0" (u16 5) which follows it
        std::size_t countAt = 0;
        for (std::size_t i = 1; i + 1 < bytes.size(); ++i)
            if (bytes[i] == std::byte{ 5 } && bytes[i + 1] == std::byte{ 0 } && bytes[i + 2] == std::byte{ 'B' })
            {
                countAt = i - 1;
                break;
            }
        ASSERT_GT(countAt, 0u);
        ASSERT_EQ(bytes[countAt], std::byte{ 1 });
        std::vector<std::byte> zero = bytes;
        zero[countAt] = std::byte{ 0 };
        EXPECT_FALSE(decodes(zero));
        std::vector<std::byte> many = bytes;
        many[countAt] = static_cast<std::byte>(kNeutralSaveMaxPoseBones + 1);
        EXPECT_FALSE(decodes(many));
    }

    TEST(CorpsePoseSave, aRandomDamageNeverCrashesTheDecoder)
    {
        const std::vector<std::byte> good = goodBytes();
        std::uint32_t seed = 12345u;
        for (int round = 0; round < 3000; ++round)
        {
            std::vector<std::byte> bytes = good;
            for (int k = 0; k < 3; ++k)
            {
                seed = seed * 1664525u + 1013904223u;
                bytes[(seed >> 8) % bytes.size()] = static_cast<std::byte>(seed >> 24);
            }
            NeutralSaveState back;
            std::string error;
            (void)decodeNeutralSave(bytes, back, error); // true or false, never a crash, and canonical when true
            if (!back.mCorpsePoses.empty())
            {
                EXPECT_LE(back.mCorpsePoses.size(), kNeutralSaveMaxPoses);
            }
        }
    }
}
