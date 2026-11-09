#include <components/modorganizer/masters.hpp>

#include "tempfolder.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>

using namespace ModOrganizer;
using ModOrganizerTest::TempFolder;

namespace ModOrganizer
{
    std::ostream& operator<<(std::ostream& out, const Hint& h)
    {
        return out << int(h.mKind) << ":" << h.mPlugin << ":" << h.mMaster;
    }
}

namespace
{
    PluginInfo plugin(std::string name, std::vector<std::string> masters = {}, bool active = true)
    {
        return { std::move(name), active, std::move(masters) };
    }

    // A TES3 plugin: "TES3" record with HEDR and the master list (MAST + DATA).
    std::string tes3(const std::vector<std::string>& masters)
    {
        const auto u32 = [](std::uint32_t v) {
            std::string s(4, '\0');
            for (int i = 0; i < 4; ++i)
                s[i] = static_cast<char>((v >> (8 * i)) & 0xff);
            return s;
        };
        std::string body;
        std::string hedr = std::string(4, '\0') + u32(0) + std::string(32, 'a') + std::string(256, 'b') + u32(0);
        body += "HEDR" + u32(static_cast<std::uint32_t>(hedr.size())) + hedr;
        // version float 1.3
        const float version = 1.3f;
        std::memcpy(&hedr[0], &version, 4);
        body.clear();
        body += "HEDR" + u32(static_cast<std::uint32_t>(hedr.size())) + hedr;
        for (const std::string& m : masters)
        {
            const std::string name = m + '\0';
            body += "MAST" + u32(static_cast<std::uint32_t>(name.size())) + name;
            body += "DATA" + u32(8) + std::string(8, '\0');
        }
        return "TES3" + u32(static_cast<std::uint32_t>(body.size())) + u32(0) + u32(0) + body;
    }
}

TEST(ModOrganizerMasters, a_correct_order_has_no_hints)
{
    const std::vector<PluginInfo> list = { plugin("a.esm"), plugin("b.esp", { "a.esm" }) };
    EXPECT_TRUE(masterHints(list, {}).empty());
}

TEST(ModOrganizerMasters, missing_inactive_and_misordered_masters)
{
    const std::vector<PluginInfo> list = { plugin("late.esp", { "Z.ESM", "gone.esm", "off.esm", "base.esm" }),
        plugin("z.esm"), plugin("off.esm", {}, false) };
    const std::vector<Hint> hints = masterHints(list, { "Base.esm" });
    const std::vector<Hint> expected
        = { { HintKind::MasterAfter, "late.esp", "Z.ESM" }, { HintKind::MissingMaster, "late.esp", "gone.esm" },
              { HintKind::MasterAfter, "late.esp", "off.esm" }, { HintKind::InactiveMaster, "late.esp", "off.esm" } };
    EXPECT_EQ(hints, expected);
}

TEST(ModOrganizerMasters, inactive_plugins_are_not_checked)
{
    EXPECT_TRUE(masterHints({ plugin("x.esp", { "missing.esm" }, false) }, {}).empty());
}

TEST(ModOrganizerMasters, sorting_puts_masters_first_and_keeps_the_rest_in_place)
{
    const std::vector<PluginInfo> list
        = { plugin("c.esp", { "a.esm" }), plugin("x.esp"), plugin("a.esm"), plugin("d.esp", { "c.esp" }) };
    const auto order = suggestSortedOrder(list);
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(*order, (std::vector<std::string>{ "x.esp", "a.esm", "c.esp", "d.esp" }));
}

TEST(ModOrganizerMasters, an_already_sorted_list_is_returned_unchanged)
{
    const std::vector<PluginInfo> list = { plugin("a.esm"), plugin("b.esp", { "a.esm" }), plugin("c.esp") };
    EXPECT_EQ(*suggestSortedOrder(list), (std::vector<std::string>{ "a.esm", "b.esp", "c.esp" }));
}

TEST(ModOrganizerMasters, a_cycle_gives_no_order)
{
    EXPECT_FALSE(suggestSortedOrder({ plugin("a.esp", { "b.esp" }), plugin("b.esp", { "a.esp" }) }).has_value());
}

TEST(ModOrganizerMasters, masters_not_in_the_list_do_not_block_sorting)
{
    const auto order = suggestSortedOrder({ plugin("a.esp", { "elsewhere.esm" }) });
    ASSERT_TRUE(order.has_value());
    EXPECT_EQ(order->size(), 1u);
}

TEST(ModOrganizerMasters, reads_masters_from_a_generated_header)
{
    TempFolder t;
    const auto file = t.write("m.esp", tes3({ "Morrowind.esm", "Tribunal.esm" }));
    std::string error;
    const std::vector<std::string> masters = readMasters(file, error);
    EXPECT_EQ(masters, (std::vector<std::string>{ "Morrowind.esm", "Tribunal.esm" })) << error;
}

TEST(ModOrganizerMasters, an_unreadable_file_gives_an_error_not_a_crash)
{
    TempFolder t;
    std::string error;
    EXPECT_TRUE(readMasters(t.write("junk.esp", "not a plugin at all"), error).empty());
    EXPECT_FALSE(error.empty());
    error.clear();
    EXPECT_TRUE(readMasters(t.path() / "missing.esp", error).empty());
    EXPECT_FALSE(error.empty());
}
