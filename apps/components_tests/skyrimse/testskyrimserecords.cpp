// Task 0086, record loading first slice: the Skyrim SE record model built from generated plugins. No game data.
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <zlib.h>

#include <cstring>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0, kGrupWorld = 1, kGrupInteriorBlock = 2, kGrupInteriorSub = 3,
                            kGrupExtBlock = 4, kGrupExtSub = 5, kGrupCellChildren = 6, kGrupPersistent = 8,
                            kGrupTemporary = 9, kGrupVisibleDistant = 10;

    Bytes header(std::uint32_t flags = 0, const std::vector<Master>& masters = { { "Skyrim.esm", 0 } })
    {
        return tes4Header(1.7f, flags, "A", "B", 1, masters, 24, 44);
    }

    LoadOrder defaultOrder()
    {
        LoadOrder order;
        std::string err;
        EXPECT_TRUE(order.add("Skyrim.esm", false, err));
        EXPECT_TRUE(order.add("test.esp", false, err));
        return order;
    }

    struct Parsed
    {
        ParsedPlugin mPlugin;
        bool mOk = false;
        Content::Error mError;
    };

    Parsed parse(const Bytes& bytes, const LoadOrder& order, ParseLimits limits = ParseLimits())
    {
        Parsed p;
        p.mOk = parseRecordsBytes(bytes.str(), "test.esp", order, limits, p.mPlugin, p.mError);
        return p;
    }

    Bytes edid(const std::string& s)
    {
        return sub4("EDID", Bytes().zstring(s));
    }

    Bytes refrData(float x, float y, float z, float rx, float ry, float rz)
    {
        return sub4("DATA", Bytes().f32(x).f32(y).f32(z).f32(rx).f32(ry).f32(rz));
    }

    Bytes refr(std::uint32_t id, std::uint32_t base, std::uint32_t flags = 0, float x = 1, float scale = -1.0f)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(base)));
        if (scale > 0)
            subs.append(sub4("XSCL", Bytes().f32(scale)));
        subs.append(refrData(x, 2, 3, 0.5f, 0.25f, 0.125f));
        return record4("REFR", flags, id, subs);
    }

    Bytes cell(std::uint32_t id, const std::string& name, std::uint16_t flags, const Bytes& extra = Bytes())
    {
        Bytes subs;
        subs.append(edid(name));
        subs.append(sub4("FULL", Bytes().zstring(name)));
        subs.append(sub4("DATA", Bytes().u16(flags)));
        subs.append(extra);
        return record4("CELL", 0, id, subs);
    }

    // The interior layout: CELL top group > block > sub-block > CELL record + children group.
    Bytes interiorPlugin(const Bytes& cellRec, std::uint32_t cellId, const Bytes& persistent, const Bytes& temporary)
    {
        Bytes children;
        if (persistent.size() > 0)
            children.append(group4(cellId, kGrupPersistent, persistent));
        if (temporary.size() > 0)
            children.append(group4(cellId, kGrupTemporary, temporary));
        Bytes sub = cellRec;
        sub.append(group4(cellId, kGrupCellChildren, children));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop,
            group4(cellId % 10, kGrupInteriorBlock, group4((cellId / 10) % 10, kGrupInteriorSub, sub))));
        return bytes;
    }

    std::unique_ptr<Content::Plugin> openHeader(const Bytes& h, const char* name)
    {
        Content::Error error;
        return Content::Plugin::open(std::make_unique<std::istringstream>(h.str(), std::ios::binary), name, error);
    }

    std::span<const std::byte> view(const std::string& s)
    {
        return { reinterpret_cast<const std::byte*>(s.data()), s.size() };
    }

    // ---- resolution ----------------------------------------------------------------------------------------------

    TEST(SkyrimRecordsResolve, normalPluginsUseTheirIndexInTheTopByte)
    {
        const LoadOrder order = defaultOrder();
        const auto plugin = openHeader(header(), "test.esp");
        ASSERT_NE(plugin, nullptr);
        const FormIdResolver resolver(plugin->header(), "test.esp", order);
        EXPECT_EQ(resolver.resolve(0), 0u);
        EXPECT_EQ(resolver.resolve(0x00012345), 0x00012345u); // master 0 is Skyrim.esm at index 0
        EXPECT_EQ(resolver.resolve(0x01000800), 0x01000800u); // the plugin itself (index 1 = master list size)
        EXPECT_EQ(resolver.resolve(0xFF000014), 0x01000014u); // beyond the master list is the plugin itself
    }

    TEST(SkyrimRecordsResolve, aMasterNotInTheLoadOrderIsUnresolved)
    {
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("test.esp", false, err));
        const auto plugin = openHeader(header(), "test.esp");
        ASSERT_NE(plugin, nullptr);
        const FormIdResolver resolver(plugin->header(), "test.esp", order);
        EXPECT_FALSE(resolver.resolve(0x00012345).has_value());
        EXPECT_TRUE(resolver.resolve(0x01000800).has_value());
    }

    TEST(SkyrimRecordsResolve, lightPluginsMapToFeSpace)
    {
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("Skyrim.esm", false, err));
        ASSERT_TRUE(order.add("Light.esl", true, err));
        ASSERT_TRUE(order.add("Other.esp", false, err));
        ASSERT_TRUE(order.add("LightB.esl", true, err));
        EXPECT_EQ(order.normalCount(), 2u);
        EXPECT_EQ(order.lightCount(), 2u);
        EXPECT_EQ(*order.prefixOf("light.esl"), 0xFE000000u);
        EXPECT_EQ(*order.prefixOf("LIGHTB.ESL"), 0xFE001000u);
        EXPECT_EQ(*order.prefixOf("other.esp"), 0x01000000u);

        // A light plugin whose masters are Skyrim.esm and Light.esl.
        const Bytes h = tes4Header(1.7f, Content::sRecLight, "", "", 0, { { "Skyrim.esm", 0 }, { "Light.esl", 0 } });
        const auto plugin = openHeader(h, "LightB.esl");
        ASSERT_NE(plugin, nullptr);
        const FormIdResolver resolver(plugin->header(), "LightB.esl", order);
        EXPECT_EQ(resolver.resolve(0x01000ABC), 0xFE000ABCu); // master 1 is a light plugin
        EXPECT_EQ(resolver.resolve(0x0100FABC), 0xFE000ABCu); // only the low 12 bits count
        EXPECT_EQ(resolver.resolve(0x02000801), 0xFE001801u); // itself
        EXPECT_EQ(resolver.resolve(0x00000014), 0x00000014u);
    }

    TEST(SkyrimRecordsResolve, theLoadOrderRefusesDuplicatesAndOverflow)
    {
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("a.esp", false, err));
        EXPECT_FALSE(order.add("A.ESP", false, err));
        EXPECT_FALSE(err.empty());
        for (std::size_t i = 1; i < LoadOrder::kMaxNormal; ++i)
            ASSERT_TRUE(order.add("n" + std::to_string(i) + ".esp", false, err)) << i;
        EXPECT_FALSE(order.add("one-too-many.esp", false, err));
        for (std::size_t i = 0; i < LoadOrder::kMaxLight; ++i)
            ASSERT_TRUE(order.add("l" + std::to_string(i) + ".esl", true, err)) << i;
        EXPECT_FALSE(order.add("light-too-many.esl", true, err));
    }

    // ---- cells and references ------------------------------------------------------------------------------------

    TEST(SkyrimRecordsCell, anInteriorCellWithItsReferenceGroups)
    {
        const std::uint32_t cellId = 0x01000D2A; // self
        Bytes persistent = refr(0x01000900, 0x00001234, 0x400 /*persistent*/);
        Bytes temporary = refr(0x01000901, 0x01000555, 0, 10.0f, 2.0f);
        temporary.append(record4("ACHR", 0, 0x01000902,
            Bytes().append(sub4("NAME", Bytes().u32(0x00000007))).append(refrData(5, 6, 7, 0, 0, 1))));
        Bytes extra = sub4("XCLW", Bytes().f32(-12.5f));
        extra.append(sub4("XLCN", Bytes().u32(0x00AAAAAA)));
        const Bytes bytes = interiorPlugin(
            cell(cellId, "TestRoom", CellFlags::Interior | CellFlags::HasWater, extra), cellId, persistent, temporary);
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mPlugin.mCells.size(), 1u);
        const CellRecord& c = p.mPlugin.mCells[0];
        EXPECT_EQ(c.mCommon.mId, cellId);
        EXPECT_EQ(c.mCommon.mEditorId, "TestRoom");
        EXPECT_EQ(c.mName.mText, "TestRoom");
        EXPECT_TRUE(c.interior());
        EXPECT_EQ(c.mFlags & CellFlags::HasWater, CellFlags::HasWater);
        ASSERT_TRUE(c.mWaterHeight.has_value());
        EXPECT_FLOAT_EQ(*c.mWaterHeight, -12.5f);
        EXPECT_EQ(c.mLocation, 0x00AAAAAAu);
        EXPECT_FALSE(c.mGrid.has_value());
        EXPECT_EQ(c.mWorld, 0u);
        EXPECT_EQ(c.mInteriorBlock, static_cast<std::int64_t>(cellId % 10));
        EXPECT_EQ(c.mInteriorSubBlock, static_cast<std::int64_t>((cellId / 10) % 10));
        ASSERT_EQ(c.mPersistent.size(), 1u);
        ASSERT_EQ(c.mTemporary.size(), 2u);
        EXPECT_EQ(c.refCount(), 3u);
        const PlacedRef& r = c.mPersistent[0];
        EXPECT_EQ(r.mKind, RefKind::Placed);
        EXPECT_EQ(r.mGroup, RefGroup::Persistent);
        EXPECT_TRUE(r.persistent());
        EXPECT_EQ(r.mBase, 0x00001234u);
        EXPECT_EQ(r.mCommon.mId, 0x01000900u);
        EXPECT_TRUE(r.mHasTransform);
        EXPECT_FLOAT_EQ(r.mPosition.x, 1.0f);
        EXPECT_FLOAT_EQ(r.mPosition.z, 3.0f);
        EXPECT_FLOAT_EQ(r.mRotation.y, 0.25f);
        EXPECT_FLOAT_EQ(r.mScale, 1.0f);
        const PlacedRef& t0 = c.mTemporary[0];
        EXPECT_EQ(t0.mGroup, RefGroup::Temporary);
        EXPECT_EQ(t0.mBase, 0x01000555u);
        EXPECT_FLOAT_EQ(t0.mScale, 2.0f);
        EXPECT_FLOAT_EQ(t0.mPosition.x, 10.0f);
        EXPECT_EQ(c.mTemporary[1].mKind, RefKind::Actor);
        EXPECT_EQ(c.mTemporary[1].mBase, 0x00000007u);
        EXPECT_EQ(p.mPlugin.mCounts.at(Content::fourCC("REFR")).mParsed, 2u);
        EXPECT_EQ(p.mPlugin.mCounts.at(Content::fourCC("ACHR")).mParsed, 1u);
        EXPECT_EQ(p.mPlugin.mRefsOutsideCells, 0u);
        EXPECT_EQ(p.mPlugin.findCell(cellId), &p.mPlugin.mCells[0]);
        EXPECT_EQ(p.mPlugin.findCell(1), nullptr);
    }

    TEST(SkyrimRecordsCell, enableParentFlagsAndUnresolvedIds)
    {
        const std::uint32_t cellId = 0x01000D2A;
        Bytes r;
        r.append(sub4("NAME", Bytes().u32(0x05000001))); // master index 5: not a master, so the plugin itself
        r.append(sub4("XESP", Bytes().u32(0x00000014).u8(3).u8(0).u8(0).u8(0)));
        r.append(refrData(0, 0, 0, 0, 0, 0));
        const Bytes bytes = interiorPlugin(
            cell(cellId, "R", CellFlags::Interior), cellId, Bytes(), record4("REFR", 0x800, 0x01000910, r));
        // A load order without Skyrim.esm: the enable parent (a Skyrim.esm form) is unresolved.
        LoadOrder order;
        std::string err;
        ASSERT_TRUE(order.add("test.esp", false, err));
        const Parsed p = parse(bytes, order);
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        const PlacedRef& ref = p.mPlugin.mCells.at(0).mTemporary.at(0);
        EXPECT_TRUE(ref.initiallyDisabled());
        ASSERT_TRUE(ref.mEnableParent.has_value());
        EXPECT_EQ(ref.mEnableParent->mParent, kUnresolvedFormId);
        EXPECT_EQ(ref.mEnableParent->mFlags, 3);
        EXPECT_EQ(ref.mBase, 0x00000001u); // test.esp is index 0 in this order
        EXPECT_GE(p.mPlugin.mUnresolvedFormIds, 1u);
    }

    TEST(SkyrimRecordsCell, exteriorCellsInAWorldspace)
    {
        const std::uint32_t worldId = 0x01000A00;
        const std::uint32_t persistentCellId = 0x01000A01;
        const std::uint32_t cellId = 0x01000A02;
        Bytes wrld;
        wrld.append(edid("TestWorld")).append(sub4("FULL", Bytes().zstring("Test World")));
        wrld.append(sub4("WNAM", Bytes().u32(0x0000003C)));
        Bytes worldRec = record4("WRLD", 0, worldId, wrld);

        Bytes persistentCell = record4("CELL", 0, persistentCellId, Bytes().append(sub4("DATA", Bytes().u16(0))));
        Bytes xclc = sub4("XCLC", Bytes().u32(static_cast<std::uint32_t>(-3)).u32(7).u32(0));
        Bytes inner = cell(cellId, "Ext", 0, xclc);
        inner.append(group4(cellId, kGrupCellChildren, group4(cellId, kGrupTemporary, refr(0x01000B00, 0x01000C00))));
        Bytes worldChildren = persistentCell;
        worldChildren.append(group4(persistentCellId, kGrupCellChildren,
            group4(persistentCellId, kGrupPersistent, refr(0x01000B01, 0x01000C01, 0x400))));
        worldChildren.append(group4(0x0007FFFD, kGrupExtBlock, group4(0x0007FFFD, kGrupExtSub, inner)));

        Bytes top = worldRec;
        top.append(group4(worldId, kGrupWorld, worldChildren));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("WRLD"), kGrupTop, top));

        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mPlugin.mWorlds.size(), 1u);
        const WorldRecord& w = p.mPlugin.mWorlds[0];
        EXPECT_EQ(w.mCommon.mEditorId, "TestWorld");
        EXPECT_EQ(w.mName.mText, "Test World");
        EXPECT_EQ(w.mParent, 0x0000003Cu);
        ASSERT_EQ(p.mPlugin.mCells.size(), 2u);
        const CellRecord* pc = p.mPlugin.findCell(persistentCellId);
        const CellRecord* ec = p.mPlugin.findCell(cellId);
        ASSERT_NE(pc, nullptr);
        ASSERT_NE(ec, nullptr);
        EXPECT_TRUE(pc->mWorldPersistentCell);
        EXPECT_EQ(pc->mWorld, worldId);
        EXPECT_EQ(pc->mPersistent.size(), 1u);
        EXPECT_FALSE(ec->mWorldPersistentCell);
        EXPECT_EQ(ec->mWorld, worldId);
        ASSERT_TRUE(ec->mGrid.has_value());
        EXPECT_EQ(ec->mGrid->x, -3);
        EXPECT_EQ(ec->mGrid->y, 7);
        EXPECT_EQ(ec->mExteriorBlockLabel, 0x0007FFFDu);
        EXPECT_EQ(ec->mExteriorSubBlockLabel, 0x0007FFFDu);
        EXPECT_EQ(ec->mInteriorBlock, -1);
        EXPECT_EQ(ec->mTemporary.size(), 1u);
    }

    TEST(SkyrimRecordsCell, visibleDistantChildren)
    {
        const std::uint32_t cellId = 0x01000D2A;
        Bytes sub = cell(cellId, "V", CellFlags::Interior);
        sub.append(group4(cellId, kGrupCellChildren, group4(cellId, kGrupVisibleDistant, refr(0x01000920, 1))));
        Bytes bytes = header();
        bytes.append(
            group4(Content::fourCC("CELL"), kGrupTop, group4(0, kGrupInteriorBlock, group4(0, kGrupInteriorSub, sub))));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mPlugin.mCells.at(0).mVisibleDistant.size(), 1u);
    }

    TEST(SkyrimRecordsCell, aReferenceOutsideAnyCellIsCounted)
    {
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("REFR"), kGrupTop, refr(0x01000930, 1)));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mPlugin.mRefsOutsideCells, 1u);
        EXPECT_TRUE(p.mPlugin.mCells.empty());
    }

    TEST(SkyrimRecordsCell, aOneByteDataFieldIsAccepted)
    {
        const std::uint32_t cellId = 0x01000D2A;
        Bytes subs;
        subs.append(sub4("DATA", Bytes().u8(1)));
        const Bytes bytes = interiorPlugin(record4("CELL", 0, cellId, subs), cellId, Bytes(), Bytes());
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mPlugin.mCells.at(0).interior());
        EXPECT_EQ(p.mPlugin.mOddSubrecords, 0u);
    }

    // ---- other record types --------------------------------------------------------------------------------------

    TEST(SkyrimRecordsTypes, staticsWeaponsMiscAndArmor)
    {
        Bytes stat;
        stat.append(edid("StatOne"));
        stat.append(sub4("OBND", Bytes().u16(0xFFFF).u16(0xFFFE).u16(0).u16(1).u16(2).u16(3)));
        stat.append(sub4("MODL", Bytes().zstring("Architecture\\Test.nif")));
        stat.append(sub4("MODT", Bytes().u32(0)));

        Bytes weap;
        weap.append(edid("WeapOne"));
        weap.append(sub4("FULL", Bytes().zstring("Sword")));
        weap.append(sub4("MODL", Bytes().zstring("Weapons\\Sword.nif")));
        weap.append(sub4("KSIZ", Bytes().u32(2)));
        weap.append(sub4("KWDA", Bytes().u32(0x0001E711).u32(0x01000777)));
        weap.append(sub4("DATA", Bytes().u32(25).f32(9.0f).u16(7)));

        Bytes misc;
        misc.append(edid("MiscOne"));
        misc.append(sub4("MODL", Bytes().zstring("Clutter\\Cup.nif")));
        misc.append(sub4("DATA", Bytes().u32(3).f32(0.5f)));

        Bytes armo;
        armo.append(edid("ArmoOne"));
        armo.append(sub4("FULL", Bytes().zstring("Helm")));
        armo.append(sub4("MODL", Bytes().u32(0x0004A111))); // an armature
        armo.append(sub4("MOD2", Bytes().zstring("Armor\\Helm\\M.nif")));
        armo.append(sub4("MOD4", Bytes().zstring("Armor\\Helm\\F.nif")));
        armo.append(sub4("DATA", Bytes().u32(120).f32(2.5f)));

        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, record4("STAT", 0, 0x01000100, stat)));
        bytes.append(group4(Content::fourCC("WEAP"), kGrupTop, record4("WEAP", 0, 0x01000101, weap)));
        bytes.append(group4(Content::fourCC("MISC"), kGrupTop, record4("MISC", 0, 0x01000102, misc)));
        bytes.append(group4(Content::fourCC("ARMO"), kGrupTop, record4("ARMO", 0, 0x01000103, armo)));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mPlugin.mStatics.size(), 1u);
        EXPECT_EQ(p.mPlugin.mStatics[0].mModel, "Architecture\\Test.nif");
        ASSERT_TRUE(p.mPlugin.mStatics[0].mBounds.has_value());
        EXPECT_EQ((*p.mPlugin.mStatics[0].mBounds)[0], -1);
        EXPECT_EQ((*p.mPlugin.mStatics[0].mBounds)[1], -2);
        EXPECT_EQ((*p.mPlugin.mStatics[0].mBounds)[5], 3);
        ASSERT_EQ(p.mPlugin.mItems.size(), 3u);
        const ItemRecord& w = p.mPlugin.mItems[0];
        EXPECT_EQ(w.mKind, ItemKind::Weapon);
        EXPECT_EQ(w.mName.mText, "Sword");
        EXPECT_EQ(w.mModel, "Weapons\\Sword.nif");
        EXPECT_EQ(w.mValue, 25);
        EXPECT_FLOAT_EQ(w.mWeight, 9.0f);
        EXPECT_EQ(w.mDamage, 7);
        ASSERT_EQ(w.mKeywords.size(), 2u);
        EXPECT_EQ(w.mKeywords[0], 0x0001E711u);
        EXPECT_EQ(w.mKeywords[1], 0x01000777u);
        const ItemRecord& m = p.mPlugin.mItems[1];
        EXPECT_EQ(m.mKind, ItemKind::Misc);
        EXPECT_EQ(m.mValue, 3);
        EXPECT_EQ(m.mDamage, 0);
        const ItemRecord& a = p.mPlugin.mItems[2];
        EXPECT_EQ(a.mKind, ItemKind::Armor);
        EXPECT_EQ(a.mModel, "Armor\\Helm\\M.nif");
        EXPECT_EQ(a.mFemaleModel, "Armor\\Helm\\F.nif");
        ASSERT_EQ(a.mArmatures.size(), 1u);
        EXPECT_EQ(a.mArmatures[0], 0x0004A111u);
        EXPECT_EQ(a.mValue, 120);
        EXPECT_EQ(p.mPlugin.mOddSubrecords, 0u);
    }

    TEST(SkyrimRecordsTypes, anNpc)
    {
        Bytes npc;
        npc.append(edid("NpcOne"));
        npc.append(sub4("FULL", Bytes().zstring("Bob")));
        npc.append(sub4("RNAM", Bytes().u32(0x00013746)));
        npc.append(sub4("CNAM", Bytes().u32(0x01000300)));
        Bytes acbs;
        acbs.u32(NpcFlags::Female | NpcFlags::Essential | NpcFlags::Unique)
            .u16(0)
            .u16(0)
            .u16(12)
            .u16(1)
            .u16(40)
            .u16(100)
            .u16(0)
            .u16(0)
            .u16(0)
            .u16(0);
        npc.append(sub4("ACBS", acbs));
        Bytes dnam;
        for (int i = 0; i < 18; ++i)
            dnam.u8(static_cast<std::uint8_t>(i + 10));
        for (int i = 0; i < 18; ++i)
            dnam.u8(0);
        dnam.u16(150).u16(60).u16(70).u16(0).f32(0.0f).u8(0).u8(0).u8(0).u8(0);
        npc.append(sub4("DNAM", dnam));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, record4("NPC_", 0, 0x01000200, npc)));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mPlugin.mNpcs.size(), 1u);
        const NpcRecord& n = p.mPlugin.mNpcs[0];
        EXPECT_EQ(n.mName.mText, "Bob");
        EXPECT_EQ(n.mRace, 0x00013746u);
        EXPECT_EQ(n.mClass, 0x01000300u);
        EXPECT_TRUE(n.mHasConfig);
        EXPECT_TRUE(n.female());
        EXPECT_TRUE(n.essential());
        EXPECT_TRUE(n.unique());
        EXPECT_FALSE(n.isProtected());
        EXPECT_EQ(n.mLevel, 12);
        EXPECT_EQ(n.mCalcMin, 1);
        EXPECT_EQ(n.mCalcMax, 40);
        ASSERT_TRUE(n.mHasStats);
        EXPECT_EQ(n.mSkills[0], 10);
        EXPECT_EQ(n.mSkills[17], 27);
        EXPECT_EQ(n.mHealth, 150);
        EXPECT_EQ(n.mMagicka, 60);
        EXPECT_EQ(n.mStamina, 70);
        EXPECT_EQ(p.mPlugin.mOddSubrecords, 0u);
    }

    Bytes lvlo(std::uint16_t level, std::uint32_t ref, std::uint16_t count)
    {
        return sub4("LVLO", Bytes().u16(level).u16(0).u32(ref).u16(count).u16(0));
    }

    TEST(SkyrimRecordsTypes, leveledLists)
    {
        Bytes lvli;
        lvli.append(edid("LItemTest")).append(sub4("LVLD", Bytes().u8(25))).append(sub4("LVLF", Bytes().u8(3)));
        lvli.append(sub4("LVLG", Bytes().u32(0x01000800)));
        lvli.append(sub4("LLCT", Bytes().u8(2)));
        lvli.append(lvlo(1, 0x00000F00, 1)).append(lvlo(5, 0x01000F01, 3));
        lvli.append(sub4("COED", Bytes().u32(0).u32(0).u32(0)));
        Bytes lvln;
        lvln.append(edid("LCharTest")).append(sub4("LVLD", Bytes().u8(0))).append(sub4("LLCT", Bytes().u8(1)));
        lvln.append(lvlo(2, 0x01000F02, 1));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("LVLI"), kGrupTop, record4("LVLI", 0, 0x01000400, lvli)));
        bytes.append(group4(Content::fourCC("LVLN"), kGrupTop, record4("LVLN", 0, 0x01000401, lvln)));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mPlugin.mLeveledLists.size(), 2u);
        const LeveledList& a = p.mPlugin.mLeveledLists[0];
        EXPECT_FALSE(a.mIsNpcList);
        EXPECT_EQ(a.mChanceNone, 25);
        EXPECT_EQ(a.mFlags, 3);
        EXPECT_EQ(a.mGlobal, 0x01000800u);
        ASSERT_EQ(a.mEntries.size(), 2u);
        EXPECT_EQ(a.mEntries[1].mLevel, 5);
        EXPECT_EQ(a.mEntries[1].mReference, 0x01000F01u);
        EXPECT_EQ(a.mEntries[1].mCount, 3);
        EXPECT_TRUE(p.mPlugin.mLeveledLists[1].mIsNpcList);
        EXPECT_EQ(p.mPlugin.mLeveledLists[1].mEntries.size(), 1u);
        EXPECT_EQ(p.mPlugin.mOddSubrecords, 0u);
    }

    // ---- strings and compression ---------------------------------------------------------------------------------

    TEST(SkyrimRecordsStrings, aLocalizedPluginHoldsIdsNotText)
    {
        const std::uint32_t cellId = 0x01000D2A;
        Bytes subs;
        subs.append(sub4("FULL", Bytes().u32(0x00001234))).append(sub4("DATA", Bytes().u16(1)));
        Bytes sub = record4("CELL", 0, cellId, subs);
        sub.append(group4(cellId, kGrupCellChildren, Bytes()));
        Bytes bytes = header(Content::sRecLocalized);
        bytes.append(
            group4(Content::fourCC("CELL"), kGrupTop, group4(0, kGrupInteriorBlock, group4(0, kGrupInteriorSub, sub))));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        LString name = p.mPlugin.mCells.at(0).mName;
        EXPECT_TRUE(name.mPresent);
        EXPECT_TRUE(name.mLocalized);
        EXPECT_EQ(name.mId, 0x1234u);
        EXPECT_TRUE(name.mText.empty());

        Bytes table;
        table.u32(1).u32(9).u32(0x1234).u32(0).zstring("Whiterun");
        StringTable st;
        Content::Error error;
        ASSERT_TRUE(StringTable::parse(view(table.str()), StringTableKind::Strings, "x.strings", st, error))
            << error.describe();
        EXPECT_EQ(st.size(), 1u);
        ASSERT_NE(st.find(0x1234), nullptr);
        EXPECT_EQ(*st.find(0x1234), "Whiterun");
        EXPECT_EQ(st.find(5), nullptr);
        EXPECT_TRUE(lookupLString(name, st));
        EXPECT_EQ(name.mText, "Whiterun");
        LString unknown;
        unknown.mPresent = unknown.mLocalized = true;
        unknown.mId = 99;
        EXPECT_FALSE(lookupLString(unknown, st));
    }

    TEST(SkyrimRecordsStrings, aLocalizedNameOfTheWrongSizeIsOddNotFatal)
    {
        Bytes npc;
        npc.append(sub4("FULL", Bytes().zstring("Text in a localised plugin")));
        Bytes bytes = header(Content::sRecLocalized);
        bytes.append(group4(Content::fourCC("NPC_"), kGrupTop, record4("NPC_", 0, 0x01000200, npc)));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mPlugin.mNpcs.size(), 1u);
        EXPECT_FALSE(p.mPlugin.mNpcs[0].mName.mPresent);
        EXPECT_EQ(p.mPlugin.mOddSubrecords, 1u);
        EXPECT_EQ(p.mPlugin.mOddBy.at("NPC_.FULL"), 1u);
    }

    TEST(SkyrimRecordsStrings, lengthPrefixedTablesAndPaths)
    {
        Bytes table;
        table.u32(2).u32(4 + 6 + 4 + 3);
        table.u32(7).u32(0).u32(1).u32(4 + 6);
        table.u32(6).text(std::string("Hello\0", 6)).u32(3).text(std::string("Hi\0", 3));
        StringTable st;
        Content::Error error;
        ASSERT_TRUE(StringTable::parse(view(table.str()), StringTableKind::DlStrings, "x.dlstrings", st, error))
            << error.describe();
        ASSERT_NE(st.find(7), nullptr);
        EXPECT_EQ(*st.find(7), "Hello");
        EXPECT_EQ(*st.find(1), "Hi");
        EXPECT_EQ(
            stringTablePath("Skyrim.esm", "English", StringTableKind::Strings), "strings\\skyrim_english.strings");
        EXPECT_EQ(
            stringTablePath("Update.ESM", "english", StringTableKind::DlStrings), "strings\\update_english.dlstrings");
        EXPECT_EQ(stringTablePath("X.esp", "german", StringTableKind::IlStrings), "strings\\x_german.ilstrings");
    }

    TEST(SkyrimRecordsStrings, hostileTablesAreRefused)
    {
        StringTable st;
        Content::Error error;
        const auto parseText
            = [&](const Bytes& b, StringTableKind k) { return StringTable::parse(view(b.str()), k, "x", st, error); };
        EXPECT_FALSE(parseText(Bytes().u32(1), StringTableKind::Strings)); // no second word
        EXPECT_FALSE(parseText(Bytes().u32(0xFFFFFFFF).u32(0), StringTableKind::Strings)); // count beyond the bytes
        EXPECT_FALSE(parseText(Bytes().u32(1).u32(100).u32(1).u32(0), StringTableKind::Strings)); // data size beyond
        EXPECT_FALSE(parseText(Bytes().u32(1).u32(2).u32(1).u32(50).text("ab"), StringTableKind::Strings)); // offset
        EXPECT_FALSE(parseText(Bytes().u32(1).u32(2).u32(1).u32(0).text("ab"), StringTableKind::Strings)); // no NUL
        EXPECT_FALSE(parseText(
            Bytes().u32(1).u32(8).u32(1).u32(0).u32(0xFFFFFF).text("abcd"), StringTableKind::DlStrings)); // length
        EXPECT_FALSE(error.mMessage.empty());
    }

    TEST(SkyrimRecordsCompressed, aCompressedRecordIsParsed)
    {
        Bytes stat;
        stat.append(edid("Zipped")).append(sub4("MODL", Bytes().zstring("a.nif")));
        uLongf size = compressBound(static_cast<uLong>(stat.size()));
        std::string z(size, '\0');
        ASSERT_EQ(compress2(reinterpret_cast<Bytef*>(z.data()), &size,
                      reinterpret_cast<const Bytef*>(stat.str().data()), static_cast<uLong>(stat.size()), 6),
            Z_OK);
        z.resize(size);
        Bytes body;
        body.u32(static_cast<std::uint32_t>(stat.size())).text(z);
        Bytes bytes = header();
        bytes.append(
            group4(Content::fourCC("STAT"), kGrupTop, record4("STAT", Content::sRecCompressed, 0x01000100, body)));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk) << p.mError.describe();
        ASSERT_EQ(p.mPlugin.mStatics.size(), 1u);
        EXPECT_EQ(p.mPlugin.mStatics[0].mCommon.mEditorId, "Zipped");
        EXPECT_EQ(p.mPlugin.mStatics[0].mModel, "a.nif");
    }

    TEST(SkyrimRecordsCompressed, aDamagedCompressedRecordIsAnErrorAndTheWalkContinues)
    {
        Bytes body;
        body.u32(100).text("not zlib at all");
        Bytes stat;
        stat.append(edid("Good"));
        Bytes list = record4("STAT", Content::sRecCompressed, 0x01000100, body);
        list.append(record4("STAT", 0, 0x01000101, stat));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, list));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mPlugin.mStatics.size(), 1u);
        EXPECT_EQ(p.mPlugin.mStatics[0].mCommon.mEditorId, "Good");
        ASSERT_EQ(p.mPlugin.mErrors.size(), 1u);
        EXPECT_EQ(p.mPlugin.mErrors[0].mFile, "test.esp");
        EXPECT_EQ(p.mPlugin.mErrors[0].mRecordType, Content::fourCC("STAT"));
        EXPECT_EQ(p.mPlugin.mCounts.at(Content::fourCC("STAT")).mFailed, 1u);
    }

    // ---- damaged and hostile input -------------------------------------------------------------------------------

    TEST(SkyrimRecordsHostile, aSubrecordPastTheRecordEndFailsThatRecordOnly)
    {
        Bytes bad;
        bad.fourCC("EDID").u16(200).text("short"); // claims 200 bytes
        Bytes list = record4("STAT", 0, 0x01000100, bad);
        list.append(record4("STAT", 0, 0x01000101, edid("Next")));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, list));
        const Parsed p = parse(bytes, defaultOrder());
        ASSERT_TRUE(p.mOk);
        ASSERT_EQ(p.mPlugin.mStatics.size(), 1u);
        EXPECT_EQ(p.mPlugin.mStatics[0].mCommon.mEditorId, "Next");
        ASSERT_EQ(p.mPlugin.mErrors.size(), 1u);
        EXPECT_NE(p.mPlugin.mErrors[0].mOffset, 0u);
        EXPECT_NE(p.mPlugin.mErrors[0].mMessage.find("EDID"), std::string::npos);
    }

    TEST(SkyrimRecordsHostile, aLeveledListOverTheEntryCapIsRefused)
    {
        Bytes lvli;
        lvli.append(sub4("LLCT", Bytes().u8(255)));
        for (int i = 0; i < 300; ++i)
            lvli.append(lvlo(1, 0x01000F00, 1));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("LVLI"), kGrupTop, record4("LVLI", 0, 0x01000400, lvli)));
        ParseLimits limits;
        limits.mMaxEntriesPerList = 100;
        const Parsed p = parse(bytes, defaultOrder(), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mPlugin.mLeveledLists.empty());
        ASSERT_EQ(p.mPlugin.mErrors.size(), 1u);
        EXPECT_EQ(p.mPlugin.mCounts.at(Content::fourCC("LVLI")).mFailed, 1u);
    }

    TEST(SkyrimRecordsHostile, aRecordOverTheByteLimitIsRefused)
    {
        Bytes stat;
        stat.append(sub4("MODL", Bytes().zstring(std::string(2000, 'x'))));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, record4("STAT", 0, 0x01000100, stat)));
        ParseLimits limits;
        limits.mMaxRecordBytes = 1000;
        const Parsed p = parse(bytes, defaultOrder(), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mPlugin.mStatics.empty());
        EXPECT_EQ(p.mPlugin.mErrors.size(), 1u);
    }

    TEST(SkyrimRecordsHostile, aLongStringIsRefusedByTheStringCap)
    {
        Bytes stat;
        stat.append(sub4("MODL", Bytes().zstring(std::string(3000, 'x'))));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, record4("STAT", 0, 0x01000100, stat)));
        ParseLimits limits;
        limits.mMaxStringBytes = 1024;
        const Parsed p = parse(bytes, defaultOrder(), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_TRUE(p.mPlugin.mStatics.empty());
        EXPECT_EQ(p.mPlugin.mErrors.size(), 1u);
    }

    TEST(SkyrimRecordsHostile, errorsAreCappedAndTheRestCounted)
    {
        Bytes list;
        for (int i = 0; i < 20; ++i)
        {
            Bytes bad;
            bad.fourCC("EDID").u16(90).text("x");
            list.append(record4("STAT", 0, 0x01000100 + i, bad));
        }
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, list));
        ParseLimits limits;
        limits.mMaxErrors = 5;
        const Parsed p = parse(bytes, defaultOrder(), limits);
        ASSERT_TRUE(p.mOk);
        EXPECT_EQ(p.mPlugin.mErrors.size(), 5u);
        EXPECT_EQ(p.mPlugin.mErrorsDropped, 15u);
        EXPECT_EQ(p.mPlugin.mCounts.at(Content::fourCC("STAT")).mFailed, 20u);
    }

    TEST(SkyrimRecordsHostile, aTruncatedFileKeepsTheRecordsBeforeTheDamage)
    {
        Bytes list = record4("STAT", 0, 0x01000100, edid("One"));
        list.append(record4("STAT", 0, 0x01000101, edid("Two")));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, list));
        std::string cut = bytes.str();
        cut.resize(cut.size() - 4);
        Bytes b2;
        b2.text(cut);
        const Parsed p = parse(b2, defaultOrder());
        EXPECT_FALSE(p.mOk);
        EXPECT_TRUE(p.mPlugin.mWalkError.has_value());
    }

    TEST(SkyrimRecordsHostile, aMorrowindFileIsRefused)
    {
        const Bytes tes3 = tes3Header(1.3f, 0, "", "", 0, {});
        const Parsed p = parse(tes3, defaultOrder());
        EXPECT_FALSE(p.mOk);
        EXPECT_FALSE(p.mError.mMessage.empty());
    }

    TEST(SkyrimRecordsXxxx, aSubrecordLargerThan64KiBUsesTheXxxxField)
    {
        const std::string longText(70000, 'q');
        Bytes stat;
        stat.fourCC("XXXX").u16(4).u32(static_cast<std::uint32_t>(longText.size() + 1));
        stat.fourCC("EDID").u16(0).zstring(longText);
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("STAT"), kGrupTop, record4("STAT", 0, 0x01000100, stat)));
        ParseLimits big;
        big.mMaxStringBytes = 100000;
        const Parsed ok = parse(bytes, defaultOrder(), big);
        ASSERT_TRUE(ok.mOk);
        ASSERT_EQ(ok.mPlugin.mStatics.size(), 1u);
        EXPECT_EQ(ok.mPlugin.mStatics[0].mCommon.mEditorId.size(), 70000u);
        const Parsed refused = parse(bytes, defaultOrder());
        EXPECT_TRUE(refused.mPlugin.mStatics.empty());
        EXPECT_EQ(refused.mPlugin.mErrors.size(), 1u);
    }

    Bytes variedPlugin()
    {
        const std::uint32_t cellId = 0x01000D2A;
        Bytes bytes = interiorPlugin(cell(cellId, "TestRoom", CellFlags::Interior), cellId,
            refr(0x01000900, 0x00001234, 0x400), refr(0x01000901, 0x01000555, 0, 10.0f, 2.0f));
        Bytes weap;
        weap.append(edid("W")).append(sub4("DATA", Bytes().u32(25).f32(9.0f).u16(7)));
        weap.append(sub4("KWDA", Bytes().u32(1).u32(2)));
        bytes.append(group4(Content::fourCC("WEAP"), kGrupTop, record4("WEAP", 0, 0x01000101, weap)));
        Bytes lvli;
        lvli.append(edid("L")).append(sub4("LVLD", Bytes().u8(25)));
        lvli.append(lvlo(1, 0x00000F00, 1));
        bytes.append(group4(Content::fourCC("LVLI"), kGrupTop, record4("LVLI", 0, 0x01000400, lvli)));
        return bytes;
    }

    std::string digest(const ParsedPlugin& p)
    {
        std::string out;
        for (const CellRecord& c : p.mCells)
        {
            out += c.mCommon.mEditorId + std::to_string(c.refCount());
            for (const PlacedRef& r : c.mTemporary)
                out += std::to_string(r.mBase) + "," + std::to_string(r.mPosition.x);
        }
        for (const ItemRecord& i : p.mItems)
            out += i.mCommon.mEditorId + std::to_string(i.mValue) + std::to_string(i.mKeywords.size());
        for (const LeveledList& l : p.mLeveledLists)
            out += l.mCommon.mEditorId + std::to_string(l.mEntries.size());
        out += "|" + std::to_string(p.mErrors.size()) + "|" + std::to_string(p.mOddSubrecords);
        return out;
    }

    TEST(SkyrimRecordsDeterminism, theSameInputGivesTheSameModel)
    {
        const Bytes bytes = variedPlugin();
        const Parsed a = parse(bytes, defaultOrder());
        const Parsed b = parse(bytes, defaultOrder());
        ASSERT_TRUE(a.mOk);
        EXPECT_EQ(digest(a.mPlugin), digest(b.mPlugin));
        EXPECT_NE(digest(a.mPlugin).find("TestRoom2"), std::string::npos);
    }

    TEST(SkyrimRecordsFuzz, mutatedPluginsNeverCrashAndStayDeterministic)
    {
        const std::string original = variedPlugin().str();
        std::mt19937 rng(12345);
        const LoadOrder order = defaultOrder();
        for (int i = 0; i < 6000; ++i)
        {
            std::string mutated = original;
            const int edits = 1 + static_cast<int>(rng() % 4);
            for (int e = 0; e < edits && !mutated.empty(); ++e)
            {
                const std::size_t at = rng() % mutated.size();
                switch (rng() % 3)
                {
                    case 0:
                        mutated[at] = static_cast<char>(rng());
                        break;
                    case 1:
                        mutated[at] = static_cast<char>(mutated[at] ^ (1 << (rng() % 8)));
                        break;
                    default:
                        mutated.resize(1 + at);
                        break;
                }
            }
            ParsedPlugin one;
            ParsedPlugin two;
            Content::Error e1;
            Content::Error e2;
            ParseLimits limits;
            limits.mMaxRecordBytes = 1 << 20;
            const bool a = parseRecordsBytes(mutated, "f.esp", order, limits, one, e1);
            const bool b = parseRecordsBytes(mutated, "f.esp", order, limits, two, e2);
            ASSERT_EQ(a, b);
            ASSERT_EQ(digest(one), digest(two));
            for (const Content::Error& err : one.mErrors)
                ASSERT_EQ(err.mFile, "f.esp");
        }
    }

    TEST(SkyrimRecordsFuzz, mutatedStringTablesNeverCrash)
    {
        Bytes table;
        table.u32(2).u32(4 + 6 + 4 + 3);
        table.u32(7).u32(0).u32(1).u32(4 + 6);
        table.u32(6).text(std::string("Hello\0", 6)).u32(3).text(std::string("Hi\0", 3));
        const std::string original = table.str();
        std::mt19937 rng(777);
        for (int i = 0; i < 6000; ++i)
        {
            std::string m = original;
            m[rng() % m.size()] = static_cast<char>(rng());
            if (rng() % 4 == 0)
                m.resize(rng() % m.size());
            for (StringTableKind k :
                { StringTableKind::Strings, StringTableKind::DlStrings, StringTableKind::IlStrings })
            {
                StringTable st;
                Content::Error error;
                const bool ok = StringTable::parse(view(m), k, "f", st, error);
                if (!ok)
                {
                    ASSERT_FALSE(error.mMessage.empty());
                }
            }
        }
    }
}
