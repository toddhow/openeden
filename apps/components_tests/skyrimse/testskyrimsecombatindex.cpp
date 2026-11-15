// Task 0171, review fix 1: the combat records are read on a worker, never on the main thread. SkyrimWorldView::
// combatReady() starts the read and answers false until it is done; the data answers "nothing" before that and does
// not read anything itself. Generated plugins only.
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <functional>
#include <thread>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_skyrim_combatindex_"
                    + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_"
                    + std::to_string(counter++));
            fs::create_directories(mPath);
        }
        ~TempDir()
        {
            std::error_code ec;
            fs::remove_all(mPath, ec);
        }
        fs::path write(const std::string& name, const Bytes& bytes) const
        {
            const fs::path p = mPath / name;
            std::ofstream out(p, std::ios::binary);
            const std::string s = bytes.str();
            out.write(s.data(), static_cast<std::streamsize>(s.size()));
            return p;
        }
    };

    Bytes plugin()
    {
        Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "a", "b", 1, {}, 24, 44);
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring("HallA")));
        subs.append(sub4("FULL", Bytes().zstring("Hall")));
        subs.append(sub4("DATA", Bytes().u16(0x0001)));
        Bytes cell = record4("CELL", 0, 0x900, subs);
        cell.append(group4(0x900, 6, group4(0x900, 9, Bytes())));
        bytes.append(group4(Content::fourCC("CELL"), 0, group4(0, 2, group4(0, 3, cell))));
        return bytes;
    }

    bool eventually(const std::function<bool()>& condition, std::chrono::milliseconds limit = 5000ms)
    {
        const auto end = std::chrono::steady_clock::now() + limit;
        while (std::chrono::steady_clock::now() < end)
        {
            if (condition())
                return true;
            std::this_thread::sleep_for(2ms);
        }
        return condition();
    }

    TEST(SkyrimSeCombatIndex, theFirstAskStartsTheReadAndDoesNotWaitForIt)
    {
        TempDir dir;
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, nullptr));
        ASSERT_TRUE(view.startLoad({ dir.write("a.esm", plugin()) }, nullptr));
        ASSERT_TRUE(eventually([&] { return view.isReady(); }));
        // Never ready at the very first ask: the read has only just been started (before the fix: true, after the main
        // thread had read every plugin inside settings()).
        EXPECT_FALSE(view.combatReady());
        EXPECT_TRUE(eventually([&] { return view.combatReady(); }));
        EXPECT_TRUE(view.combatReady()); // and stays ready
    }

    TEST(SkyrimSeCombatIndex, theAnswersWaitForTheWorkerAndReadNothingThemselves)
    {
        TempDir dir;
        SkyrimWorldView view;
        ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, nullptr));
        ASSERT_TRUE(view.startLoad({ dir.write("a.esm", plugin()) }, nullptr));
        ASSERT_TRUE(eventually([&] { return view.isReady(); }));
        FighterProfile profile;
        EXPECT_FALSE(view.fighter(1, profile)); // nothing is known yet and nothing is read to find out
        EXPECT_EQ(view.weaponByEditorId("anything"), nullptr);
        EXPECT_FALSE(view.combatReady()); // none of the asks above started or finished the read
        ASSERT_TRUE(eventually([&] { return view.combatReady(); }));
        EXPECT_FALSE(view.fighter(0x777, profile)); // an unknown NPC, now really looked up
        EXPECT_EQ(view.weaponByEditorId("anything"), nullptr);
    }

    TEST(SkyrimSeCombatIndex, aViewDestroyedWhileTheWorkerReadsIsClean)
    {
        TempDir dir;
        for (int i = 0; i < 20; ++i)
        {
            SkyrimWorldView view;
            ASSERT_TRUE(view.configure(SkyrimWorldView::Options{}, nullptr));
            ASSERT_TRUE(view.startLoad({ dir.write("a.esm", plugin()) }, nullptr));
            ASSERT_TRUE(eventually([&] { return view.isReady(); }));
            (void)view.combatReady();
        }
    }
}
