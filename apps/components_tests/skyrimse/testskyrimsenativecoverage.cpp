// Task 0321: the native-function coverage survey. Gated: skipped unless OPENEDEN_SKYRIMSE_DATA names a Skyrim SE Data
// folder (OPENEDEN_FO4_DATA optionally names a Fallout 4 Data folder). Prints aggregate rows only (native names,
// counts and script FAMILIES; no script text): one `NATCOV` line per native called by the game's own scripts, with the
// status the pack's registration lists give (through PapyrusHost::registeredNatives, not from memory). The rows are
// turned into docs/research/0057-papyrus-native-coverage.md; OPENEDEN_NATIVE_COVERAGE_OUT also writes them to a file.

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include <components/bsa/ba2gnrlfile.hpp>
#include <components/bsa/compressedbsafile.hpp>
#include <components/script_obscript/derive.hpp>
#include <components/script_obscript/functiontable.hpp>
#include <components/script_papyrus/papyrusformnatives.hpp>
#include <components/script_papyrus/papyrusruntime.hpp>
#include <components/script_papyrus/pexfile.hpp>
#include <game_skyrimse/conditions.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/survey.hpp>

#include "../script_obscript/realdatasupport.hpp"

namespace
{
    namespace fs = std::filesystem;
    using namespace openeden::script;

    std::string lowerText(std::string text)
    {
        for (char& c : text)
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c - 'A' + 'a');
        return text;
    }

    using ScriptMap = std::map<std::string, std::vector<std::uint8_t>>; // lower-case class name -> bytes

    std::vector<std::uint8_t> readAll(std::istream& in)
    {
        return std::vector<std::uint8_t>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    }

    // Scripts of one archive (BSA or general BA2), later ones overwrite earlier ones.
    void takeArchive(const fs::path& path, ScriptMap& out)
    {
        std::ifstream probe(path, std::ios::binary);
        char head[12] = {};
        probe.read(head, sizeof(head));
        const bool isBa2 = std::string(head, 4) == "BTDX";
        if (isBa2 && std::string(head + 8, 4) != "GNRL")
            return;
        if (!isBa2 && std::string(head, 3) != "BSA")
            return;
        auto take = [&](auto& archive) {
            archive.open(path);
            for (const auto& entry : archive.getList())
            {
                std::string name = lowerText(std::string(entry.name()));
                std::replace(name.begin(), name.end(), '\\', '/');
                if (name.size() > 12 && name.ends_with(".pex") && name.starts_with("scripts/")
                    && std::count(name.begin(), name.end(), '/') == 1)
                {
                    auto stream = archive.getFile(&entry);
                    out[name.substr(8, name.size() - 12)] = readAll(*stream);
                }
            }
        };
        try
        {
            if (isBa2)
            {
                Bsa::BA2GNRLFile archive;
                take(archive);
            }
            else
            {
                Bsa::CompressedBSAFile archive;
                take(archive);
            }
        }
        catch (const std::exception& e)
        {
            std::cout << "NATCOV archive skipped " << path.filename().string() << ": " << e.what() << "\n";
        }
    }

    class QuietWorld : public PapyrusWorld
    {
    public:
        bool formClass(FormId, std::string&) const override { return false; }
        void report(std::string_view) override {}
        void log(Severity, std::string_view) override {}
    };

    class QueryWorld final : public QuietWorld, public PapyrusFormQueries
    {
    };

    class NoData final : public game_skyrimse::PapyrusGameData
    {
    public:
        bool readPex(const std::string&, std::vector<std::uint8_t>&) override { return false; }
        std::string formClass(game_skyrimse::FormId) const override { return {}; }
    };

    struct Survey
    {
        std::map<std::string, std::uint64_t> mSites;
        std::map<std::string, std::map<std::string, std::uint64_t>> mCallers;
    };

    Survey surveyOf(const ScriptMap& scripts)
    {
        QuietWorld world;
        auto runtime = ScriptPapyrus::createRuntime(world, UnknownFunctionPolicy::Tolerant);
        for (const auto& [name, bytes] : scripts)
            runtime->loadScript(bytes, name + ".pex");
        Survey survey;
        for (const auto& [key, count] : runtime->nativeCallSites())
            survey.mSites[key] = count;
        survey.mCallers = runtime->nativeCallers();
        return survey;
    }

    // The family of a script: fragments name their quest ("qf_mq101_0001abcd" -> "qf_mq101"), others their prefix up to
    // the first digit or underscore (at least three letters): "dlc1vq00node" -> "dlc", "mq101script" -> "mq".
    std::string family(const std::string& cls)
    {
        if (cls.starts_with("qf_") || cls.starts_with("pf_") || cls.starts_with("sf_") || cls.starts_with("tf_"))
        {
            const std::size_t second = cls.find('_', 3);
            return cls.substr(0, second == std::string::npos ? cls.size() : second);
        }
        if (cls.starts_with("tif__"))
            return "tif";
        std::size_t i = 0;
        while (i < cls.size() && !(cls[i] >= '0' && cls[i] <= '9') && cls[i] != '_')
            ++i;
        return cls.substr(0, std::max<std::size_t>(i, std::min<std::size_t>(cls.size(), 3)));
    }

    std::string topFamilies(const std::map<std::string, std::uint64_t>& callers)
    {
        std::map<std::string, std::uint64_t> byFamily;
        for (const auto& [cls, count] : callers)
            byFamily[family(cls)] += count;
        std::vector<std::pair<std::string, std::uint64_t>> rows(byFamily.begin(), byFamily.end());
        std::sort(rows.begin(), rows.end(), [](const auto& a, const auto& b) {
            return a.second != b.second ? a.second > b.second : a.first < b.first;
        });
        std::string text;
        for (std::size_t i = 0; i < rows.size() && i < 3; ++i)
            text += (i ? "," : "") + rows[i].first + ":" + std::to_string(rows[i].second);
        return text;
    }

    std::ostream* sOut = nullptr;
    void row(const std::string& text)
    {
        std::cout << "NATCOV " << text << "\n";
        if (sOut != nullptr)
            *sOut << text << "\n";
    }
}

