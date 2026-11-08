// Task 0093 gated real-data check of the .pex reader (stage b; stage f adds the VM runs).
//
// In plain words: with the player's own copy of Skyrim Special Edition or Fallout 4 (read only), this reads every
// compiled script it can find (the loose ones in the game's Scripts folder and the ones inside the game's archives,
// and, when asked, every script of the player's mod folders) and prints AGGREGATE numbers only: how many files, how
// many were accepted, which versions and which object size conventions they use, why the others were refused, and,
// for the game's own scripts, how many of the calls between scripts the loaded set can resolve (link). Nothing from the
// game is written anywhere: no script text, no file. The check is skipped, never failed, when the data is absent.
//
// Run it with tools/papyrus_check/check.sh, or set OPENEDEN_PAPYRUS_DATA to the game's Data folder (and optionally
// OPENEDEN_PAPYRUS_MODS to a folder holding mod folders, as Mod Organizer 2's "mods" folder does).

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include <components/bsa/ba2gnrlfile.hpp>
#include <components/bsa/bsafile.hpp>
#include <components/bsa/compressedbsafile.hpp>
#include <components/script_papyrus/papyrusruntime.hpp>
#include <components/script_papyrus/pexfile.hpp>

namespace
{
    namespace fs = std::filesystem;
    namespace Pex = ScriptPapyrus::Pex;
    using namespace openeden::script;

    std::string lowerText(std::string text)
    {
        for (char& c : text)
        {
            if (c >= 'A' && c <= 'Z')
            {
                c = static_cast<char>(c - 'A' + 'a');
            }
        }
        return text;
    }

    bool endsWith(const std::string& text, const std::string& suffix)
    {
        return text.size() >= suffix.size() && text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0;
    }

    std::vector<std::uint8_t> readAll(std::istream& in)
    {
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // Calls \a visit(name, bytes) for every ".pex" under \a root: loose files, and the files inside every archive.
    template <class Visitor>
    void forEachPex(
        const fs::path& root, bool recursive, std::size_t& archivesRead, std::size_t& archivesSkipped, Visitor&& visit)
    {
        std::vector<fs::path> files;
        std::error_code ec;
        if (recursive)
        {
            for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
                 !ec && it != end; it.increment(ec))
            {
                if (it->is_regular_file(ec))
                {
                    files.push_back(it->path());
                }
            }
        }
        else
        {
            for (fs::directory_iterator it(root, ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->is_regular_file(ec))
                {
                    files.push_back(it->path());
                }
            }
            const fs::path scripts = root / "Scripts";
            if (fs::is_directory(scripts, ec))
            {
                for (fs::recursive_directory_iterator it(scripts, ec), end; !ec && it != end; it.increment(ec))
                {
                    if (it->is_regular_file(ec))
                    {
                        files.push_back(it->path());
                    }
                }
            }
        }
        std::sort(files.begin(), files.end());
        for (const fs::path& path : files)
        {
            const std::string ext = lowerText(path.extension().string());
            if (ext == ".pex")
            {
                std::ifstream in(path, std::ios::binary);
                visit(path.filename().string(), readAll(in));
            }
            else if (ext == ".bsa" || ext == ".ba2")
            {
                try
                {
                    // A BA2 is general (files) or textures; only a general archive can hold scripts.
                    std::ifstream probe(path, std::ios::binary);
                    char head[12] = {};
                    probe.read(head, sizeof(head));
                    const bool isBa2 = std::string(head, 4) == "BTDX";
                    if (isBa2 && std::string(head + 8, 4) != "GNRL")
                    {
                        continue;
                    }
                    if (!isBa2 && std::string(head, 3) != "BSA")
                    {
                        ++archivesSkipped;
                        continue;
                    }
                    if (isBa2)
                    {
                        Bsa::BA2GNRLFile archive;
                        archive.open(path);
                        ++archivesRead;
                        for (const auto& entry : archive.getList())
                        {
                            const std::string name = lowerText(std::string(entry.name()));
                            if (endsWith(name, ".pex"))
                            {
                                auto stream = archive.getFile(&entry);
                                visit(name, readAll(*stream));
                            }
                        }
                    }
                    else
                    {
                        Bsa::CompressedBSAFile archive;
                        archive.open(path);
                        ++archivesRead;
                        for (const auto& entry : archive.getList())
                        {
                            const std::string name = lowerText(std::string(entry.name()));
                            if (endsWith(name, ".pex"))
                            {
                                auto stream = archive.getFile(&entry);
                                visit(name, readAll(*stream));
                            }
                        }
                    }
                }
                catch (const std::exception& error)
                {
                    ++archivesSkipped;
                    std::cout << "PAPYRUS-CHECK archive skipped: " << error.what() << "\n";
                }
            }
        }
    }

    // A message with its numbers and its location taken out, so that the same kind of refusal counts as one.
    std::string category(const std::string& message)
    {
        std::string text = message;
        const std::size_t colon = text.find(": ");
        if (colon != std::string::npos && text.find('@') < colon)
        {
            text = text.substr(colon + 2);
        }
        std::string result;
        bool inDigits = false;
        for (char c : text)
        {
            if (c >= '0' && c <= '9')
            {
                if (!inDigits)
                {
                    result += '#';
                }
                inDigits = true;
            }
            else
            {
                inDigits = false;
                result += c;
            }
        }
        const std::size_t quote = result.find('\'');
        if (quote != std::string::npos)
        {
            const std::size_t closing = result.find('\'', quote + 1);
            if (closing != std::string::npos)
            {
                result = result.substr(0, quote) + "'...'" + result.substr(closing + 1);
            }
        }
        return result;
    }

