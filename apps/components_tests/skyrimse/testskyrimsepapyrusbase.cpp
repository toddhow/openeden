// Task 0181 (Papyrus slice 2, part 1): the VMAD of base records (ACTI, NPC_, MISC ...) and their keywords, and the rule
// that combines the scripts of a placed reference with those of its base. Generated plugins; no game data. Written first
// and run red against stubs (no `mScripts` on the base records: the file did not compile; the combine function a stub that
// returned only the reference's scripts).
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0;

    Bytes header()
    {
        return tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
    }

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    // One script "Scr" with an object property naming `form` and an int property.
    Bytes vmadBytes(std::uint32_t form, std::int32_t number)
    {
        Bytes b;
        b.u16(5).u16(2).u16(1);
        const std::string name = "Scr";
        b.u16(static_cast<std::uint16_t>(name.size())).text(name).u8(0).u16(2);
        b.u16(1).text("o").u8(1).u8(1).u16(0).u16(0xFFFF).u32(form);
        b.u16(1).text("n").u8(3).u8(1).u32(static_cast<std::uint32_t>(number));
        return b;
    }

    Bytes topGroup(const char (&type)[5], const Bytes& records)
    {
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC(type), kGrupTop, records));
        return bytes;
    }

    Bytes actiWith(const Bytes& vmad, const std::vector<std::uint32_t>& keywords = {})
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Lever")));
        if (vmad.size() > 0)
            subs.append(sub4("VMAD", vmad));
        subs.append(sub4("MODL", Bytes().zstring("meshes\\lever.nif")));
        if (!keywords.empty())
        {
            subs.append(sub4("KSIZ", Bytes().u32(static_cast<std::uint32_t>(keywords.size()))));
            Bytes list;
            for (const std::uint32_t k : keywords)
                list.u32(k);
            subs.append(sub4("KWDA", list));
        }
        return record4("ACTI", 0, 0x01000A00, subs);
    }

    TEST(SkyrimPapyrusBase, anActivatorKeepsItsScriptsWithResolvedForms)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(
            topGroup("ACTI", actiWith(vmadBytes(0x01000555, 7))).str(), "test.esp", order(), ParseLimits(), plugin, error))
            << error.describe();
        ASSERT_EQ(plugin.mModels.size(), 1u);
        const ModelRecord& acti = plugin.mModels[0];
        ASSERT_EQ(acti.mScripts.size(), 1u);
        EXPECT_EQ(acti.mScripts[0].mName, "Scr");
        ASSERT_EQ(acti.mScripts[0].mProperties.size(), 2u);
        EXPECT_EQ(acti.mScripts[0].mProperties[0].mValue.mForm, 0x01000555u) << "resolved to a load order id";
        EXPECT_EQ(acti.mScripts[0].mProperties[1].mValue.mInteger, 7);
        EXPECT_EQ(plugin.mOddSubrecords, 0u);
    }

    TEST(SkyrimPapyrusBase, anActivatorWithoutAVmadHasNoScripts)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(
            parseRecordsBytes(topGroup("ACTI", actiWith(Bytes())).str(), "test.esp", order(), ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_TRUE(plugin.mModels[0].mScripts.empty());
    }

    TEST(SkyrimPapyrusBase, aDamagedBaseVmadIsCountedAndTheRecordStillLoads)
    {
        Bytes damaged = vmadBytes(0x01000555, 7);
        damaged.str().resize(damaged.size() - 3);
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(
            topGroup("ACTI", actiWith(damaged)).str(), "test.esp", order(), ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        EXPECT_TRUE(plugin.mModels[0].mScripts.empty());
        EXPECT_EQ(plugin.mOddSubrecords, 1u);
    }

    TEST(SkyrimPapyrusBase, anActivatorKeepsItsKeywordsResolved)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(topGroup("ACTI", actiWith(Bytes(), { 0x01000100, 0x00013746 })).str(), "test.esp",
            order(), ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mModels.size(), 1u);
        ASSERT_EQ(plugin.mModels[0].mKeywords.size(), 2u);
        EXPECT_EQ(plugin.mModels[0].mKeywords[0], 0x01000100u);
        EXPECT_EQ(plugin.mModels[0].mKeywords[1], 0x00013746u);
    }

    TEST(SkyrimPapyrusBase, aMiscItemKeepsItsScripts)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Coin")));
        subs.append(sub4("VMAD", vmadBytes(0, 3)));
        subs.append(sub4("MODL", Bytes().zstring("meshes\\coin.nif")));
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(topGroup("MISC", record4("MISC", 0, 0x01000A01, subs)).str(), "test.esp", order(),
            ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mItems.size(), 1u);
        ASSERT_EQ(plugin.mItems[0].mScripts.size(), 1u);
        EXPECT_EQ(plugin.mItems[0].mScripts[0].mName, "Scr");
    }

    TEST(SkyrimPapyrusBase, anNpcKeepsItsScripts)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Guard")));
        subs.append(sub4("VMAD", vmadBytes(0x01000555, 1)));
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(parseRecordsBytes(topGroup("NPC_", record4("NPC_", 0, 0x01000A02, subs)).str(), "test.esp", order(),
            ParseLimits(), plugin, error));
        ASSERT_EQ(plugin.mNpcs.size(), 1u);
        ASSERT_EQ(plugin.mNpcs[0].mScripts.size(), 1u);
        EXPECT_EQ(plugin.mNpcs[0].mScripts[0].mProperties[0].mValue.mForm, 0x01000555u);
    }

    // ---- combining the scripts of a reference with its base's ----------------------------------------------------

    vmad::Script named(const std::string& name, std::int32_t marker)
    {
        vmad::Script s;
        s.mName = name;
        vmad::Property p;
        p.mName = "marker";
        p.mType = 3;
        p.mValue.mKind = vmad::ValueKind::Int;
        p.mValue.mInteger = marker;
        s.mProperties.push_back(p);
        return s;
    }

    TEST(SkyrimPapyrusBase, theReferencesOwnScriptsComeFirstThenTheBasesInOrder)
    {
        const std::vector<vmad::Script> own = { named("A", 1), named("B", 2) };
        const std::vector<vmad::Script> base = { named("C", 3), named("D", 4) };
        const std::vector<vmad::Script> all = vmad::combineScripts(own, &base);
        ASSERT_EQ(all.size(), 4u);
        EXPECT_EQ(all[0].mName, "A");
        EXPECT_EQ(all[1].mName, "B");
        EXPECT_EQ(all[2].mName, "C");
        EXPECT_EQ(all[3].mName, "D");
    }

    TEST(SkyrimPapyrusBase, aBaseScriptTheReferenceAlreadyHasIsLeftOutWithoutCase)
    {
        const std::vector<vmad::Script> own = { named("Door", 1) };
        const std::vector<vmad::Script> base = { named("DOOR", 9), named("Other", 2) };
        const std::vector<vmad::Script> all = vmad::combineScripts(own, &base);
        ASSERT_EQ(all.size(), 2u);
        EXPECT_EQ(all[0].mProperties[0].mValue.mInteger, 1) << "the reference's values win";
        EXPECT_EQ(all[1].mName, "Other");
    }

    TEST(SkyrimPapyrusBase, noBaseGivesTheReferencesScriptsAsTheyAre)
    {
        const std::vector<vmad::Script> own = { named("A", 1) };
        EXPECT_EQ(vmad::combineScripts(own, nullptr).size(), 1u);
        const std::vector<vmad::Script> none;
        EXPECT_TRUE(vmad::combineScripts(none, nullptr).empty());
        const std::vector<vmad::Script> base = { named("C", 3) };
        EXPECT_EQ(vmad::combineScripts(none, &base).size(), 1u) << "a reference with no VMAD of its own gets its base's";
    }
}
