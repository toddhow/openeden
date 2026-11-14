// Task 0135 stage S5: Skyrim SE AI packages (PACK) decoded from generated plugins, the package index, and the procedure
// classification. No game data. Layouts: game_skyrimse/package.hpp.
#include <game_skyrimse/package.hpp>

#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;
    constexpr FormId kSelf = 0x01000000;
    constexpr FormId kPackSandbox = kSelf | 0x900;
    constexpr FormId kPackTemplateSandbox = kSelf | 0x901;
    constexpr FormId kPackTemplateTravel = kSelf | 0x902;
    constexpr FormId kPackLoop1 = kSelf | 0x903;
    constexpr FormId kPackLoop2 = kSelf | 0x904;
    constexpr FormId kRefMarker = kSelf | 0x2A0;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    Bytes ctda(std::uint16_t function, float value)
    {
        Bytes b;
        b.u8(0).u8(0).u8(0).u8(0).f32(value);
        b.u16(function).u16(0).u32(0).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", b);
    }

    struct PackSpec
    {
        std::uint32_t mId = kPackSandbox;
        std::string mEditorId = "TestSandbox";
        std::uint32_t mFlags = 0x2000;
        std::uint8_t mType = 18;
        std::uint8_t mSpeed = 2;
        std::uint16_t mInterrupt = 0xEF;
        std::int8_t mDow = -1;
        std::int8_t mHour = -1;
        std::int8_t mMinute = -1;
        std::int32_t mDuration = 0;
        std::vector<Bytes> mConditions; ///< before PKCU
        std::vector<Bytes> mBranchConditions; ///< after the tree: not the package's
        std::uint32_t mTemplate = 0;
        std::vector<std::string> mProcedures;
        std::vector<std::array<std::int32_t, 3>> mLocations; ///< type, value, radius
        bool mDeleted = false;
        std::size_t mPkdtSize = 12;
        bool mGarbageInPsdt = true;
    };

    Bytes packRecord(const PackSpec& p)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring(p.mEditorId)));
        Bytes pkdt;
        pkdt.u32(p.mFlags).u8(p.mType).u8(0).u8(p.mSpeed).u8(0xFF).u16(p.mInterrupt).u16(0);
        if (p.mPkdtSize != 12)
            pkdt = Bytes().u32(p.mFlags);
        b.append(sub4("PKDT", pkdt));
        Bytes psdt;
        psdt.u8(0xFF)
            .u8(static_cast<std::uint8_t>(p.mDow))
            .u8(0)
            .u8(static_cast<std::uint8_t>(p.mHour))
            .u8(static_cast<std::uint8_t>(p.mMinute));
        if (p.mGarbageInPsdt)
            psdt.u8(0xAB).u8(0x74).u8(0x69);
        else
            psdt.u8(0).u8(0).u8(0);
        psdt.u32(static_cast<std::uint32_t>(p.mDuration));
        b.append(sub4("PSDT", psdt));
        for (const Bytes& c : p.mConditions)
            b.append(c);
        b.append(sub4("PKCU", Bytes().u32(1).u32(p.mTemplate).u32(10)));
        for (const auto& loc : p.mLocations)
        {
            b.append(sub4("ANAM", Bytes().zstring("Location")));
            b.append(sub4("PLDT",
                Bytes()
                    .u32(static_cast<std::uint32_t>(loc[0]))
                    .u32(static_cast<std::uint32_t>(loc[1]))
                    .u32(static_cast<std::uint32_t>(loc[2]))));
        }
        b.append(sub4("ANAM", Bytes().zstring("Bool")));
        b.append(sub4("CNAM", Bytes().u8(1)));
        b.append(sub4("XNAM", Bytes().u8(5)));
        for (const std::string& name : p.mProcedures)
        {
            b.append(sub4("PRCB", Bytes().u32(1).u32(0)));
            b.append(sub4("PNAM", Bytes().zstring(name)));
            b.append(sub4("FNAM", Bytes().u32(0)));
            b.append(sub4("PNAM", Bytes().u8(1))); // a padding PNAM: not alphabetic
        }
        b.append(sub4("CITC", Bytes().u32(static_cast<std::uint32_t>(p.mBranchConditions.size()))));
        for (const Bytes& c : p.mBranchConditions)
            b.append(c);
        return record4("PACK", p.mDeleted ? 0x20u : 0u, p.mId, b);
    }

    Bytes packPlugin(const std::vector<PackSpec>& packs)
    {
        Bytes bytes = header();
        Bytes records;
        for (const PackSpec& p : packs)
            records.append(packRecord(p));
        bytes.append(group4(Content::fourCC("PACK"), kGrupTop, records));
        return bytes;
    }

    struct Parsed
    {
        PackageRecords mRecords;
        std::unique_ptr<Content::Plugin> mPlugin;
        bool mOk = false;
    };

    Parsed parse(const Bytes& bytes, PackageLimits limits = PackageLimits())
    {
        Parsed p;
        Content::Error error;
        p.mOk = parsePackageRecordsBytes(bytes.str(), "test.esp", order(), limits, p.mRecords, error);
        p.mPlugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        return p;
    }

    PackageIndex indexOf(const Bytes& bytes)
    {
        Parsed p = parse(bytes);
        PackageIndex index;
        index.add(std::move(p.mRecords), FormIdResolver(p.mPlugin->header(), "test.esp", order()));
        return index;
    }

    TEST(SkyrimPackageParse, theHeaderAndTheFlagsOfAPackageAreRead)
    {
        PackSpec s;
        s.mFlags = 0x2400;
        s.mSpeed = 3;
        s.mInterrupt = 0x1234;
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        const PackageRecord& r = p.mRecords.mPackages[0];
        EXPECT_EQ(r.mCommon.mId, kPackSandbox);
        EXPECT_EQ(r.mCommon.mEditorId, "TestSandbox");
        EXPECT_TRUE(r.mHasPkdt);
        EXPECT_EQ(r.mFlags, 0x2400u);
        EXPECT_EQ(r.mType, 18);
        EXPECT_EQ(r.mPreferredSpeed, 3);
        EXPECT_EQ(r.mInterruptFlags, 0x1234);
    }

    TEST(SkyrimPackageParse, theScheduleIsReadAndItsGarbageBytesAreNot)
    {
        PackSpec s;
        s.mDow = 7;
        s.mHour = 20;
        s.mMinute = 30;
        s.mDuration = 480;
        for (const bool garbage : { true, false })
        {
            s.mGarbageInPsdt = garbage;
            const Parsed p = parse(packPlugin({ s }));
            ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
            const PackageSchedule& sch = p.mRecords.mPackages[0].mSchedule;
            EXPECT_EQ(sch.mMonth, -1);
            EXPECT_EQ(sch.mDayOfWeek, 7);
            EXPECT_EQ(sch.mHour, 20);
            EXPECT_EQ(sch.mMinute, 30);
            EXPECT_EQ(sch.mDurationMinutes, 480);
        }
    }

    TEST(SkyrimPackageParse, anAnyHourScheduleIsMinusOne)
    {
        PackSpec s;
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        EXPECT_EQ(p.mRecords.mPackages[0].mSchedule.mHour, -1);
        EXPECT_EQ(p.mRecords.mPackages[0].mSchedule.mDurationMinutes, 0);
    }

    TEST(SkyrimPackageParse, theConditionsBeforePkcuAreThePackagesAndTheOnesAfterAreNot)
    {
        PackSpec s;
        s.mConditions = { ctda(58, 1.f), ctda(74, 2.f) };
        s.mBranchConditions = { ctda(14, 9.f) };
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        const PackageRecord& r = p.mRecords.mPackages[0];
        ASSERT_EQ(r.mConditions.size(), 2u);
        EXPECT_EQ(r.mConditions[0].mFunction, 58);
        EXPECT_EQ(r.mConditions[1].mFunction, 74);
        EXPECT_TRUE(r.mConditionsOk);
    }

    TEST(SkyrimPackageParse, locationsAreReadAndFormValuesAreResolved)
    {
        PackSpec s;
        s.mLocations = { { 0, static_cast<std::int32_t>(kRefMarker), 256 }, { 3, 0, 512 }, { 12, 0, 64 } };
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        const auto& locs = p.mRecords.mPackages[0].mLocations;
        ASSERT_EQ(locs.size(), 3u);
        EXPECT_EQ(locs[0].mType, PackageLocationType::NearReference);
        EXPECT_EQ(locs[0].mValue, kRefMarker);
        EXPECT_EQ(locs[0].mRadius, 256);
        EXPECT_EQ(locs[1].mType, PackageLocationType::NearEditorLocation);
        EXPECT_EQ(locs[1].mRadius, 512);
        EXPECT_EQ(locs[2].mType, PackageLocationType::NearSelf);
    }

    TEST(SkyrimPackageParse, aFormValueWithAMasterIndexPastTheMastersIsThePluginsOwn)
    {
        // The resolver's rule (records.hpp): every index at or above the master count is the plugin itself.
        PackSpec s;
        s.mLocations = { { 0, static_cast<std::int32_t>(0x05000123u), 100 } };
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        EXPECT_EQ(p.mRecords.mPackages[0].mLocations[0].mValue, 0x01000123u);
    }

    TEST(SkyrimPackageParse, aNullFormValueStaysNull)
    {
        PackSpec s;
        s.mLocations = { { 0, 0, 100 } };
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        EXPECT_EQ(p.mRecords.mPackages[0].mLocations[0].mValue, kNullFormId);
        EXPECT_EQ(p.mRecords.mUnresolvedFormIds, 0u);
    }

    TEST(SkyrimPackageParse, onlyAlphabeticProcedureNamesAreKept)
    {
        PackSpec s;
        s.mProcedures = { "Travel", "UnlockDoors", "Sandbox" };
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        const std::vector<std::string> expected{ "Travel", "UnlockDoors", "Sandbox" };
        EXPECT_EQ(p.mRecords.mPackages[0].mProcedures, expected);
    }

    TEST(SkyrimPackageParse, theTemplateIsResolved)
    {
        PackSpec s;
        s.mTemplate = kPackTemplateSandbox;
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        EXPECT_EQ(p.mRecords.mPackages[0].mTemplate, kPackTemplateSandbox);
    }

    TEST(SkyrimPackageParse, aWrongSizedPkdtIsOddAndNeverGuessed)
    {
        PackSpec s;
        s.mPkdtSize = 4;
        const Parsed p = parse(packPlugin({ s }));
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        EXPECT_FALSE(p.mRecords.mPackages[0].mHasPkdt);
        EXPECT_EQ(p.mRecords.mOddSubrecords, 1u);
        EXPECT_EQ(p.mRecords.mOddBy.count("PACK.PKDT"), 1u);
    }

    TEST(SkyrimPackageParse, tooManyConditionsFailTheRecordNotTheWalk)
    {
        PackSpec bad;
        bad.mConditions = { ctda(1, 0), ctda(1, 0), ctda(1, 0) };
        PackSpec good;
        good.mId = kPackTemplateTravel;
        good.mEditorId = "Good";
        PackageLimits limits;
        limits.mMaxConditions = 2;
        const Parsed p = parse(packPlugin({ bad, good }), limits);
        ASSERT_EQ(p.mRecords.mPackages.size(), 1u);
        EXPECT_EQ(p.mRecords.mPackages[0].mCommon.mEditorId, "Good");
        EXPECT_EQ(p.mRecords.mCounts.at(Content::fourCC("PACK")).mFailed, 1u);
        EXPECT_EQ(p.mRecords.mErrors.size(), 1u);
    }

    TEST(SkyrimPackageParse, anotherGameFileIsRefused)
    {
        // A TES3 file is not a Skyrim SE plugin.
        Parsed p;
        Content::Error error;
        const std::string tes3 = std::string("TES3\x20\0\0\0\0\0\0\0\0\0\0\0", 16) + std::string(32, '\0');
        p.mOk = parsePackageRecordsBytes(tes3, "morrowind.esm", order(), PackageLimits(), p.mRecords, error);
        EXPECT_FALSE(p.mOk);
    }

    // ---- the index -------------------------------------------------------------------------------------------------

    TEST(SkyrimPackageIndex, aLaterPluginReplacesAPackageAndADeletedOneGoes)
    {
        PackSpec a;
        a.mEditorId = "First";
        PackSpec b;
        b.mId = kPackTemplateTravel;
        b.mEditorId = "Other";
        Parsed p1 = parse(packPlugin({ a, b }));
        PackSpec a2;
        a2.mEditorId = "Second";
        PackSpec gone;
        gone.mId = kPackTemplateTravel;
        gone.mDeleted = true;
        Parsed p2 = parse(packPlugin({ a2, gone }));
        PackageIndex index;
        index.add(std::move(p1.mRecords), FormIdResolver(p1.mPlugin->header(), "test.esp", order()));
        index.add(std::move(p2.mRecords), FormIdResolver(p2.mPlugin->header(), "test.esp", order()));
        ASSERT_NE(index.package(kPackSandbox), nullptr);
        EXPECT_EQ(index.package(kPackSandbox)->mCommon.mEditorId, "Second");
        EXPECT_EQ(index.package(kPackTemplateTravel), nullptr);
        EXPECT_EQ(index.packageCount(), 1u);
    }

    TEST(SkyrimPackageIndex, aPackageUsesItsTemplatesProcedures)
    {
        PackSpec tmpl;
        tmpl.mId = kPackTemplateSandbox;
        tmpl.mEditorId = "Sandbox";
        tmpl.mType = 19;
        tmpl.mProcedures = { "Travel", "UnlockDoors", "Sandbox" };
        PackSpec inst;
        inst.mTemplate = kPackTemplateSandbox;
        const PackageIndex index = indexOf(packPlugin({ tmpl, inst }));
        const PackageRecord* r = index.package(kPackSandbox);
        ASSERT_NE(r, nullptr);
        EXPECT_EQ(index.behaviorOf(*r), PackageBehavior::Sandbox);
    }

    TEST(SkyrimPackageIndex, aPackageWithoutATemplateUsesItsOwnProcedures)
    {
        PackSpec own;
        own.mProcedures = { "Sleep" };
        const PackageIndex index = indexOf(packPlugin({ own }));
        EXPECT_EQ(index.behaviorOf(*index.package(kPackSandbox)), PackageBehavior::Sleep);
    }

    TEST(SkyrimPackageIndex, aMissingOrCyclicTemplateIsStand)
    {
        PackSpec missing;
        missing.mTemplate = kSelf | 0xFFF;
        PackSpec l1;
        l1.mId = kPackLoop1;
        l1.mTemplate = kPackLoop2;
        PackSpec l2;
        l2.mId = kPackLoop2;
        l2.mTemplate = kPackLoop1;
        const PackageIndex index = indexOf(packPlugin({ missing, l1, l2 }));
        EXPECT_EQ(index.behaviorOf(*index.package(kPackSandbox)), PackageBehavior::Stand);
        EXPECT_EQ(index.behaviorOf(*index.package(kPackLoop1)), PackageBehavior::Stand);
    }

    // ---- classification ---------------------------------------------------------------------------------------------

    PackageBehavior classify(std::vector<std::string> names)
    {
        return classifyProcedures(names);
    }

    TEST(SkyrimPackageClassify, theTemplatesOfTheMasters)
    {
        EXPECT_EQ(classify({ "Travel" }), PackageBehavior::Travel);
        EXPECT_EQ(classify({ "Travel", "UnlockDoors", "Sandbox" }), PackageBehavior::Sandbox);
        EXPECT_EQ(classify({ "Travel", "LockDoors", "Find", "Sandbox", "Sleep" }), PackageBehavior::Sleep);
        EXPECT_EQ(
            classify({ "Travel", "UnlockDoors", "Find", "Sandbox", "Acquire", "Find", "Sandbox", "Sit", "Eat", "Sit" }),
            PackageBehavior::Eat);
        EXPECT_EQ(
            classify({ "Travel", "ForceGreet", "Travel", "ForceGreet", "Sandbox", "Travel" }), PackageBehavior::Stand);
        EXPECT_EQ(classify({ "Patrol" }), PackageBehavior::Stand);
        EXPECT_EQ(classify({ "HoldPosition" }), PackageBehavior::Stand);
        EXPECT_EQ(classify({ "Find", "Travel", "Wander", "Sit", "Wait" }), PackageBehavior::Wander);
        EXPECT_EQ(classify({}), PackageBehavior::Stand);
    }

    TEST(SkyrimPackageClassify, namesAreCaseInsensitiveAndUnknownKindsAreUnsupported)
    {
        EXPECT_EQ(classify({ "sANDBOX" }), PackageBehavior::Sandbox);
        EXPECT_EQ(classify({ "UseWeapon" }), PackageBehavior::Unsupported);
        EXPECT_EQ(classify({ "Shout", "Hover" }), PackageBehavior::Unsupported);
    }
}