    struct Tally
    {
        std::string mOrigin;
        std::size_t mFiles = 0;
        std::size_t mOk = 0;
        std::size_t mFailed = 0;
        std::size_t mDeviant = 0;
        std::map<std::string, std::size_t> mVersions;
        std::map<std::string, std::size_t> mConventions;
        std::map<std::string, std::size_t> mFailures;
        std::size_t mArchivesRead = 0;
        std::size_t mArchivesSkipped = 0;

        void print() const
        {
            std::cout << "PAPYRUS-CHECK " << mOrigin << ": files " << mFiles << ", accepted " << mOk << ", refused "
                      << mFailed << ", archives read " << mArchivesRead << ", skipped " << mArchivesSkipped << "\n";
            for (const auto& [version, n] : mVersions)
            {
                std::cout << "PAPYRUS-CHECK " << mOrigin << ": version " << version << ": " << n << "\n";
            }
            for (const auto& [convention, n] : mConventions)
            {
                std::cout << "PAPYRUS-CHECK " << mOrigin << ": object size " << convention << ": " << n << "\n";
            }
            for (const auto& [why, n] : mFailures)
            {
                std::cout << "PAPYRUS-CHECK " << mOrigin << ": refused for '" << why << "': " << n << "\n";
            }
        }
    };

    class SilentWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId, std::string&) const override { return false; }
        void report(std::string_view) override {}
        void log(Severity, std::string_view) override { ++mLines; }
        std::size_t mLines = 0;
    };

    void readOrigin(Tally& tally, const fs::path& root, bool recursive, PapyrusContract* runtime,
        std::vector<std::string>* failureNames)
    {
        forEachPex(root, recursive, tally.mArchivesRead, tally.mArchivesSkipped,
            [&](const std::string& name, const std::vector<std::uint8_t>& bytes) {
                ++tally.mFiles;
                const Pex::ParseResult result = Pex::parse(bytes, name);
                if (!result.mOk)
                {
                    ++tally.mFailed;
                    ++tally.mFailures[category(result.mReports.empty() ? "no report" : result.mReports[0].message)];
                    if (failureNames != nullptr)
                    {
                        failureNames->push_back(
                            name + ": " + (result.mReports.empty() ? "" : result.mReports[0].message));
                    }
                    return;
                }
                ++tally.mOk;
                const Pex::File& file = result.mFile;
                ++tally.mVersions[std::to_string(file.mMajor) + "." + std::to_string(file.mMinor) + " game "
                    + (file.mGame == Pex::Game::Fallout4 ? "Fallout 4" : "Skyrim SE")
                    + (file.mBigEndian ? " big endian" : " little endian")];
                for (const Pex::Object& object : file.mObjects)
                {
                    const char* label = object.mSizeConvention == PexSizeConvention::IncludesSelf
                        ? "includes its own field"
                        : object.mSizeConvention == PexSizeConvention::ExcludesSelf
                        ? "bytes after the field"
                        : "bytes after the field, other order";
                    ++tally.mConventions[label];
                    if (object.mSizeConvention != PexSizeConvention::IncludesSelf)
                    {
                        ++tally.mDeviant;
                    }
                }
                if (runtime != nullptr)
                {
                    runtime->loadScript(bytes, name);
                }
            });
    }

    TEST(PapyrusRealData, every_installed_script_parses)
    {
        const char* dataName = std::getenv("OPENEDEN_PAPYRUS_DATA");
        if (dataName == nullptr || !fs::is_directory(dataName))
        {
            GTEST_SKIP() << "OPENEDEN_PAPYRUS_DATA does not name a Skyrim SE or Fallout 4 Data folder";
        }
        std::vector<std::string> failureNames;

        SilentWorld world;
        auto runtime = ScriptPapyrus::createRuntime(world);
        Tally game;
        game.mOrigin = "game data";
        readOrigin(game, dataName, false, runtime.get(), &failureNames);
        game.print();

        std::size_t gameDeviant = game.mDeviant;
        std::size_t refused = game.mFailed;
        const LinkReport link = runtime->link();
        const double total = static_cast<double>(link.callSites == 0 ? 1 : link.callSites);
        std::cout << "PAPYRUS-CHECK game data: call sites " << link.callSites << ", resolved to a script function "
                  << link.resolvedScript << ", to a native " << link.resolvedNative << " (together "
                  << 100.0 * static_cast<double>(link.resolvedScript + link.resolvedNative) / total
                  << " percent), class not loaded " << link.missingClass << ", function missing "
                  << link.missingFunction << ", receiver class not known statically " << link.dynamicReceiver << "\n";
        std::cout << "PAPYRUS-CHECK game data: log lines for deviant files " << world.mLines << "\n";

        const char* modsName = std::getenv("OPENEDEN_PAPYRUS_MODS");
        if (modsName != nullptr && fs::is_directory(modsName))
        {
            Tally mods;
            mods.mOrigin = "mod folders";
            readOrigin(mods, modsName, true, nullptr, &failureNames);
            mods.print();
            gameDeviant += mods.mDeviant;
            refused += mods.mFailed;
        }
        std::cout << "PAPYRUS-CHECK deviant object sizes accepted in all origins: " << gameDeviant << "\n";

        if (const char* listName = std::getenv("OPENEDEN_PAPYRUS_FAILLIST"); listName != nullptr)
        {
            std::ofstream out(listName);
            for (const std::string& line : failureNames)
            {
                out << line << "\n";
            }
        }
        EXPECT_GT(game.mFiles, 0u);
        EXPECT_EQ(refused, 0u);
    }
}
