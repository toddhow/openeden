// Task 0086 stage d: the install-in-place check. It reads the player's own Skyrim Special Edition files through the
// pack and prints AGGREGATE numbers only (lines starting SKYRIMSE-CHECK). Nothing is written anywhere and no path of a
// mod folder is ever printed. Skipped when OPENEDEN_SKYRIMSE_DATA (the game's Data folder) is not set.
//
//   OPENEDEN_SKYRIMSE_DATA       the game's Data folder
//   OPENEDEN_SKYRIMSE_NIF_LIST   optional: a text file with one NIF path per line (the player's own set, outside the
//                                repository); each is read as a Nif::Scene and only totals are printed
#include <compat_skyrimse/scene_options.hpp>
#include <compat_skyrimse/skyrimse_compat_extension.hpp>
#include <game_skyrimse/pack_core.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    std::string tag(std::uint32_t type)
    {
        return std::string(reinterpret_cast<const char*>(&type), 4);
    }

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    // Walks with data and inflates every compressed record, counting the failures.
    class InflateAll : public Content::Visitor
    {
    public:
        explicit InflateAll(std::string name)
            : mName(std::move(name))
        {
        }
        bool onRecord(const Content::RawRecord& record) override
        {
            if (!record.isCompressed())
                return true;
            ++mCompressed;
            std::vector<std::byte> out;
            Content::Error error;
            if (!inflateRecord(record, mName, out, error))
                ++mFailures;
            else
                mInflatedBytes += out.size();
            return true;
        }
        std::string mName;
        std::uint64_t mCompressed = 0;
        std::uint64_t mFailures = 0;
        std::uint64_t mInflatedBytes = 0;
    };

    struct SceneTotals
    {
        std::uint64_t mFiles = 0;
        std::uint64_t mExceptions = 0;
        std::uint64_t mNodes = 0;
        std::uint64_t mGeometries = 0;
        std::uint64_t mSkinned = 0;
        std::uint64_t mBones = 0;
        std::uint64_t mVertices = 0;
        std::uint64_t mTriangles = 0;
        std::uint64_t mWarnings = 0;

        void add(const NifScene::Scene& scene)
        {
            ++mFiles;
            mNodes += scene.mNodes.size();
            mGeometries += scene.mGeometries.size();
            for (const NifScene::Geometry& g : scene.mGeometries)
            {
                if (g.mSkin != NifScene::sNone)
                    ++mSkinned;
                mVertices += g.mPositions.size();
                mTriangles += g.mTriangles.size() / 3;
            }
            for (const NifScene::Skin& s : scene.mSkins)
                mBones += s.mBoneNames.size();
            mWarnings += scene.mWarnings.size();
        }

        void print(const std::string& label) const
        {
            std::cout << "SKYRIMSE-CHECK " << label << " files " << mFiles << " exceptions " << mExceptions << " nodes "
                      << mNodes << " geometries " << mGeometries << " skinned " << mSkinned << " skinbones " << mBones
                      << " vertices " << mVertices << " triangles " << mTriangles << " warnings " << mWarnings << "\n";
        }
    };

    bool readScene(const std::string& bytes, const std::string& name, NifScene::Scene& scene, std::string& error)
    {
        try
        {
            auto input = std::make_unique<std::istringstream>(bytes);
            const std::string normalised = lower(name);
            scene = NifScene::readScene(
                std::move(input), VFS::Path::Normalized(normalised), compat_skyrimse::skyrimSceneOptions());
            return true;
        }
        catch (const std::exception& e)
        {
            error = e.what();
            return false;
        }
    }

    TEST(SkyrimSeRealData, thePackReadsTheInstalledGameInPlace)
    {
        const char* const env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;

        // ---- plugins ----
        std::uint64_t pluginsRead = 0;
        std::uint64_t pluginErrors = 0;
        std::map<std::string, std::uint64_t> unknownTypes;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            const fs::path path = data / name;
            if (!fs::exists(path))
            {
                std::cout << "SKYRIMSE-CHECK plugin " << name << " absent\n";
                continue;
            }
            RecordCensus census;
            Content::Header header;
            Content::Error error;
            if (!scanPlugin(path, census, header, error))
            {
                ++pluginErrors;
                ADD_FAILURE() << error.describe();
                continue;
            }
            ++pluginsRead;
            std::cout << "SKYRIMSE-CHECK plugin " << name << " masters " << header.mMasters.size() << " formversion "
                      << header.mFormVersion << " localized " << header.isLocalized() << " records " << census.mRecords
                      << " recognised " << census.mRecognised << " unknown " << census.mUnknown << " compressed "
                      << census.mCompressed << " groups " << census.mGroups << " types " << census.mByType.size()
                      << "\n";
            std::ostringstream types;
            for (const auto& [type, count] : census.mByType)
                types << ' ' << tag(type) << '=' << count;
            std::cout << "SKYRIMSE-CHECK pertype " << name << types.str() << "\n";
            for (const auto& [type, count] : census.mUnknownByType)
                unknownTypes[tag(type)] += count;

            Content::Error openError;
            auto plugin = Content::Plugin::openFile(path, openError);
            ASSERT_NE(plugin, nullptr) << openError.describe();
            InflateAll inflate(name);
            const auto walkError = plugin->walk(inflate);
            EXPECT_FALSE(walkError.has_value()) << (walkError ? walkError->describe() : "");
            std::cout << "SKYRIMSE-CHECK inflate " << name << " compressed " << inflate.mCompressed << " failures "
                      << inflate.mFailures << " inflatedMiB " << inflate.mInflatedBytes / (1024 * 1024) << "\n";
            EXPECT_EQ(inflate.mFailures, 0u);
        }
        std::cout << "SKYRIMSE-CHECK plugins read " << pluginsRead << " errors " << pluginErrors << " unknowntypes "
                  << unknownTypes.size() << "\n";
        for (const auto& [type, count] : unknownTypes)
            std::cout << "SKYRIMSE-CHECK unknowntype " << type << " " << count << "\n";

        // ---- archives ----
        std::vector<fs::path> archives;
        for (const auto& entry : fs::directory_iterator(data))
        {
            if (entry.is_regular_file() && lower(entry.path().extension().string()) == ".bsa")
                archives.push_back(entry.path());
        }
        std::sort(archives.begin(), archives.end());
        std::uint64_t archiveErrors = 0;
        std::uint64_t files = 0;
        std::map<std::string, std::uint64_t> byExtension;
        std::map<std::string, fs::path> where; // wanted file -> archive
        const std::vector<std::string> wanted = { std::string(compat_skyrimse::defaultMaleSkeleton()),
            std::string(compat_skyrimse::defaultFemaleSkeleton()),
            "meshes/actors/character/character assets/malebody_0.nif",
            "meshes/actors/character/character assets/femalebody_0.nif" };
        for (const fs::path& archive : archives)
        {
            ArchiveListing listing;
            std::string error;
            if (!readArchiveListing(archive, listing, error))
            {
                ++archiveErrors;
                std::cout << "SKYRIMSE-CHECK archive refused: " << error << "\n";
                continue;
            }
            files += listing.mFiles.size();
            for (const std::string& name : listing.mFiles)
            {
                std::string slashed = lower(name);
                std::replace(slashed.begin(), slashed.end(), '\\', '/');
                const std::size_t dot = slashed.rfind('.');
                ++byExtension[dot == std::string::npos ? "" : slashed.substr(dot)];
                for (const std::string& w : wanted)
                {
                    if (slashed == w)
                        where.emplace(w, archive);
                }
            }
        }
        std::cout << "SKYRIMSE-CHECK archives " << archives.size() << " refused " << archiveErrors << " files " << files
                  << " nif " << byExtension[".nif"] << " hkx " << byExtension[".hkx"] << " pex " << byExtension[".pex"]
                  << " dds " << byExtension[".dds"] << "\n";
        EXPECT_EQ(archiveErrors, 0u);

        // ---- the humanoid skeletons and bodies as scenes ----
        SceneTotals totals;
        for (const std::string& w : wanted)
        {
            const auto found = where.find(w);
            if (found == where.end())
            {
                std::cout << "SKYRIMSE-CHECK entry " << fs::path(w).filename().string()
                          << " not in the installed archives\n";
                continue;
            }
            std::string bytes;
            std::string error;
            ASSERT_TRUE(readArchiveEntry(found->second, w, bytes, error)) << error;
            NifScene::Scene scene;
            if (!readScene(bytes, w, scene, error))
            {
                ++totals.mExceptions;
                ADD_FAILURE() << w << ": " << error;
                continue;
            }
            totals.add(scene);
            if (w == compat_skyrimse::defaultMaleSkeleton())
            {
                // The bone names the contract gives must be nodes of the game's own skeleton
                const compat_skyrimse::SkyrimGameContract contract;
                const auto names = contract.getHumanoidBoneNames();
                std::set<std::string> nodes;
                for (const NifScene::Node& node : scene.mNodes)
                    nodes.insert(lower(node.mName));
                int present = 0;
                for (const std::string_view bone : { names.root, names.head, names.spine1, names.spine2 })
                    present += nodes.count(std::string(bone)) != 0 ? 1 : 0;
                std::cout << "SKYRIMSE-CHECK contract bones found in skeleton.nif: " << present << " of 4\n";
                EXPECT_EQ(present, 4);
            }
            std::cout << "SKYRIMSE-CHECK entry " << fs::path(w).filename().string() << " bytes " << bytes.size()
                      << " nodes " << scene.mNodes.size() << " geometries " << scene.mGeometries.size() << " skins "
                      << scene.mSkins.size() << "\n";
        }
        totals.print("scenes(archive entries)");

        // ---- the player's own set, if named ----
        if (const char* const list = std::getenv("OPENEDEN_SKYRIMSE_NIF_LIST"); list != nullptr)
        {
            std::ifstream in(list);
            std::string line;
            SceneTotals set;
            while (std::getline(in, line))
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                if (line.empty())
                    continue;
                std::ifstream file(line, std::ios::binary);
                std::ostringstream bytes;
                bytes << file.rdbuf();
                NifScene::Scene scene;
                std::string error;
                if (!file || !readScene(bytes.str(), "set.nif", scene, error))
                    ++set.mExceptions;
                else
                    set.add(scene);
            }
            set.print("set");
        }
    }
}
