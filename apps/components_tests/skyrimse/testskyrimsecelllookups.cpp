// Task 0102 stage N2: game_skyrimse::SkyrimCellSource answers the lookups of world_source.hpp 1.0 through the WorldView
// it wraps, and states Skyrim's units per metre. A scripted view, no game data.
#include <game_skyrimse/neutral_world.hpp>

#include <gtest/gtest.h>

namespace
{
    namespace sdkgame = openeden::sdk::game;

    class ScriptedView final : public sdkgame::WorldView
    {
    public:
        std::string_view gameId() const override { return "skyrimse"; }
        std::size_t cellCount() const override { return 2; }
        std::size_t worldCount() const override { return 1; }
        std::size_t listCells(std::size_t, std::size_t, std::vector<sdkgame::WorldCellInfo>&) const override
        {
            return 0;
        }
        std::size_t listWorlds(std::vector<sdkgame::WorldInfo>&) const override { return 0; }
        std::optional<sdkgame::WorldCellInfo> findCellByEditorId(std::string_view editorId) const override
        {
            if (editorId == "WhiterunInn")
            {
                sdkgame::WorldCellInfo info;
                info.id = 0x1234;
                return info;
            }
            return std::nullopt;
        }
        std::optional<sdkgame::WorldCellInfo> findCellByGrid(
            sdkgame::WorldId world, std::int32_t x, std::int32_t y) const override
        {
            if (world == 0x3C && x == -2 && y == 3)
            {
                sdkgame::WorldCellInfo info;
                info.id = 0x5678;
                return info;
            }
            return std::nullopt;
        }
        std::optional<sdkgame::WorldInfo> findWorldByEditorId(std::string_view editorId) const override
        {
            if (editorId == "Tamriel")
            {
                sdkgame::WorldInfo info;
                info.id = 0x3C;
                return info;
            }
            return std::nullopt;
        }
        bool loadCell(sdkgame::WorldId, sdkgame::WorldCellContent&, std::string& error) override
        {
            error = "none";
            return false;
        }
    };

    TEST(SkyrimCellSourceLookups, theLookupsGoThroughTheView)
    {
        ScriptedView view;
        game_skyrimse::SkyrimCellSource source(view);
        EXPECT_EQ(source.findCellByEditorId("WhiterunInn"), 0x1234u);
        EXPECT_EQ(source.findCellByEditorId("Nowhere"), sdkgame::kNoWorldId);
        EXPECT_EQ(source.findWorldByEditorId("Tamriel"), 0x3Cu);
        EXPECT_EQ(source.findWorldByEditorId("Elsewhere"), sdkgame::kNoWorldId);
        EXPECT_EQ(source.findCellByGrid(0x3C, -2, 3), 0x5678u);
        EXPECT_EQ(source.findCellByGrid(0x3C, 0, 0), sdkgame::kNoWorldId);
    }

    TEST(SkyrimCellSourceLookups, theUnitIsTheGameContractsValue)
    {
        ScriptedView view;
        game_skyrimse::SkyrimCellSource source(view);
        // compat_skyrimse's SkyrimGameContract::getUnitsPerMeter() is 69.99125109 (Morrowind's value; not measured for
        // Skyrim: RECALLED-VERIFY). Written out here, not shared, so a change of either is noticed.
        EXPECT_NEAR(source.unitsPerMetre(), 69.99125109, 1e-9);
    }
}