TEST(SkyrimNativeCoverage, survey_of_the_natives_the_games_scripts_call)
{
    const char* skyrimEnv = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    if (skyrimEnv == nullptr || !fs::is_directory(skyrimEnv))
        GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA does not name a Skyrim SE Data folder";
    std::ofstream file;
    if (const char* out = std::getenv("OPENEDEN_NATIVE_COVERAGE_OUT"))
    {
        file.open(out);
        sOut = &file;
    }
    const fs::path data = skyrimEnv;

    // Vanilla: the five official masters' archives, later overriding earlier.
    ScriptMap vanilla;
    for (const char* archive :
        { "Skyrim - Misc.bsa", "Update.bsa", "Dawnguard.bsa", "HearthFires.bsa", "Dragonborn.bsa" })
        if (fs::exists(data / archive))
            takeArchive(data / archive, vanilla);
    ASSERT_FALSE(vanilla.empty());
    // Non-vanilla: loose scripts, Creation Club archives (information only).
    ScriptMap extra;
    std::error_code ec;
    for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
    {
        const std::string name = lowerText(it->path().filename().string());
        if (name.starts_with("cc") && name.ends_with(".bsa"))
            takeArchive(it->path(), extra);
    }
    if (fs::is_directory(data / "Scripts", ec))
        for (fs::directory_iterator it(data / "Scripts", ec), end; !ec && it != end; it.increment(ec))
            if (lowerText(it->path().extension().string()) == ".pex")
            {
                std::ifstream in(it->path(), std::ios::binary);
                extra[lowerText(it->path().stem().string())] = readAll(in);
            }

    // The common natives against the real class files: every declared one registers (the global flags are right).
    {
        QueryWorld world;
        auto runtime = ScriptPapyrus::createRuntime(world, UnknownFunctionPolicy::Tolerant);
        for (const auto& [name, bytes] : vanilla)
            runtime->loadScript(bytes, name + ".pex");
        ScriptPapyrus::FormNatives common(world);
        const ScriptPapyrus::PureNativesReport report = common.registerAll(*runtime);
        for (const std::string& failure : report.failures)
            ADD_FAILURE() << "common native refused: " << failure;
        row("COMMON registered " + std::to_string(report.registered) + " not_declared "
            + std::to_string(report.notDeclared) + " failures " + std::to_string(report.failures.size()));
    }

    NoData noData;
    game_skyrimse::PapyrusHost host(noData);
    std::map<std::string, bool> registered; // key -> does nothing on purpose
    for (const auto& [key, noop] : host.registeredNatives())
        registered[key] = noop;

    const Survey skyrim = surveyOf(vanilla);
    std::uint64_t total = 0, done = 0, noop = 0;
    for (const auto& [key, count] : skyrim.mSites)
    {
        total += count;
        const auto found = registered.find(key);
        if (found != registered.end())
            (found->second ? noop : done) += count;
    }
    row("SUMMARY skyrim scripts " + std::to_string(vanilla.size()) + " natives " + std::to_string(skyrim.mSites.size())
        + " sites " + std::to_string(total) + " implemented_sites " + std::to_string(done) + " noop_sites "
        + std::to_string(noop) + " registered " + std::to_string(registered.size()));
    for (const auto& [key, count] : skyrim.mSites)
    {
        const auto found = registered.find(key);
        const std::string status = found == registered.end() ? "missing" : (found->second ? "noop" : "implemented");
        row("SKYRIM " + key + " " + std::to_string(count) + " " + std::to_string(skyrim.mCallers.at(key).size()) + " "
            + status + " " + topFamilies(skyrim.mCallers.at(key)));
    }
    for (const auto& [key, noopFlag] : registered)
        if (!skyrim.mSites.contains(key))
            row("REGISTERED_UNCALLED " + key + (noopFlag ? " noop" : ""));

    // Non-vanilla scripts: what they call that the vanilla scripts never call.
    {
        ScriptMap all = vanilla;
        for (const auto& [name, bytes] : extra)
            all.emplace(name, bytes);
        const Survey withExtra = surveyOf(all);
        std::uint64_t sites = 0, onlySites = 0;
        std::size_t onlyNatives = 0;
        for (const auto& [key, callers] : withExtra.mCallers)
        {
            std::uint64_t fromExtra = 0;
            for (const auto& [cls, count] : callers)
                if (!vanilla.contains(cls) && extra.contains(cls))
                    fromExtra += count;
            sites += fromExtra;
            if (fromExtra != 0 && !skyrim.mSites.contains(key))
            {
                onlySites += fromExtra;
                ++onlyNatives;
                row("NONVANILLA " + key + " " + std::to_string(fromExtra));
            }
        }
        row("SUMMARY nonvanilla scripts " + std::to_string(extra.size()) + " native_sites_in_them "
            + std::to_string(sites) + " of_natives_vanilla_never_calls " + std::to_string(onlySites) + " distinct "
            + std::to_string(onlyNatives));
    }

    const char* fo4Env = std::getenv("OPENEDEN_FO4_DATA");
    if (fo4Env != nullptr && fs::is_directory(fo4Env))
    {
        // This machine's Fallout 4 holds only the base class files (loose, "Scripts/Actor.pex" and so on), not the
        // game's own scripts, so what can be compared is what each class DECLARES as native; a Skyrim native with
        // call sites is "shared" when Fallout 4 declares the same class.function.
        ScriptMap fo4;
        const fs::path misc = fs::path(fo4Env) / "Fallout4 - Misc.ba2";
        if (fs::exists(misc))
            takeArchive(misc, fo4);
        std::error_code ec2;
        const fs::path loose = fs::path(fo4Env) / "Scripts";
        if (fs::is_directory(loose, ec2))
            for (fs::directory_iterator it(loose, ec2), end; !ec2 && it != end; it.increment(ec2))
                if (lowerText(it->path().extension().string()) == ".pex")
                {
                    std::ifstream in(it->path(), std::ios::binary);
                    fo4[lowerText(it->path().stem().string())] = readAll(in);
                }
        auto declared = [](const ScriptMap& scripts) {
            std::set<std::string> natives;
            for (const auto& [name, bytes] : scripts)
            {
                const auto parsed = ScriptPapyrus::Pex::parse(bytes, name + ".pex");
                if (!parsed.mOk)
                    continue;
                const auto& pexFile = parsed.mFile;
                const auto& object = pexFile.mObjects[0];
                for (const auto& state : object.mStates)
                    for (const auto& function : state.mFunctions)
                        if (function.isNative())
                            natives.insert(ScriptPapyrus::Pex::lowered(pexFile.str(object.mName)) + "."
                                + ScriptPapyrus::Pex::lowered(pexFile.str(function.mName)));
            }
            return natives;
        };
        const std::set<std::string> fo4Declared = declared(fo4);
        const std::set<std::string> skyrimDeclared = declared(vanilla);
        std::size_t shared = 0;
        std::uint64_t sharedSites = 0;
        for (const std::string& key : fo4Declared)
        {
            const bool inSkyrim = skyrimDeclared.contains(key);
            const auto sites = skyrim.mSites.find(key);
            const auto reg = registered.find(key);
            row("FO4DECL " + key + " " + (inSkyrim ? "shared" : "fo4only") + " "
                + (reg == registered.end() ? "missing" : (reg->second ? "noop" : "implemented")) + " "
                + std::to_string(sites == skyrim.mSites.end() ? 0 : sites->second));
            if (inSkyrim)
            {
                ++shared;
                sharedSites += sites == skyrim.mSites.end() ? 0 : sites->second;
            }
        }
        row("SUMMARY fo4 class files " + std::to_string(fo4.size()) + " declared_natives "
            + std::to_string(fo4Declared.size()) + " shared_with_skyrim " + std::to_string(shared)
            + " skyrim_sites_of_shared " + std::to_string(sharedSites) + " skyrim_declared "
            + std::to_string(skyrimDeclared.size()));
    }
    else
        row("SUMMARY fo4 not surveyed (OPENEDEN_FO4_DATA unset)");
}

