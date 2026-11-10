// Task 0086, world model option 1: the aggregate check of the Skyrim SE world store on the player's own plugins. It
// prints counts, times and memory only (lines starting SKYRIMSE-WORLD), never a cell, record or mod name, and writes
// nothing. Skipped when the environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA     the game's Data folder (required)
//   OPENEDEN_SKYRIMSE_MO2      optional: a Mod Organizer 2 instance folder (holding mods/ and profiles/); the plugins
//   of
//                              the profile's loadorder.txt are loaded in that order
//   OPENEDEN_SKYRIMSE_PROFILE  optional: the profile name (default "Default")
#include <game_skyrimse/world_view.hpp>

#include <components/contentsource/plugin.hpp>

#include <gtest/gtest.h>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <psapi.h>
#endif

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    namespace sdkgame = openeden::sdk::game;
    using namespace game_skyrimse;
    using Clock = std::chrono::steady_clock;

    std::string envOr(const char* name)
    {
        const char* v = std::getenv(name);
        return v != nullptr ? v : "";
    }

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    double mebibytesInUse()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return static_cast<double>(counters.WorkingSetSize) / (1024.0 * 1024.0);
#endif
        return 0.0;
    }

    double peakMebibytes()
    {
#ifdef _WIN32
        PROCESS_MEMORY_COUNTERS counters{};
        if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
            return static_cast<double>(counters.PeakWorkingSetSize) / (1024.0 * 1024.0);
#endif
        return 0.0;
    }

    double secondsSince(Clock::time_point start)
    {
        return std::chrono::duration<double>(Clock::now() - start).count();
    }

    // form id -> record type for every record of one plugin (a diagnostic: what the unresolved bases are).
    class TypeMap final : public Content::Visitor
    {
    public:
        std::map<std::uint32_t, std::uint32_t> mTypes;
        bool onRecord(const Content::RawRecord& r) override
        {
            mTypes[r.mFormId] = r.mType;
            return true;
        }
    };

    // Loads the plugins, then loads every interior cell and an even sample of exterior cells; prints aggregates.
    void runStore(const char* label, const std::vector<fs::path>& paths, const fs::path& data)
    {
        const double before = mebibytesInUse();
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        const auto t0 = Clock::now();
        const std::size_t taken = view.load(paths, problems);
        const double indexSeconds = secondsSince(t0);
        const double afterIndex = mebibytesInUse();
        const SkyrimWorldView::Stats& s = view.stats();
        std::cout << "SKYRIMSE-WORLD " << label << " index: plugins " << taken << " of " << paths.size() << " refused "
                  << s.mPluginsRefused << " cells " << s.mCells << " worlds " << s.mWorlds << " base forms "
                  << s.mBaseForms << " references in the index " << s.mReferencesInIndex << " string tables missing "
                  << s.mStringTablesMissing << " unresolved form ids " << s.mUnresolvedFormIds << " record errors "
                  << s.mRecordErrors << " time " << indexSeconds << " s memory +" << (afterIndex - before)
                  << " MiB (working set " << afterIndex << ", peak " << peakMebibytes() << ")\n";

        std::vector<sdkgame::WorldCellInfo> cells;
        view.listCells(0, view.cellCount(), cells);
        std::uint64_t interiors = 0, exteriors = 0, withName = 0;
        for (const sdkgame::WorldCellInfo& c : cells)
        {
            if (c.interior)
                ++interiors;
            else
                ++exteriors;
            if (!c.name.empty())
                ++withName;
        }
        std::cout << "SKYRIMSE-WORLD " << label << " lists: interior " << interiors << " exterior " << exteriors
                  << " with a display name " << withName << "\n";

        // Load: every interior cell, and about 2000 exterior cells spread over the sorted list.
        std::uint64_t loaded = 0, failed = 0, objects = 0, unresolved = 0, withModel = 0, withBounds = 0, nif = 0;
        std::map<int, std::uint64_t> kinds;
        TypeMap typeMap;
        std::map<std::string, std::uint64_t> unresolvedTypes;
        if (!paths.empty())
        {
            Content::Error openError;
            if (const auto plugin = Content::Plugin::openFile(paths.front(), openError))
            {
                Content::WalkOptions walkOptions;
                walkOptions.mReadData = false;
                (void)plugin->walk(typeMap, walkOptions);
            }
        }
        std::map<std::string, std::uint64_t> failures;
        double worst = 0.0;
        const std::size_t exteriorStep = std::max<std::size_t>(1, exteriors / 2000);
        std::size_t exteriorSeen = 0;
        const auto t1 = Clock::now();
        for (const sdkgame::WorldCellInfo& c : cells)
        {
            if (!c.interior)
            {
                if (exteriorSeen++ % exteriorStep != 0)
                    continue;
            }
            sdkgame::WorldCellContent content;
            std::string error;
            const auto t2 = Clock::now();
            const bool ok = view.loadCell(c.id, content, error);
            worst = std::max(worst, secondsSince(t2));
            if (!ok)
            {
                ++failed;
                std::string kind;
                for (char ch : error)
                    if (!std::isdigit(static_cast<unsigned char>(ch)))
                        kind.push_back(ch);
                ++failures[kind.substr(kind.find(':') == std::string::npos ? 0 : kind.find(':'))];
                continue;
            }
            ++loaded;
            objects += content.objects.size();
            unresolved += content.unresolvedBases;
            for (const sdkgame::PlacedObject& o : content.objects)
            {
                if (o.baseKind == sdkgame::BaseKind::Unknown)
                {
                    const auto t = typeMap.mTypes.find(o.base);
                    ++unresolvedTypes[o.base == 0       ? "(no base)"
                            : t != typeMap.mTypes.end() ? Content::fourCCToString(t->second)
                                                        : "(not in the first plugin)"];
                }
                ++kinds[static_cast<int>(o.baseKind)];
                if (!o.modelPath.empty())
                {
                    ++withModel;
                    const std::string m = lower(o.modelPath);
                    if (m.size() > 4 && m.compare(m.size() - 4, 4, ".nif") == 0)
                        ++nif;
                }
                if (o.hasBounds)
                    ++withBounds;
            }
        }
        const double loadSeconds = secondsSince(t1);
        std::cout << "SKYRIMSE-WORLD " << label << " cells loaded: ok " << loaded << " failed " << failed << " objects "
                  << objects << " unresolved bases " << unresolved << " with a model " << withModel << " (.nif " << nif
                  << ") with bounds " << withBounds << " time " << loadSeconds << " s, slowest cell " << worst
                  << " s, cell reads " << s.mCellReads << " references decoded " << s.mReferencesDecoded
                  << " bytes read " << s.mBytesRead << " cache hits " << s.mCacheHits << " dropped "
                  << s.mObjectsDropped << " memory now " << mebibytesInUse() << " MiB (peak " << peakMebibytes()
                  << ")\n";
        std::cout << "SKYRIMSE-WORLD " << label << " kinds:";
        for (const auto& [kind, n] : kinds)
            std::cout << " " << kind << "=" << n;
        std::cout << "\n";
        std::cout << "SKYRIMSE-WORLD " << label << " unknown base types:";
        for (const auto& [type, n] : unresolvedTypes)
            std::cout << " " << type << "=" << n;
        std::cout << '\n';
        for (const auto& [why, n] : failures)
            std::cout << "SKYRIMSE-WORLD " << label << " load failure kind (" << n << "): " << why << "\n";

        // A second pass over the interior cells: the same objects, to pin determinism on real data.
        SkyrimWorldView second({}, &files);
        std::vector<std::string> problems2;
        second.load(paths, problems2);
        std::uint64_t compared = 0, differing = 0;
        for (const sdkgame::WorldCellInfo& c : cells)
        {
            if (!c.interior || compared >= 300)
                continue;
            sdkgame::WorldCellContent a, b;
            std::string error;
            if (view.loadCell(c.id, a, error) && second.loadCell(c.id, b, error))
            {
                ++compared;
                if (!(a.objects == b.objects))
                    ++differing;
            }
        }
        std::cout << "SKYRIMSE-WORLD " << label << " determinism: " << compared << " interior cells compared, "
                  << differing << " differ\n";
        EXPECT_EQ(differing, 0u);
        EXPECT_EQ(s.mReferencesInIndex, 0u);
        EXPECT_GT(taken, 0u);
    }

    TEST(SkyrimWorldViewRealData, theGamesOwnMasters)
    {
        const std::string dataEnv = envOr("OPENEDEN_SKYRIMSE_DATA");
        if (dataEnv.empty() || !fs::is_directory(dataEnv))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = dataEnv;
        std::vector<fs::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            if (fs::exists(data / name))
                paths.push_back(data / name);
        runStore("masters", paths, data);
    }

    TEST(SkyrimWorldViewRealData, theModList)
    {
        const std::string dataEnv = envOr("OPENEDEN_SKYRIMSE_DATA");
        const std::string mo2Env = envOr("OPENEDEN_SKYRIMSE_MO2");
        if (dataEnv.empty() || mo2Env.empty() || !fs::is_directory(mo2Env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA and OPENEDEN_SKYRIMSE_MO2 are not both set";
        std::string profile = envOr("OPENEDEN_SKYRIMSE_PROFILE");
        if (profile.empty())
            profile = "Default";
        std::ifstream list(fs::path(mo2Env) / "profiles" / profile / "loadorder.txt");
        if (!list)
            GTEST_SKIP() << "no loadorder.txt";
        std::vector<std::string> names;
        for (std::string line; std::getline(list, line);)
        {
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (line.empty() || line[0] == '#')
                continue;
            if (line[0] == '*')
                line.erase(0, 1);
            names.push_back(line);
        }
        std::map<std::string, fs::path> mods;
        std::error_code ec;
        for (const auto& mod : fs::directory_iterator(fs::path(mo2Env) / "mods", ec))
        {
            if (!mod.is_directory(ec))
                continue;
            for (const auto& f : fs::directory_iterator(mod.path(), ec))
            {
                const std::string e = lower(f.path().extension().string());
                if (f.is_regular_file(ec) && (e == ".esm" || e == ".esp" || e == ".esl"))
                    mods[lower(f.path().filename().string())] = f.path();
            }
        }
        std::vector<fs::path> paths;
        for (const std::string& name : names)
        {
            const fs::path inData = fs::path(dataEnv) / name;
            if (fs::exists(inData, ec))
                paths.push_back(inData);
            else if (const auto it = mods.find(lower(name)); it != mods.end())
                paths.push_back(it->second);
        }
        runStore("mods", paths, fs::path(dataEnv));
    }
}
