// Task 0361e follow-up (toddhow, 2026-11-20): the player's Skyrim body needs the actor records, which a worker reads.
// The worker used to start only when something first polled the player body, after the whole new-game cell load, so
// the first NPC of the cell read every plugin AGAIN on the main thread inside that load (about 2 s with the five
// masters). With the player body on, the first cell build now starts the worker, and the first actor description adopts
// its result instead of reading. Generated plugins only; no game data. Written first and run red against the old code.
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    namespace fs = std::filesystem;
    using namespace std::chrono_literals;

    constexpr std::uint32_t kGrupTop = 0, kGrupInteriorBlock = 2, kGrupInteriorSub = 3, kGrupCellChildren = 6,
                            kGrupTemporary = 9;

    struct TempDir
    {
        fs::path mPath;
        TempDir()
        {
            static int counter = 0;
            mPath = fs::temp_directory_path()
                / ("oe_skyrim_earlyactors_"
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

    Bytes interiorCell(std::uint32_t id, const std::string& editorId)
    {
        Bytes subs;
        subs.append(sub4("EDID", Bytes().zstring(editorId)));
        subs.append(sub4("FULL", Bytes().zstring("Plain " + editorId)));
        subs.append(sub4("DATA", Bytes().u16(0x0001)));
        Bytes sub = record4("CELL", 0, id, subs);
        sub.append(group4(id, kGrupCellChildren, group4(id, kGrupTemporary, Bytes())));
        return group4(Content::fourCC("CELL"), kGrupTop,
            group4(id % 10, kGrupInteriorBlock, group4((id / 10) % 10, kGrupInteriorSub, sub)));
    }

    struct Fixture
    {
        TempDir dir;
        std::vector<fs::path> paths;
        std::mutex noticeMutex;
        std::vector<std::string> notices;
        SkyrimWorldView view;

        explicit Fixture(bool playerBody)
        {
            Bytes bytes = tes4Header(1.7f, Content::sRecMaster, "a", "b", 1, {}, 24, 44);
            bytes.append(interiorCell(0x900, "HallA"));
            paths.push_back(dir.write("a.esm", bytes));
            SkyrimWorldView::Options options;
            options.mNotice = [this](const std::string& text) {
                const std::lock_guard lock(noticeMutex);
                notices.push_back(text);
            };
            EXPECT_TRUE(view.configure(std::move(options), nullptr));
            view.setPlayerBody(playerBody);
            EXPECT_TRUE(view.startLoad(paths, nullptr));
            const auto end = std::chrono::steady_clock::now() + 10s;
            while (!view.isReady() && std::chrono::steady_clock::now() < end)
                std::this_thread::sleep_for(2ms); // the plugin is a few hundred bytes
        }
        bool noticed(const std::string& part)
        {
            const std::lock_guard lock(noticeMutex);
            for (const std::string& text : notices)
                if (text.find(part) != std::string::npos)
                    return true;
            return false;
        }
    };

    void buildFirstCellThenGiveTheWorkerTime(Fixture& f)
    {
        const auto cell = f.view.findCellByEditorId("HallA");
        ASSERT_TRUE(cell.has_value());
        openeden::sdk::game::WorldCellContent content;
        std::string error;
        ASSERT_TRUE(f.view.loadCell(cell->id, content, error)) << error;
        std::this_thread::sleep_for(1000ms); // the records of one tiny plugin take a few milliseconds
    }

    TEST(SkyrimSeEarlyActorIndex, theFirstCellBuildStartsTheWorkerAndTheFirstActorAdoptsItsResult)
    {
        Fixture f(true);
        buildFirstCellThenGiveTheWorkerTime(f);
        openeden::sdk::game::ActorAppearance out;
        std::string error;
        (void)f.view.describeActor(0x800, 0, out, error); // there is no such NPC: only the index matters here
        EXPECT_TRUE(f.noticed("combat records read on a worker")) << "the worker's result is adopted";
        EXPECT_FALSE(f.noticed("actor records read on the main thread")) << "the main thread did not read them again";
    }

    TEST(SkyrimSeEarlyActorIndex, withoutThePlayerBodyNothingIsReadAheadAndTheOldPathStays)
    {
        Fixture f(false);
        buildFirstCellThenGiveTheWorkerTime(f);
        openeden::sdk::game::ActorAppearance out;
        std::string error;
        (void)f.view.describeActor(0x800, 0, out, error);
        EXPECT_FALSE(f.noticed("combat records read on a worker")) << "no worker was started";
        EXPECT_TRUE(f.noticed("actor records read on the main thread")) << "the actor records are read as before";
    }
}