// Task 0321 (cross-game): which Fallout New Vegas ObScript commands share a name with a Skyrim condition function
// (xEdit's table) or a Papyrus native function, and which of those the pack already evaluates or registers. Names and
// counts only. Needs OPENEDEN_FNV_DATA (the Data folder) and OPENEDEN_SKYRIMSE_DATA.
TEST(SkyrimNativeCoverage, new_vegas_commands_against_skyrim_conditions_and_natives)
{
    RealData::Masters masters;
    const char* skyrimEnv = std::getenv("OPENEDEN_SKYRIMSE_DATA");
    if (skyrimEnv == nullptr || !fs::is_directory(skyrimEnv) || !RealData::openMasters(masters))
        GTEST_SKIP() << "OPENEDEN_FNV_DATA and OPENEDEN_SKYRIMSE_DATA are both needed";
    std::ofstream file;
    if (const char* out = std::getenv("OPENEDEN_NATIVE_COVERAGE_OUT"))
    {
        file.open(std::string(out) + ".fnv");
        sOut = &file;
    }
    // Skyrim's declared natives by function name (any class), and what the host registers.
    ScriptMap vanilla;
    for (const char* archive :
        { "Skyrim - Misc.bsa", "Update.bsa", "Dawnguard.bsa", "HearthFires.bsa", "Dragonborn.bsa" })
        if (fs::exists(fs::path(skyrimEnv) / archive))
            takeArchive(fs::path(skyrimEnv) / archive, vanilla);
    const Survey skyrim = surveyOf(vanilla);
    std::map<std::string, std::set<std::string>> nativeByFunction; // function -> classes calling it with sites
    for (const auto& [key, sites] : skyrim.mSites)
        nativeByFunction[key.substr(key.find('.') + 1)].insert(key);
    NoData noData;
    game_skyrimse::PapyrusHost host(noData);
    std::map<std::string, bool> registered;
    for (const auto& [key, noop] : host.registeredNatives())
        registered[key] = noop;
    std::map<std::string, std::uint16_t> skyrimConditionByName; // lower name -> index
    for (std::uint16_t i = 0; i < 1024; ++i)
    {
        const std::string_view name = game_skyrimse::survey::xeditFunctionName(i);
        if (!name.empty())
            skyrimConditionByName.emplace(lowerText(std::string(name)), i);
    }

    const std::vector<RealData::Unit> units = RealData::readUnits(masters);
    RealData::DataWorld world;
    std::set<std::string> variableNames;
    RealData::buildWorld(masters, world, variableNames);
    ScriptObscript::DeriveEnvironment environment;
    for (const auto& [name, id] : world.mForms)
        environment.mForms.insert(name);
    environment.mVariables = variableNames;
    std::vector<ScriptObscript::SourceUnit> views;
    for (const RealData::Unit& unit : units)
        views.push_back({ unit.mSource, unit.mCompiled });
    const ScriptObscript::FunctionTable table = ScriptObscript::deriveFunctionTable(views, environment);

    std::size_t commands = 0, sameIndexAndName = 0, sameNameOtherIndex = 0, indexReused = 0, papyrusSameName = 0,
                evaluated = 0, papyrusImplemented = 0, anyShared = 0;
    std::uint64_t commandUses = 0, sharedUses = 0;
    for (const auto& entry : table.mEntries)
    {
        if (entry.mOpcode < 0x1000)
            continue;
        ++commands;
        commandUses += entry.mOccurrences;
        const std::uint16_t index = static_cast<std::uint16_t>(entry.mOpcode - 0x1000);
        const std::string_view atIndex = game_skyrimse::survey::xeditFunctionName(index);
        const std::string atIndexLower = lowerText(std::string(atIndex));
        std::string relation = "none";
        std::uint16_t skyrimIndex = 0xFFFF;
        if (!atIndex.empty() && atIndexLower == entry.mName)
        {
            relation = "same_index_and_name";
            skyrimIndex = index;
            ++sameIndexAndName;
        }
        else if (const auto byName = skyrimConditionByName.find(entry.mName); byName != skyrimConditionByName.end())
        {
            relation = "same_name_other_index";
            skyrimIndex = byName->second;
            ++sameNameOtherIndex;
        }
        else if (!atIndex.empty())
        {
            relation = "index_reused_by_other_function";
            ++indexReused;
        }
        const bool isEvaluated
            = skyrimIndex != 0xFFFF && game_skyrimse::conditions::findFunction(skyrimIndex) != nullptr;
        const auto papyrus = nativeByFunction.find(entry.mName);
        bool papyrusImpl = false;
        if (papyrus != nativeByFunction.end())
            for (const std::string& key : papyrus->second)
                papyrusImpl = papyrusImpl || registered.contains(key);
        evaluated += isEvaluated ? 1 : 0;
        papyrusSameName += papyrus != nativeByFunction.end() ? 1 : 0;
        papyrusImplemented += papyrusImpl ? 1 : 0;
        if (skyrimIndex != 0xFFFF || papyrus != nativeByFunction.end())
        {
            ++anyShared;
            sharedUses += entry.mOccurrences;
            std::string classes;
            if (papyrus != nativeByFunction.end())
                for (const std::string& key : papyrus->second)
                    classes += (classes.empty() ? "" : ",") + key;
            row("FNV " + entry.mName + " uses " + std::to_string(entry.mOccurrences) + " opcode "
                + std::to_string(entry.mOpcode) + " condition " + relation + " skyrim_index "
                + (skyrimIndex == 0xFFFF ? std::string("-") : std::to_string(skyrimIndex)) + " evaluated "
                + (isEvaluated ? "yes" : "no") + " papyrus " + (classes.empty() ? std::string("-") : classes)
                + " papyrus_registered " + (papyrusImpl ? "yes" : "no"));
        }
    }
    row("SUMMARY fnv commands " + std::to_string(commands) + " uses " + std::to_string(commandUses)
        + " share_something " + std::to_string(anyShared) + " their_uses " + std::to_string(sharedUses)
        + " condition_same_index_and_name " + std::to_string(sameIndexAndName) + " condition_same_name_other_index "
        + std::to_string(sameNameOtherIndex) + " index_reused_by_other_function " + std::to_string(indexReused)
        + " condition_evaluated_by_us " + std::to_string(evaluated) + " papyrus_same_function_name "
        + std::to_string(papyrusSameName) + " papyrus_registered " + std::to_string(papyrusImplemented));
}
