// Task 0116: the gated real-data run of the Skyrim SE data survey. It prints AGGREGATE numbers only (lines starting
// SKYRIMSE-SURVEY): condition function indices and their uses, native Papyrus function names (these are public API
// names of the scripting language, taken from the class files) and event names, NIF block type names. No record name,
// form id, script name, mesh path or mod path is printed and nothing is written. Skipped when the environment is not
// set. Run it with tools/skyrimse_survey_check/check.sh.
//
//   OPENEDEN_SKYRIMSE_DATA          the game's Data folder (required)
//   OPENEDEN_SKYRIMSE_SURVEY_PARTS  optional: comma list of conditions, papyrus, nif (default: all)
//   OPENEDEN_SKYRIMSE_SURVEY_NIF_MAX  optional: stop the NIF part after this many files (default: all)
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/records.hpp>
#include <game_skyrimse/survey.hpp>
#include <game_skyrimse/survey_nif.hpp>
#include <game_skyrimse/survey_scripts.hpp>

#include <gtest/gtest.h>

#include <components/bsa/compressedbsafile.hpp>
#include <components/nif/niffile.hpp>
#include <components/script_papyrus/papyrusruntime.hpp>
#include <components/script_papyrus/pexfile.hpp>
#include <components/vfs/pathutil.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;
    using namespace game_skyrimse::survey;

    std::string lower(std::string s)
    {
        std::transform(
            s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    bool partWanted(const char* part)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_SURVEY_PARTS");
        if (env == nullptr || *env == 0)
            return true;
        return (std::string(",") + lower(env) + ",").find(std::string(",") + part + ",") != std::string::npos;
    }

    fs::path dataFolder()
    {
        const char* data = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (data == nullptr || !fs::is_directory(data))
            return {};
        return data;
    }

    void line(const std::string& text)
    {
        std::cout << "SKYRIMSE-SURVEY " << text << "\n";
    }

    // ---- conditions ----

    struct PluginFile
    {
        std::string mName;
        fs::path mPath;
    };

    class IdCollector final : public Content::Visitor
    {
    public:
        IdCollector(const FormIdResolver& resolver, std::unordered_map<std::uint32_t, std::uint32_t>& types)
            : mResolver(resolver)
            , mTypes(types)
        {
        }
        bool onRecord(const Content::RawRecord& record) override
        {
            if (const auto id = mResolver.resolve(record.mFormId); id.has_value())
                mTypes[*id] = record.mType;
            return true;
        }

    private:
        const FormIdResolver& mResolver;
        std::unordered_map<std::uint32_t, std::uint32_t>& mTypes;
    };

    class SurveyVisitor final : public Content::Visitor
    {
    public:
        SurveyVisitor(ConditionSurvey& survey, const std::string& file)
            : mSurvey(survey)
            , mFile(file)
        {
        }
        bool onRecord(const Content::RawRecord& record) override
        {
            if (record.isCompressed())
            {
                Content::Error error;
                if (!inflateRecord(record, mFile, mBuffer, error))
                {
                    ++mInflateFailures;
                    return true;
                }
                mSurvey.addRecord(record.mType, mBuffer);
            }
            else
            {
                mSurvey.addRecord(record.mType, record.mData);
            }
            return true;
        }
        std::uint64_t mInflateFailures = 0;

    private:
        ConditionSurvey& mSurvey;
        const std::string& mFile;
        std::vector<std::byte> mBuffer;
    };

    TEST(SkyrimSeSurveyReal, conditions)
    {
        const fs::path data = dataFolder();
        if (data.empty() || !partWanted("conditions"))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder, or the part is off";

        std::vector<PluginFile> files;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            if (fs::is_regular_file(data / name))
                files.push_back({ name, data / name });
        }
        ASSERT_FALSE(files.empty());

        LoadOrder order;
        std::vector<std::unique_ptr<Content::Plugin>> plugins;
        for (const PluginFile& f : files)
        {
            Content::Error error;
            auto plugin = Content::Plugin::openFile(f.mPath, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            std::string why;
            ASSERT_TRUE(order.add(f.mName, plugin->header().isLight(), why)) << why;
            plugins.push_back(std::move(plugin));
        }

        std::unordered_map<std::uint32_t, std::uint32_t> types;
        types.reserve(1u << 21);
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            FormIdResolver resolver(plugins[i]->header(), files[i].mName, order);
            IdCollector collector(resolver, types);
            Content::WalkOptions options;
            options.mReadData = false;
            const auto error = plugins[i]->walk(collector, options);
            EXPECT_FALSE(error.has_value()) << (error ? error->describe() : "");
        }

        ConditionSurvey survey;
        std::uint64_t inflateFailures = 0;
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            FormIdResolver resolver(plugins[i]->header(), files[i].mName, order);
            survey.setFormTypeLookup([&](std::uint32_t stored) -> std::optional<std::uint32_t> {
                const auto id = resolver.resolve(stored);
                if (!id.has_value())
                    return std::nullopt;
                const auto it = types.find(*id);
                if (it == types.end())
                    return std::nullopt;
                return it->second;
            });
            SurveyVisitor visitor(survey, files[i].mName);
            const auto error = plugins[i]->walk(visitor);
            EXPECT_FALSE(error.has_value()) << (error ? error->describe() : "");
            inflateFailures += visitor.mInflateFailures;
        }
        line("conditions: plugins " + std::to_string(files.size()) + ", form ids known " + std::to_string(types.size())
            + ", inflate failures " + std::to_string(inflateFailures));
        writeConditionReport(survey, std::cout, 400);
        EXPECT_GT(survey.mConditions, 0u);
    }

    // ---- scripts and meshes ----

    std::vector<std::uint8_t> readAll(std::istream& in)
    {
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // Calls visit(lower-case name, bytes) for each file with the extension in the Data folder's loose Scripts/Meshes
    // folder (when asked) and in every BSA of the Data folder.
    template <class Visit>
    void forEachFile(const fs::path& data, const std::string& extension, Visit&& visit)
    {
        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
        {
            if (!it->is_regular_file(ec) || lower(it->path().extension().string()) != ".bsa")
                continue;
            // the archives of the five masters only (not the Creation Club content or resource packs)
            const std::string stem = lower(it->path().filename().string());
            for (const char* prefix : { "skyrim - ", "update", "dawnguard", "hearthfires", "dragonborn" })
            {
                if (stem.rfind(prefix, 0) == 0)
                {
                    archives.push_back(it->path());
                    break;
                }
            }
        }
        std::sort(archives.begin(), archives.end());
        for (const fs::path& path : archives)
        {
            try
            {
                Bsa::CompressedBSAFile archive;
                archive.open(path);
                for (const auto& entry : archive.getList())
                {
                    const std::string name = lower(std::string(entry.name()));
                    if (name.size() >= extension.size()
                        && name.compare(name.size() - extension.size(), extension.size(), extension) == 0)
                    {
                        auto stream = archive.getFile(&entry);
                        if (!visit(name, readAll(*stream)))
                            return;
                    }
                }
            }
            catch (const std::exception& error)
            {
                line(std::string("archive skipped: ") + error.what());
            }
        }
    }

    class SilentWorld final : public openeden::script::PapyrusWorld
    {
    public:
        bool formClass(openeden::script::FormId, std::string&) const override { return false; }
        void report(std::string_view) override {}
        void log(openeden::script::Severity, std::string_view) override {}
    };

    TEST(SkyrimSeSurveyReal, papyrus)
    {
        const fs::path data = dataFolder();
        if (data.empty() || !partWanted("papyrus"))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder, or the part is off";

        SilentWorld world;
        auto runtime = ScriptPapyrus::createRuntime(world);
        EventSurvey events;
        std::uint64_t files = 0, refused = 0;
        std::map<std::string, std::uint64_t> prefixes;
        std::set<std::string> seenNames; // a loose script that an archive also holds counts once (archives first)
        const auto onScript = [&](const std::string& name, const std::vector<std::uint8_t>& bytes) {
            const std::string base = lower(fs::path(name).filename().string());
            if (!seenNames.insert(base).second)
                return true;
            ++files;
            const auto result = ScriptPapyrus::Pex::parse(bytes, name);
            if (!result.mOk)
            {
                ++refused;
                return true;
            }
            events.addScript(result.mFile);
            {
                // script naming: the text up to the first underscore when it is short (fragment scripts and the like)
                const std::string stem = lower(fs::path(name).stem().string());
                const std::size_t us = stem.find('_');
                ++prefixes[us != std::string::npos && us <= 6 ? stem.substr(0, us + 1) : std::string("(none)")];
            }
            runtime->loadScript(bytes, name);
            return true;
        };
        forEachFile(data, ".pex", onScript);
        // loose scripts of the Data folder
        std::error_code ec;
        if (fs::is_directory(data / "Scripts", ec))
        {
            for (fs::recursive_directory_iterator it(data / "Scripts", ec), end; !ec && it != end; it.increment(ec))
            {
                if (it->is_regular_file(ec) && lower(it->path().extension().string()) == ".pex")
                {
                    std::ifstream in(it->path(), std::ios::binary);
                    onScript(it->path().filename().string(), readAll(in));
                }
            }
        }
        {
            std::vector<std::pair<std::string, std::uint64_t>> rows(prefixes.begin(), prefixes.end());
            std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
            for (std::size_t i = 0; i < rows.size() && i < 12; ++i)
                line("papyrus script name prefix " + rows[i].first + " scripts " + std::to_string(rows[i].second));
        }
        const openeden::script::LinkReport link = runtime->link();
        line("papyrus: script files " + std::to_string(files) + ", refused " + std::to_string(refused) + ", objects "
            + std::to_string(events.mObjects) + ", functions " + std::to_string(events.mFunctions)
            + ", native functions declared " + std::to_string(events.mNativeFunctions));
        line("papyrus: call sites " + std::to_string(link.callSites) + ", resolved to a script function "
            + std::to_string(link.resolvedScript) + ", to a native " + std::to_string(link.resolvedNative)
            + ", class not loaded " + std::to_string(link.missingClass) + ", function missing "
            + std::to_string(link.missingFunction) + ", receiver not known statically "
            + std::to_string(link.dynamicReceiver));

        const auto natives = runtime->nativeCallSites();
        std::uint64_t total = 0;
        std::vector<std::uint64_t> counts;
        std::map<std::string, std::uint64_t> byClass;
        for (const auto& [name, count] : natives)
        {
            total += count;
            counts.push_back(count);
            byClass[name.substr(0, name.find('.'))] += count;
        }
        line("papyrus natives: distinct called " + std::to_string(natives.size()) + ", call sites "
            + std::to_string(total));
        for (const CoveragePoint& p : coverageCurve(counts, standardFractions()))
            line("papyrus natives: coverage " + std::to_string(static_cast<int>(p.mFraction * 100.0 + 0.5))
                + " percent: " + std::to_string(p.mItems) + " natives");
        std::uint64_t running = 0;
        for (std::size_t i = 0; i < natives.size() && i < 200; ++i)
        {
            running += natives[i].second;
            line("papyrus native #" + std::to_string(i + 1) + " " + natives[i].first + " sites "
                + std::to_string(natives[i].second) + " cumulative "
                + std::to_string(total ? 100.0 * static_cast<double>(running) / static_cast<double>(total) : 0.0)
                + " percent");
        }
        std::vector<std::pair<std::string, std::uint64_t>> classes(byClass.begin(), byClass.end());
        std::sort(classes.begin(), classes.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (std::size_t i = 0; i < classes.size() && i < 40; ++i)
            line("papyrus native class " + classes[i].first + " sites " + std::to_string(classes[i].second));

        const auto rows = events.events();
        line("papyrus events: handler names " + std::to_string(rows.size()) + ", objects with handlers "
            + std::to_string(events.mObjectsWithHandlers) + " of " + std::to_string(events.mObjects)
            + ", natively declared events " + std::to_string(events.declaredNativeEventCount()));
        std::uint64_t declaredRows = 0;
        for (const EventRow& r : rows)
            declaredRows += r.mDeclaredNative ? 1 : 0;
        line("papyrus events: handled names that a native class file declares as a function "
            + std::to_string(declaredRows)
            + " (the class files list no events: the set of events the game can send is not in the scripts)");
        std::vector<std::uint64_t> handlerCounts;
        for (const EventRow& r : rows)
            handlerCounts.push_back(r.mHandlers);
        for (const CoveragePoint& p : coverageCurve(handlerCounts, standardFractions()))
            line("papyrus events: coverage of handlers " + std::to_string(static_cast<int>(p.mFraction * 100.0 + 0.5))
                + " percent: " + std::to_string(p.mItems) + " events");
        for (std::size_t i = 0; i < rows.size() && i < 120; ++i)
            line("papyrus event #" + std::to_string(i + 1) + " " + rows[i].mName + " scripts "
                + std::to_string(rows[i].mScripts) + " handlers " + std::to_string(rows[i].mHandlers) + " in-states "
                + std::to_string(rows[i].mInStates) + (rows[i].mDeclaredNative ? " declared-by-a-native-class" : ""));
        EXPECT_GT(files, 0u);
        EXPECT_EQ(refused, 0u);
    }

    TEST(SkyrimSeSurveyReal, nif)
    {
        const fs::path data = dataFolder();
        if (data.empty() || !partWanted("nif"))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder, or the part is off";
        std::uint64_t limit = ~0ull;
        if (const char* env = std::getenv("OPENEDEN_SKYRIMSE_SURVEY_NIF_MAX"); env != nullptr && *env)
            limit = std::strtoull(env, nullptr, 10);

        NifSurvey survey;
        std::uint64_t parsed = 0, parseOk = 0;
        Nif::ReadOptions options;
        options.mAllowedVersions = Nif::VersionSet::Any;
        forEachFile(data, ".nif", [&](const std::string&, const std::vector<std::uint8_t>& bytes) {
            const std::span<const std::byte> view(reinterpret_cast<const std::byte*>(bytes.data()), bytes.size());
            const NifHeader header = readNifHeader(view);
            survey.add(header);
            if (header.mOk)
            {
                ++parsed;
                try
                {
                    VFS::Path::Normalized path("survey.nif");
                    Nif::NIFFile file(path);
                    Nif::Reader reader(file);
                    reader.parse(
                        std::make_unique<std::istringstream>(
                            std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()), std::ios::binary),
                        options);
                    ++parseOk;
                }
                catch (const std::exception& error)
                {
                    survey.noteReaderFailure(error.what());
                }
            }
            return survey.mFiles < limit;
        });
        line("nif: files " + std::to_string(survey.mFiles) + ", header failures "
            + std::to_string(survey.mHeaderFailures) + ", blocks " + std::to_string(survey.mBlocks)
            + ", distinct block types " + std::to_string(survey.mBlocksByType.size()) + ", reader parsed ok "
            + std::to_string(parseOk) + " of " + std::to_string(parsed) + ", reader failures "
            + std::to_string(survey.mReaderFailures));
        for (const auto& [v, n] : survey.mVersions)
            line("nif: version " + v + ": " + std::to_string(n));
        for (const auto& [e, n] : survey.mHeaderErrors)
            line("nif: header error '" + e + "': " + std::to_string(n));
        for (const auto& [k, n] : survey.mReaderFailureKinds)
            line("nif: reader failure '" + k + "': " + std::to_string(n));
        line("nif: unsupported block types (reader refused the file naming them) "
            + std::to_string(survey.mUnsupportedTypes.size()));
        std::vector<std::pair<std::string, std::uint64_t>> rows(
            survey.mBlocksByType.begin(), survey.mBlocksByType.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) { return a.second > b.second; });
        for (const auto& [name, n] : rows)
        {
            line("nif block " + name + " blocks " + std::to_string(n) + " files "
                + std::to_string(survey.mFilesByType[name])
                + (survey.mUnsupportedTypes.count(name) ? " UNSUPPORTED" : ""));
        }
        EXPECT_GT(survey.mFiles, 0u);
    }
}
