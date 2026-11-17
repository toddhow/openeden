// Task 0209 (M-S7): Skyrim SE music on generated plugins. No game data. The byte layouts are the ones measured on the
// installed masters (see game_packs/skyrimse/src/music.cpp); the expected values are written out by hand.
#include <game_skyrimse/music.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using namespace game_skyrimse::music;
    namespace sdk = openeden::sdk::game;

    constexpr std::uint32_t kGrupWorld = 1, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupExtBlock = 4,
                            kGrupExtSub = 5;

    // Form ids of the test plugin (load order index 1).
    constexpr FormId kTrackA = 0x01000800, kTrackSilent = 0x01000801, kTrackDay = 0x01000802,
                     kTrackPalette = 0x01000803, kTrackMissing = 0x01000804, kTrackNight = 0x01000805;
    constexpr FormId kTypeTavern = 0x01000900, kTypeCombat = 0x01000901, kTypeRegion = 0x01000902,
                     kTypeWorld = 0x01000903, kTypeLocation = 0x01000904, kTypeTown = 0x01000905,
                     kTypeOrdered = 0x01000906, kTypePalette = 0x01000907, kTypeOnce = 0x01000908;
    constexpr FormId kCellInn = 0x01000A00, kCellLocation = 0x01000A01, kCellRegion = 0x01000A02,
                     kCellWorld = 0x01000A03, kCellNothing = 0x01000A04, kCellBoth = 0x01000A05;
    constexpr FormId kRegion = 0x01000B00, kRegionOther = 0x01000B01, kLocation = 0x01000C00,
                     kLocationParent = 0x01000C01, kWorld = 0x01000D00;

    LoadOrder oneMaster()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    Bytes ctda(
        std::uint8_t compare, float value, std::uint16_t function, std::uint32_t param1 = 0, std::uint8_t flags = 0)
    {
        return Bytes()
            .u8(static_cast<std::uint8_t>((compare << 5) | flags))
            .u8(0)
            .u8(0)
            .u8(0)
            .f32(value)
            .u16(function)
            .u16(0)
            .u32(param1)
            .u32(0)
            .u32(0)
            .u32(0)
            .u32(0);
    }

    Bytes must(FormId id, const std::string& name, std::uint32_t kind, const std::string& file, float duration,
        const std::vector<Bytes>& conditions = {}, const std::vector<std::uint32_t>& palette = {})
    {
        Bytes subs = sub4("EDID", Bytes().zstring(name));
        subs.append(sub4("CNAM", Bytes().u32(kind)));
        if (!file.empty())
            subs.append(sub4("ANAM", Bytes().zstring(file)));
        if (duration > 0.f)
            subs.append(sub4("FLTV", Bytes().f32(duration)));
        if (!conditions.empty())
        {
            subs.append(sub4("CITC", Bytes().u32(static_cast<std::uint32_t>(conditions.size()))));
            for (const Bytes& c : conditions)
                subs.append(sub4("CTDA", c));
        }
        if (!palette.empty())
        {
            Bytes ids;
            for (std::uint32_t p : palette)
                ids.u32(p);
            subs.append(sub4("SNAM", ids));
        }
        return record4("MUST", 0, id, subs);
    }

    Bytes musc(FormId id, const std::string& name, std::uint32_t flags, std::uint16_t priority, std::uint16_t ducking,
        float fade, const std::vector<std::uint32_t>& tracks)
    {
        Bytes list;
        for (std::uint32_t t : tracks)
            list.u32(t);
        return record4("MUSC", 0, id,
            Bytes()
                .append(sub4("EDID", Bytes().zstring(name)))
                .append(sub4("FNAM", Bytes().u32(flags)))
                .append(sub4("PNAM", Bytes().u16(priority).u16(ducking)))
                .append(sub4("WNAM", Bytes().f32(fade)))
                .append(sub4("TNAM", list)));
    }

    Bytes cell(FormId id, const std::string& name, bool interior, FormId music, FormId location,
        const std::vector<FormId>& regions)
    {
        Bytes subs = sub4("EDID", Bytes().zstring(name));
        subs.append(sub4("DATA", Bytes().u16(interior ? 0x0001 : 0x0000)));
        if (music != 0)
            subs.append(sub4("XCMO", Bytes().u32(music)));
        if (location != 0)
            subs.append(sub4("XLCN", Bytes().u32(location)));
        if (!regions.empty())
        {
            Bytes ids;
            for (FormId r : regions)
                ids.u32(r);
            subs.append(sub4("XCLR", ids));
        }
        return record4("CELL", 0, id, subs);
    }

    Bytes testPlugin()
    {
        Bytes bytes = header();
        Bytes tracks;
        tracks.append(must(kTrackA, "TrackA", kKindSingle, "Data\\Music\\Tavern\\MUS_Tavern_01.wav", 0.f));
        tracks.append(must(kTrackSilent, "TrackSilent", kKindSilent, "", 30.f));
        // Day only: GetCurrentTime >= 6 AND GetCurrentTime <= 20 (compare codes 3 and 5).
        tracks.append(must(kTrackDay, "TrackDay", kKindSingle, "\\Data\\Music\\Town\\MUS_Town_Day_01.wav", 0.f,
            { ctda(3, 6.f, 18), ctda(5, 20.f, 18) }));
        // Palette: groups [A, silent] 0 [silent, A, A].
        tracks.append(must(kTrackPalette, "TrackPalette", kKindPalette, "", 210.f, {},
            { kTrackA, kTrackSilent, 0u, kTrackSilent, kTrackA, kTrackA }));
        tracks.append(must(kTrackMissing, "TrackMissing", kKindSingle, "Data\\Music\\Nowhere\\none.wav", 0.f));
        // Night only: GetCurrentTime < 6 (compare code 4).
        tracks.append(must(kTrackNight, "TrackNight", kKindSingle, "Data\\Music\\Explore\\MUS_Explore_Night_01.wav",
            0.f, { ctda(4, 6.f, 18) }));
        bytes.append(group4(Content::fourCC("MUST"), 0, tracks));

        Bytes types;
        types.append(musc(kTypeTavern, "TypeTavern", 0x00, 55, 0, 0.f, { kTrackA, kTrackA, kTrackSilent }));
        types.append(musc(kTypeCombat, "TypeCombat", 0x24, 2, 10000, 9.f, { kTrackA }));
        types.append(musc(kTypeRegion, "TypeRegion", 0x00, 48, 0, 3.f, { kTrackA }));
        types.append(musc(kTypeWorld, "TypeWorld", 0x00, 30, 0, 6.f, { kTrackA }));
        types.append(musc(kTypeLocation, "TypeLocation", 0x00, 50, 0, 0.f, { kTrackA }));
        types.append(musc(kTypeTown, "TypeTown", 0x00, 60, 0, 6.f, { kTrackDay, kTrackNight }));
        types.append(musc(kTypeOrdered, "TypeOrdered", 0x08, 60, 0, 0.f, { kTrackA, kTrackSilent, kTrackDay }));
        types.append(musc(kTypePalette, "TypePalette", 0x00, 60, 0, 0.f, { kTrackPalette }));
        types.append(musc(kTypeOnce, "TypeOnce", 0x01, 60, 0, 0.f, { kTrackMissing, kTrackA }));
        bytes.append(group4(Content::fourCC("MUSC"), 0, types));

        Bytes regions;
        regions.append(record4("REGN", 0, kRegion,
            Bytes().append(sub4("EDID", Bytes().zstring("R"))).append(sub4("RDMO", Bytes().u32(kTypeRegion)))));
        regions.append(record4("REGN", 0, kRegionOther,
            Bytes().append(sub4("EDID", Bytes().zstring("R2"))).append(sub4("RDMO", Bytes().u32(kTypeTown)))));
        bytes.append(group4(Content::fourCC("REGN"), 0, regions));

        Bytes locations;
        locations.append(record4("LCTN", 0, kLocation,
            Bytes().append(sub4("EDID", Bytes().zstring("L"))).append(sub4("PNAM", Bytes().u32(kLocationParent)))));
        locations.append(record4("LCTN", 0, kLocationParent,
            Bytes().append(sub4("EDID", Bytes().zstring("LP"))).append(sub4("NAM1", Bytes().u32(kTypeLocation)))));
        bytes.append(group4(Content::fourCC("LCTN"), 0, locations));

        bytes.append(group4(Content::fourCC("DOBJ"), 0,
            record4("DOBJ", 0, 0x01000E00,
                Bytes().append(sub4("DNAM", Bytes().fourCC("BTMS").u32(kTypeCombat).fourCC("DTMS").u32(kTypeTown))))));

        Bytes interior = group4(Content::fourCC("CELL"), 0,
            group4(0, kGrupInteriorBlock,
                group4(0, kGrupInteriorSub,
                    Bytes()
                        .append(cell(kCellInn, "Inn", true, kTypeTavern, kLocation, {}))
                        .append(cell(kCellNothing, "Nothing", true, 0, 0, {})))));
        bytes.append(interior);

        Bytes world = record4("WRLD", 0, kWorld,
            Bytes().append(sub4("EDID", Bytes().zstring("W"))).append(sub4("ZNAM", Bytes().u32(kTypeWorld))));
        world.append(group4(kWorld, kGrupWorld,
            group4(0, kGrupExtBlock,
                group4(0, kGrupExtSub,
                    Bytes()
                        .append(cell(kCellLocation, "ExtLocation", false, 0, kLocation, {}))
                        .append(cell(kCellRegion, "ExtRegion", false, 0, 0, { kRegion }))
                        .append(cell(kCellWorld, "ExtWorld", false, 0, 0, {}))
                        .append(cell(kCellBoth, "ExtBoth", false, 0, 0, { kRegion, kRegionOther }))))));
        bytes.append(group4(Content::fourCC("WRLD"), 0, world));
        return bytes;
    }

    MusicIndex indexOf(const LoadOrder& order, Records& records)
    {
        Content::Error error;
        EXPECT_TRUE(parseMusicRecordsBytes(testPlugin().str(), "test.esp", order, MusicLimits(), records, error))
            << error.describe();
        MusicIndex index;
        index.add(records);
        return index;
    }

    FileProbe everything()
    {
        return {};
    }

    TEST(SkyrimMusicRecords, decodesTypesTracksAndLinks)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        EXPECT_EQ(r.mTypes.size(), 9u);
        EXPECT_EQ(r.mTracks.size(), 6u);
        EXPECT_EQ(r.mCells.size(), 6u);
        EXPECT_EQ(r.mOddSubrecords, 0u);
        EXPECT_TRUE(r.mErrors.empty());
        const Type* combat = index.type(kTypeCombat);
        ASSERT_NE(combat, nullptr);
        EXPECT_EQ(combat->mEditorId, "TypeCombat");
        EXPECT_EQ(combat->mFlags, 0x24u);
        EXPECT_EQ(combat->mPriority, 2);
        EXPECT_EQ(combat->mDucking, 10000);
        EXPECT_FLOAT_EQ(combat->mFade, 9.f);
        const Type* tavern = index.type(kTypeTavern);
        ASSERT_NE(tavern, nullptr);
        ASSERT_EQ(tavern->mTracks.size(), 3u);
        EXPECT_EQ(tavern->mTracks[0], kTrackA);
        EXPECT_EQ(tavern->mTracks[2], kTrackSilent);
        const Track* a = index.track(kTrackA);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(a->mKind, TrackKind::Single);
        EXPECT_EQ(a->mFile, "Data\\Music\\Tavern\\MUS_Tavern_01.wav");
        const Track* silent = index.track(kTrackSilent);
        ASSERT_NE(silent, nullptr);
        EXPECT_EQ(silent->mKind, TrackKind::Silent);
        EXPECT_FLOAT_EQ(silent->mDuration, 30.f);
        const Track* palette = index.track(kTrackPalette);
        ASSERT_NE(palette, nullptr);
        EXPECT_EQ(palette->mKind, TrackKind::Palette);
        ASSERT_EQ(palette->mPalette.size(), 6u);
        EXPECT_EQ(palette->mPalette[2], kNullFormId);
        const Track* day = index.track(kTrackDay);
        ASSERT_NE(day, nullptr);
        EXPECT_EQ(day->mConditions.size(), 2u);
        EXPECT_EQ(day->mConditions[0].mFunction, 18);
        EXPECT_EQ(day->mConditions[0].mCompare, 3);
        EXPECT_EQ(index.combatType(), kTypeCombat);
        const CellLink* inn = index.cell(kCellInn);
        ASSERT_NE(inn, nullptr);
        EXPECT_TRUE(inn->mInterior);
        EXPECT_EQ(inn->mMusic, kTypeTavern);
        EXPECT_EQ(inn->mLocation, kLocation);
        const CellLink* ext = index.cell(kCellRegion);
        ASSERT_NE(ext, nullptr);
        EXPECT_FALSE(ext->mInterior);
        EXPECT_EQ(ext->mWorld, kWorld);
        ASSERT_EQ(ext->mRegions.size(), 1u);
        EXPECT_EQ(index.region(kRegion)->mMusic, kTypeRegion);
        EXPECT_EQ(index.location(kLocationParent)->mMusic, kTypeLocation);
        EXPECT_EQ(index.world(kWorld)->mMusic, kTypeWorld);
    }

    TEST(SkyrimMusicRecords, aShortSubrecordIsOddAndTheWalkGoesOn)
    {
        Bytes bad = musc(0x01000F00, "Bad", 0, 1, 0, 0.f, {});
        Bytes types;
        types.append(record4("MUSC", 0, 0x01000F01, Bytes().append(sub4("FNAM", Bytes().u16(3)))));
        types.append(bad);
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("MUSC"), 0, types));
        const LoadOrder order = oneMaster();
        Records r;
        Content::Error error;
        ASSERT_TRUE(parseMusicRecordsBytes(bytes.str(), "test.esp", order, MusicLimits(), r, error));
        EXPECT_EQ(r.mTypes.size(), 2u);
        EXPECT_EQ(r.mOddSubrecords, 1u);
    }

    TEST(SkyrimMusicPlace, aCellsOwnMusicWins)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        const MusicChooser chooser(index);
        const PlaceType place = chooser.placeType(kCellInn);
        EXPECT_EQ(place.mType, kTypeTavern);
        EXPECT_STREQ(place.mReason, "cell");
    }

    TEST(SkyrimMusicPlace, aLocationsMusicComesFromItsParent)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        const MusicChooser chooser(index);
        const PlaceType place = chooser.placeType(kCellLocation);
        EXPECT_EQ(place.mType, kTypeLocation);
        EXPECT_STREQ(place.mReason, "location");
    }

    TEST(SkyrimMusicPlace, aRegionBeatsTheWorldAndTheLowestPriorityNumberWinsAmongRegions)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        const MusicChooser chooser(index);
        const PlaceType one = chooser.placeType(kCellRegion);
        EXPECT_EQ(one.mType, kTypeRegion);
        EXPECT_STREQ(one.mReason, "region");
        // Regions R (type priority 48) and R2 (type priority 60): the lower number is more important.
        const PlaceType both = chooser.placeType(kCellBoth);
        EXPECT_EQ(both.mType, kTypeRegion);
    }

    TEST(SkyrimMusicPlace, anExteriorWithNoOtherLinkTakesItsWorldsMusic)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        const MusicChooser chooser(index);
        const PlaceType place = chooser.placeType(kCellWorld);
        EXPECT_EQ(place.mType, kTypeWorld);
        EXPECT_STREQ(place.mReason, "world");
    }

    TEST(SkyrimMusicPlace, anInteriorWithNoLinkAndAnUnknownCellHaveNoMusic)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        const MusicChooser chooser(index);
        EXPECT_EQ(chooser.placeType(kCellNothing).mType, kNullFormId);
        EXPECT_EQ(chooser.placeType(0x01009999).mType, kNullFormId);
    }

    TEST(SkyrimMusicPlace, theCombatTypeIsTheDefaultObjectsBattleMusic)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        EXPECT_EQ(MusicChooser(index).combatType(), kTypeCombat);
    }

    TEST(SkyrimMusicFiles, theXwmNameIsTriedFirstThenTheStoredName)
    {
        const FileProbe both = [](const std::string&) { return true; };
        EXPECT_EQ(musicFilePath("Data\\Music\\Tavern\\MUS_Tavern_01.wav", both), "music/tavern/mus_tavern_01.xwm");
        EXPECT_EQ(musicFilePath("\\Data\\Music\\Combat\\MUS_Combat_04_Finale.wav", both),
            "music/combat/mus_combat_04_finale.xwm");
        const FileProbe wavOnly = [](const std::string& p) { return p == "music/a/b.wav"; };
        EXPECT_EQ(musicFilePath("Data\\Music\\A\\B.wav", wavOnly), "music/a/b.wav");
        const FileProbe none = [](const std::string&) { return false; };
        EXPECT_EQ(musicFilePath("Data\\Music\\A\\B.wav", none), "");
    }

    TEST(SkyrimMusicFiles, hostileNamesGiveNothing)
    {
        const FileProbe any = [](const std::string&) { return true; };
        EXPECT_EQ(musicFilePath("", any), "");
        EXPECT_EQ(musicFilePath("Data\\Music\\..\\..\\x.wav", any), "");
        EXPECT_EQ(musicFilePath("C:\\x.wav", any), "");
        EXPECT_EQ(musicFilePath(std::string("Data\\Music\\a\x01.wav"), any), "");
    }

    TEST(SkyrimMusicPlan, aSingleTrackIsOneFileItem)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        sdk::MusicPlan plan;
        // TypeCombat has the one track A.
        ASSERT_TRUE(chooser.plan(kTypeCombat, ConditionFacts{}, 1, 0, everything(), plan));
        EXPECT_EQ(plan.trackName, "TrackA");
        ASSERT_EQ(plan.items.size(), 1u);
        EXPECT_EQ(plan.items[0].kind, sdk::MusicItemKind::File);
        EXPECT_EQ(plan.items[0].file, "music/tavern/mus_tavern_01.xwm");
    }

    TEST(SkyrimMusicPlan, conditionsOnTheGameHourSelectTheTrack)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        for (std::uint32_t pick = 0; pick < 8; ++pick)
        {
            sdk::MusicPlan day;
            ConditionFacts noon;
            noon.mGameHour = 12.f;
            ASSERT_TRUE(chooser.plan(kTypeTown, noon, 7, pick, everything(), day));
            EXPECT_EQ(day.trackName, "TrackDay") << "pick " << pick;
            sdk::MusicPlan night;
            ConditionFacts dark;
            dark.mGameHour = 2.f;
            ASSERT_TRUE(chooser.plan(kTypeTown, dark, 7, pick, everything(), night));
            EXPECT_EQ(night.trackName, "TrackNight") << "pick " << pick;
        }
        // 22:00 passes neither list.
        sdk::MusicPlan late;
        ConditionFacts evening;
        evening.mGameHour = 22.f;
        EXPECT_FALSE(chooser.plan(kTypeTown, evening, 7, 0, everything(), late));
        EXPECT_GT(chooser.mTracksRejectedByConditions, 0u);
    }

    TEST(SkyrimMusicPlan, theSameSeedAndPickGiveTheSamePlanAndOtherPicksVary)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser one(index);
        MusicChooser two(index);
        bool varied = false;
        std::string first;
        for (std::uint32_t pick = 0; pick < 32; ++pick)
        {
            sdk::MusicPlan a, b;
            ASSERT_TRUE(one.plan(kTypeTavern, ConditionFacts{}, 99, pick, everything(), a));
            ASSERT_TRUE(two.plan(kTypeTavern, ConditionFacts{}, 99, pick, everything(), b));
            EXPECT_EQ(a, b);
            if (pick == 0)
                first = a.trackName;
            else if (a.trackName != first)
                varied = true;
        }
        EXPECT_TRUE(varied) << "a type with two different tracks plays both over 32 picks";
        sdk::MusicPlan other;
        // A different seed gives a different sequence somewhere in 32 picks.
        bool differs = false;
        for (std::uint32_t pick = 0; pick < 32 && !differs; ++pick)
        {
            sdk::MusicPlan a, b;
            ASSERT_TRUE(one.plan(kTypeTavern, ConditionFacts{}, 99, pick, everything(), a));
            ASSERT_TRUE(one.plan(kTypeTavern, ConditionFacts{}, 100, pick, everything(), b));
            differs = !(a == b);
        }
        EXPECT_TRUE(differs);
    }

    TEST(SkyrimMusicPlan, aRepeatedTrackIdIsAHeavierWeight)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        int single = 0, silent = 0;
        for (std::uint32_t pick = 0; pick < 600; ++pick)
        {
            sdk::MusicPlan plan;
            ASSERT_TRUE(chooser.plan(kTypeTavern, ConditionFacts{}, 5, pick, everything(), plan));
            (plan.trackName == "TrackA" ? single : silent)++;
        }
        // TNAM is A, A, silent: about two thirds A.
        EXPECT_GT(single, 330);
        EXPECT_LT(single, 470);
        EXPECT_EQ(single + silent, 600);
    }

    TEST(SkyrimMusicPlan, aTypeThatKeepsItsOrderGoesRoundByThePickCounter)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        ConditionFacts noon;
        noon.mGameHour = 12.f;
        const char* expected[] = { "TrackA", "TrackSilent", "TrackDay", "TrackA", "TrackSilent", "TrackDay" };
        for (std::uint32_t pick = 0; pick < 6; ++pick)
        {
            sdk::MusicPlan plan;
            ASSERT_TRUE(chooser.plan(kTypeOrdered, noon, 3, pick, everything(), plan));
            EXPECT_EQ(plan.trackName, expected[pick]) << pick;
        }
    }

    TEST(SkyrimMusicPlan, aSilentTrackIsASilenceItem)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        ConditionFacts noon;
        noon.mGameHour = 12.f;
        sdk::MusicPlan plan;
        ASSERT_TRUE(chooser.plan(kTypeOrdered, noon, 3, 1, everything(), plan));
        ASSERT_EQ(plan.items.size(), 1u);
        EXPECT_EQ(plan.items[0].kind, sdk::MusicItemKind::Silence);
        EXPECT_FLOAT_EQ(plan.items[0].seconds, 30.f);
    }

    TEST(SkyrimMusicPlan, aPaletteIsOneOfItsGroupsInOrder)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        bool sawFirst = false, sawSecond = false;
        for (std::uint32_t pick = 0; pick < 40; ++pick)
        {
            sdk::MusicPlan plan;
            ASSERT_TRUE(chooser.plan(kTypePalette, ConditionFacts{}, 11, pick, everything(), plan));
            EXPECT_EQ(plan.trackName, "TrackPalette");
            if (plan.items.size() == 2)
            {
                // Group one: [A, silent].
                EXPECT_EQ(plan.items[0].kind, sdk::MusicItemKind::File);
                EXPECT_EQ(plan.items[1].kind, sdk::MusicItemKind::Silence);
                sawFirst = true;
            }
            else
            {
                // Group two: [silent, A, A].
                ASSERT_EQ(plan.items.size(), 3u);
                EXPECT_EQ(plan.items[0].kind, sdk::MusicItemKind::Silence);
                EXPECT_EQ(plan.items[1].kind, sdk::MusicItemKind::File);
                EXPECT_EQ(plan.items[2].kind, sdk::MusicItemKind::File);
                sawSecond = true;
            }
        }
        EXPECT_TRUE(sawFirst);
        EXPECT_TRUE(sawSecond);
    }

    TEST(SkyrimMusicPlan, aTrackWithNoFileIsSkippedForTheNextCandidate)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        MusicChooser chooser(index);
        const FileProbe tavernOnly = [](const std::string& p) { return p.rfind("music/tavern/", 0) == 0; };
        for (std::uint32_t pick = 0; pick < 8; ++pick)
        {
            sdk::MusicPlan plan;
            ASSERT_TRUE(chooser.plan(kTypeOnce, ConditionFacts{}, 2, pick, tavernOnly, plan));
            EXPECT_EQ(plan.trackName, "TrackA");
        }
        EXPECT_GT(chooser.mTracksWithoutFile, 0u);
        sdk::MusicPlan none;
        const FileProbe nothing = [](const std::string&) { return false; };
        EXPECT_FALSE(chooser.plan(kTypeOnce, ConditionFacts{}, 2, 0, nothing, none));
    }

    TEST(SkyrimMusicDescribe, theTypeCarriesPriorityDuckingFadeAndFlags)
    {
        const LoadOrder order = oneMaster();
        Records r;
        const MusicIndex index = indexOf(order, r);
        const MusicChooser chooser(index);
        sdk::MusicTypeDescription d;
        ASSERT_TRUE(chooser.describe(kTypeCombat, "combat", true, d));
        EXPECT_EQ(d.editorId, "TypeCombat");
        EXPECT_EQ(d.priority, 2);
        EXPECT_EQ(d.duckingCentiDb, 10000);
        EXPECT_FLOAT_EQ(d.fadeSeconds, 9.f);
        EXPECT_EQ(d.flags, 0x24u);
        EXPECT_TRUE(d.combat);
        EXPECT_EQ(d.reason, "combat");
        EXPECT_FALSE(chooser.describe(0x01009999, "x", false, d));
    }

    TEST(SkyrimMusicRuntime, readsOnAWorkerAndChoosesTheCombatTypeWhileFighting)
    {
        // The file is named like its load order entry, or its own ids do not resolve.
        const std::filesystem::path dir = std::filesystem::temp_directory_path() / "openeden_music_test_runtime";
        std::filesystem::create_directories(dir);
        const std::filesystem::path path = dir / "test.esp";
        {
            std::ofstream out(path, std::ios::binary);
            const std::string bytes = testPlugin().str();
            out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        }
        {
            MusicRuntime runtime({ path }, oneMaster());
            EXPECT_FALSE(runtime.ready());
            sdk::MusicTypeDescription early;
            EXPECT_FALSE(runtime.type(kCellInn, false, early)) << "not ready: no answer, no wait";
            runtime.start();
            for (int i = 0; i < 500 && !runtime.ready(); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            ASSERT_TRUE(runtime.ready());
            sdk::MusicTypeDescription calm;
            ASSERT_TRUE(runtime.type(kCellInn, false, calm));
            EXPECT_EQ(calm.editorId, "TypeTavern");
            EXPECT_FALSE(calm.combat);
            sdk::MusicTypeDescription fight;
            ASSERT_TRUE(runtime.type(kCellInn, true, fight));
            EXPECT_EQ(fight.editorId, "TypeCombat");
            EXPECT_TRUE(fight.combat);
            // A cell with no place music still gets the combat type in a fight.
            sdk::MusicTypeDescription nowhere;
            EXPECT_FALSE(runtime.type(kCellNothing, false, nowhere));
            EXPECT_TRUE(runtime.type(kCellNothing, true, nowhere));
        }
        std::filesystem::remove_all(dir);
    }
}
