// Task 0176: the VMAD of a placed reference (PlacedRef::mScripts) and of a quest (parseQuestVmads, QuestVmadIndex), on
// generated plugins. No game data.
#include <game_skyrimse/quest_script.hpp>
#include <game_skyrimse/records.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;
    constexpr std::uint32_t kRoomId = 0x01000D2A;

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

    // A VMAD with one script "Scr" holding one object property that names `form` (format 2) and one int property.
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

    Bytes roomWith(const Bytes& reference)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("Room")));
        subs.append(sub4("DATA", Bytes().u16(CellFlags::Interior)));
        Bytes sub = record4("CELL", 0, kRoomId, subs);
        sub.append(group4(kRoomId, kGrupCellChildren, group4(kRoomId, kGrupTemporary, reference)));
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("CELL"), kGrupTop,
            group4(kRoomId % 10, kGrupInteriorBlock, group4((kRoomId / 10) % 10, kGrupInteriorSub, sub))));
        return bytes;
    }

    Bytes refrWith(const Bytes& vmad)
    {
        Bytes subs;
        subs.append(sub4("NAME", Bytes().u32(0x00001234)));
        subs.append(sub4("DATA", Bytes().f32(1).f32(2).f32(3).f32(0).f32(0).f32(0)));
        if (vmad.size() > 0)
            subs.append(sub4("VMAD", vmad));
        return record4("REFR", 0, 0x01000901, subs);
    }

    const PlacedRef* find(const ParsedPlugin& p)
    {
        for (const CellRecord& c : p.mCells)
            for (const PlacedRef& r : c.mTemporary)
                if (r.mCommon.mId == 0x01000901)
                    return &r;
        return nullptr;
    }

    TEST(SkyrimVmadRecords, a_placed_reference_keeps_its_scripts_with_resolved_forms)
    {
        ParsedPlugin plugin;
        Content::Error error;
        // 0x00013746 is Skyrim.esm's (index 0); 0x01000555 is test.esp's own
        ASSERT_TRUE(parseRecordsBytes(
            roomWith(refrWith(vmadBytes(0x01000555, 7))).str(), "test.esp", order(), ParseLimits(), plugin, error))
            << error.describe();
        const PlacedRef* r = find(plugin);
        ASSERT_NE(r, nullptr);
        ASSERT_EQ(r->mScripts.size(), 1u);
        EXPECT_EQ(r->mScripts[0].mName, "Scr");
        ASSERT_EQ(r->mScripts[0].mProperties.size(), 2u);
        EXPECT_EQ(r->mScripts[0].mProperties[0].mValue.mForm, 0x01000555u);
        EXPECT_EQ(r->mScripts[0].mProperties[1].mValue.mInteger, 7);
        EXPECT_EQ(plugin.mOddSubrecords, 0u);
    }

    TEST(SkyrimVmadRecords, a_reference_without_a_vmad_has_no_scripts)
    {
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(
            parseRecordsBytes(roomWith(refrWith(Bytes())).str(), "test.esp", order(), ParseLimits(), plugin, error));
        const PlacedRef* r = find(plugin);
        ASSERT_NE(r, nullptr);
        EXPECT_TRUE(r->mScripts.empty());
    }

    TEST(SkyrimVmadRecords, a_damaged_vmad_is_counted_and_the_reference_still_loads)
    {
        Bytes damaged = vmadBytes(0x01000555, 7);
        damaged.str().resize(damaged.size() - 3);
        ParsedPlugin plugin;
        Content::Error error;
        ASSERT_TRUE(
            parseRecordsBytes(roomWith(refrWith(damaged)).str(), "test.esp", order(), ParseLimits(), plugin, error));
        const PlacedRef* r = find(plugin);
        ASSERT_NE(r, nullptr);
        EXPECT_TRUE(r->mScripts.empty());
        EXPECT_EQ(plugin.mOddSubrecords, 1u);
    }

    // ---- quests ---------------------------------------------------------------------------------------------------

    Bytes questWith(std::uint32_t id, const Bytes& vmad, std::uint32_t flags = 0)
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("Q")));
        if (vmad.size() > 0)
            b.append(sub4("VMAD", vmad));
        b.append(sub4("DNAM", Bytes().u16(1).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("ANAM", Bytes().u32(1)));
        return record4("QUST", flags, id, b);
    }

    QuestVmadRecords parseQuests(const Bytes& records)
    {
        Bytes bytes = header();
        bytes.append(group4(Content::fourCC("QUST"), kGrupTop, records));
        Content::Error error;
        const std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
            std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
        QuestVmadRecords out;
        EXPECT_NE(plugin, nullptr);
        if (plugin != nullptr)
        {
            EXPECT_TRUE(parseQuestVmads(*plugin, order(), out));
        }
        return out;
    }

    TEST(SkyrimVmadRecords, a_quest_vmad_is_parsed_resolved_and_indexed)
    {
        Bytes records = questWith(0x01000800, vmadBytes(0x01000555, 3));
        records.append(questWith(0x01000801, Bytes()));
        QuestVmadRecords parsed = parseQuests(records);
        ASSERT_EQ(parsed.mQuests.size(), 2u);
        EXPECT_TRUE(parsed.mQuests[0].mHasVmad);
        EXPECT_FALSE(parsed.mQuests[1].mHasVmad);
        QuestVmadIndex index;
        index.add(std::move(parsed));
        const vmad::Vmad* v = index.find(0x01000800);
        ASSERT_NE(v, nullptr);
        ASSERT_EQ(v->mScripts.size(), 1u);
        EXPECT_EQ(v->mScripts[0].mProperties[0].mValue.mForm, 0x01000555u);
        EXPECT_EQ(index.find(0x01000801), nullptr);
        EXPECT_EQ(index.count(), 1u);
    }

    TEST(SkyrimVmadRecords, a_later_record_without_a_vmad_removes_the_earlier_scripts)
    {
        QuestVmadIndex index;
        index.add(parseQuests(questWith(0x01000800, vmadBytes(0x01000555, 3))));
        ASSERT_NE(index.find(0x01000800), nullptr);
        index.add(parseQuests(questWith(0x01000800, Bytes())));
        EXPECT_EQ(index.find(0x01000800), nullptr);
    }

    TEST(SkyrimVmadRecords, a_deleted_quest_is_removed)
    {
        QuestVmadIndex index;
        index.add(parseQuests(questWith(0x01000800, vmadBytes(0x01000555, 3))));
        index.add(parseQuests(questWith(0x01000800, vmadBytes(0x01000555, 3), RecordFlags::Deleted)));
        EXPECT_EQ(index.find(0x01000800), nullptr);
    }

    TEST(SkyrimVmadRecords, a_damaged_quest_vmad_is_counted)
    {
        Bytes damaged = vmadBytes(0x01000555, 3);
        damaged.str().resize(damaged.size() - 2);
        const QuestVmadRecords parsed = parseQuests(questWith(0x01000800, damaged));
        EXPECT_EQ(parsed.mDamaged, 1u);
    }
}
