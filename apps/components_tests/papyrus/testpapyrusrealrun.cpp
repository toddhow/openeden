// Task 0093 stage c: the gated real-data run of the VM. Skipped, never failed, when the game is absent.
//
// In plain words: with the player's own copy of Skyrim Special Edition or Fallout 4 (read only), this loads every
// compiled script of the game's own archives into the VM, picks a fixed sample of them by a stable hash of the script
// name (200 for Skyrim SE, 100 for Fallout 4), makes an instance of each, runs its OnInit (and OnUpdate when it has
// one) against a stub world, and prints AGGREGATE numbers only: how many finish, how many touch a native that nobody
// registered, how many need a latent call (stage d) and how the rest fail. It runs three set-ups: a recorder for every
// native, nothing registered at all, and a recorder for only the 106 natives the game's own scripts call most (the
// yardstick of task 0093: what a first table-driven set of natives would buy). Nothing from the game is written
// anywhere; native and class names appear as facts, no script text.
//
// Run it with tools/papyrus_check/check.sh, or set OPENEDEN_PAPYRUS_DATA to the game's Data folder; the sample size
// can be changed with OPENEDEN_PAPYRUS_SAMPLE.

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
#include <set>
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

    // Every script inside the archives that sit in the game's Data folder (the game's own scripts), by lower-case file
    // name; the first archive (by path) that holds a name wins.
    std::map<std::string, std::vector<std::uint8_t>> readGameScripts(const fs::path& data)
    {
        std::map<std::string, std::vector<std::uint8_t>> scripts;
        std::vector<fs::path> archives;
        std::error_code ec;
        for (fs::directory_iterator it(data, ec), end; !ec && it != end; it.increment(ec))
        {
            const std::string ext = lowerText(it->path().extension().string());
            if (it->is_regular_file(ec) && (ext == ".bsa" || ext == ".ba2"))
            {
                archives.push_back(it->path());
            }
        }
        std::sort(archives.begin(), archives.end());
        for (const fs::path& path : archives)
        {
            try
            {
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
                    continue;
                }
                auto take = [&](auto& archive) {
                    archive.open(path);
                    for (const auto& entry : archive.getList())
                    {
                        std::string name = lowerText(std::string(entry.name()));
                        std::replace(name.begin(), name.end(), '\\', '/');
                        if (endsWith(name, ".pex") && name.find("scripts/") != std::string::npos
                            && std::count(name.begin(), name.end(), '/') == 1 && !scripts.contains(name))
                        {
                            auto stream = archive.getFile(&entry);
                            scripts.emplace(name, readAll(*stream));
                        }
                    }
                };
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
            catch (const std::exception& error)
            {
                std::cout << "PAPYRUS-RUN archive skipped: " << error.what() << "\n";
            }
        }
        return scripts;
    }

    std::uint64_t stableHash(const std::string& text)
    {
        std::uint64_t hash = 1469598103934665603ull; // FNV-1a
        for (const char c : text)
        {
            hash ^= static_cast<std::uint8_t>(c);
            hash *= 1099511628211ull;
        }
        return hash;
    }

    class QuietWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId, std::string&) const override { return false; }
        void report(std::string_view) override {}
        void log(Severity, std::string_view) override { ++mLines; }
        std::size_t mLines = 0;
    };

    struct Native
    {
        std::string mClass;
        std::string mFunction;
        bool mGlobal = false;
    };

    // The value an event argument of this declared type gets: the zero of the type.
    PapyrusValue zeroArgument(const std::string& lowerType)
    {
        if (lowerType == "int")
        {
            return PapyrusValue::makeInt(0);
        }
        if (lowerType == "float")
        {
            return PapyrusValue::makeFloat(0.0f);
        }
        if (lowerType == "bool")
        {
            return PapyrusValue::makeBool(false);
        }
        if (lowerType == "string")
        {
            return PapyrusValue::makeString("");
        }
        return PapyrusValue::makeNone();
    }

    struct Sampled
    {
        std::string mFile;
        bool mHasInit = false;
        bool mHasUpdate = false;
        std::vector<std::string> mUpdateParamTypes;
        /// Every non-global function whose name starts with "On" (the events), in file order, with its parameter types.
        std::vector<std::pair<std::string, std::vector<std::string>>> mEvents;
    };

    struct Outcome
    {
        std::size_t mRun = 0;
        std::size_t mComplete = 0;
        std::size_t mCompleteWithNative = 0;
        std::size_t mUnregistered = 0; ///< Reached at least one native that nobody registered.
        std::size_t mLatent = 0;
        std::size_t mNoneReceiver = 0;
        std::size_t mMissingClass = 0;
        std::size_t mInitDefined = 0;
        std::size_t mCouldNotAttach = 0;
        std::uint64_t mInstructions = 0;
        std::map<std::string, std::size_t> mFailures;
    };

    std::string errorTag(const std::string& message)
    {
        const std::size_t open = message.find('[');
        const std::size_t close = message.find(']', open == std::string::npos ? 0 : open);
        return open != std::string::npos && close != std::string::npos ? message.substr(open + 1, close - open - 1)
                                                                       : std::string("unknown");
    }

    Outcome runSample(const std::map<std::string, std::vector<std::uint8_t>>& scripts,
        const std::vector<Native>& natives, const std::set<std::string>& registered, bool registerAll,
        const std::vector<Sampled>& sample, bool allEvents)
    {
        QuietWorld world;
        auto runtime = ScriptPapyrus::createRuntime(world, UnknownFunctionPolicy::Tolerant);
        for (const auto& [name, bytes] : scripts)
        {
            runtime->loadScript(bytes, name);
        }
        std::uint64_t recorded = 0;
        static const std::set<std::string> latent = { "utility.wait", "utility.waitmenumode", "utility.waitgametime" };
        for (const Native& native : natives)
        {
            const std::string key = lowerText(native.mClass) + "." + lowerText(native.mFunction);
            if (!registerAll && !registered.contains(key))
            {
                continue;
            }
            NativeDescriptor descriptor;
            descriptor.className = native.mClass;
            descriptor.functionName = native.mFunction;
            descriptor.global = native.mGlobal;
            descriptor.latent = latent.contains(key);
            ErrorReport error;
            runtime->registerNative(
                descriptor,
                [&recorded](const NativeCall&) {
                    ++recorded;
                    return NativeResult{};
                },
                error);
        }

        Outcome outcome;
        std::uint32_t form = 0x1000;
        for (const Sampled& item : sample)
        {
            const std::string className
                = item.mFile.substr(item.mFile.find('/') + 1, item.mFile.size() - item.mFile.find('/') - 5);
            AttachKey key;
            key.formId = ++form;
            const ScriptPapyrus::Stats before = runtime->stats();
            const std::uint64_t recordedBefore = recorded;
            const InstanceId id = runtime->attach(key, className);
            if (id == 0)
            {
                ++outcome.mCouldNotAttach;
                continue;
            }
            ++outcome.mRun;
            if (item.mHasInit)
            {
                ++outcome.mInitDefined;
            }
            std::vector<PapyrusValue> arguments;
            if (allEvents)
            {
                std::size_t sent = 0;
                for (const auto& [eventName, types] : item.mEvents)
                {
                    if (eventName == "OnInit" || sent >= 8)
                    {
                        continue;
                    }
                    arguments.clear();
                    for (const std::string& type : types)
                    {
                        arguments.push_back(zeroArgument(type));
                    }
                    runtime->send(id, eventName, arguments);
                    ++sent;
                }
            }
            else if (item.mHasUpdate)
            {
                for (const std::string& type : item.mUpdateParamTypes)
                {
                    arguments.push_back(zeroArgument(type));
                }
                runtime->send(id, "OnUpdate", arguments);
            }
            const TickResult tick = runtime->tick(0.0);
            outcome.mInstructions += tick.instructionsExecuted;
            const ScriptPapyrus::Stats& after = runtime->stats();
            if (tick.errors.empty())
            {
                ++outcome.mComplete;
                if (recorded > recordedBefore)
                {
                    ++outcome.mCompleteWithNative;
                }
            }
            else
            {
                ++outcome.mFailures[errorTag(tick.errors[0].message)];
                if (std::getenv("OPENEDEN_PAPYRUS_VERBOSE") != nullptr)
                {
                    std::cout << "PAPYRUS-RUN failure in " << item.mFile << ": " << tick.errors[0].message << "\n";
                }
            }
            if (after.unregisteredNativeCalls > before.unregisteredNativeCalls)
            {
                ++outcome.mUnregistered;
            }
            if (after.latentSkipped > before.latentSkipped)
            {
                ++outcome.mLatent;
            }
            if (after.noneReceiverCalls > before.noneReceiverCalls)
            {
                ++outcome.mNoneReceiver;
            }
            if (after.missingClassCalls > before.missingClassCalls)
            {
                ++outcome.mMissingClass;
            }
        }
        return outcome;
    }

    void print(const char* label, const Outcome& o)
    {
        std::cout << "PAPYRUS-RUN " << label << ": attached " << o.mRun << " (could not attach " << o.mCouldNotAttach
                  << "), define OnInit " << o.mInitDefined << ", ran to the end " << o.mComplete
                  << ", of those with a native call " << o.mCompleteWithNative << ", reached an unregistered native "
                  << o.mUnregistered << ", reached a latent native " << o.mLatent << ", called on None "
                  << o.mNoneReceiver << ", called a class that is not loaded " << o.mMissingClass << ", instructions "
                  << o.mInstructions << "\n";
        for (const auto& [why, n] : o.mFailures)
        {
            std::cout << "PAPYRUS-RUN " << label << ": failed with [" << why << "]: " << n << "\n";
        }
    }

    TEST(PapyrusRealRun, a_sample_of_the_games_own_scripts_runs_on_the_vm)
    {
        const char* dataName = std::getenv("OPENEDEN_PAPYRUS_DATA");
        if (dataName == nullptr || !fs::is_directory(dataName))
        {
            GTEST_SKIP() << "OPENEDEN_PAPYRUS_DATA does not name a Skyrim SE or Fallout 4 Data folder";
        }
        const auto scripts = readGameScripts(dataName);
        ASSERT_FALSE(scripts.empty()) << "no script found in the archives of the Data folder";

        // What the files declare: the natives, and for the sample the events they define.
        std::vector<Native> natives;
        std::map<std::string, Sampled> facts;
        bool fallout4 = false;
        for (const auto& [name, bytes] : scripts)
        {
            const Pex::ParseResult parsed = Pex::parse(bytes, name);
            if (!parsed.mOk)
            {
                continue;
            }
            const Pex::File& file = parsed.mFile;
            fallout4 = file.mGame == Pex::Game::Fallout4;
            const Pex::Object& object = file.mObjects[0];
            Sampled facts1;
            facts1.mFile = name;
            for (const Pex::State& state : object.mStates)
            {
                for (const Pex::Function& function : state.mFunctions)
                {
                    const std::string fn = Pex::lowered(file.str(function.mName));
                    if (function.isNative())
                    {
                        natives.push_back({ std::string(file.str(object.mName)), std::string(file.str(function.mName)),
                            function.isGlobal() });
                    }
                    if (fn == "oninit")
                    {
                        facts1.mHasInit = true;
                    }
                    if (!function.isGlobal() && fn.size() > 2 && fn[0] == 'o' && fn[1] == 'n')
                    {
                        std::vector<std::string> types;
                        for (const Pex::Parameter& p : function.mParameters)
                        {
                            types.push_back(Pex::lowered(file.str(p.mType)));
                        }
                        facts1.mEvents.emplace_back(std::string(file.str(function.mName)), std::move(types));
                    }
                    if (fn == "onupdate")
                    {
                        facts1.mHasUpdate = true;
                        for (const Pex::Parameter& p : function.mParameters)
                        {
                            facts1.mUpdateParamTypes.push_back(Pex::lowered(file.str(p.mType)));
                        }
                    }
                }
            }
            for (const Pex::Property& property : object.mProperties)
            {
                if ((property.mFlags & Pex::Property::kAuto) != 0)
                {
                    continue;
                }
                const std::string base(file.str(property.mName));
                if ((property.mFlags & Pex::Property::kRead) != 0 && property.mGetter.isNative())
                {
                    natives.push_back(
                        { std::string(file.str(object.mName)), base + ".get", property.mGetter.isGlobal() });
                }
                if ((property.mFlags & Pex::Property::kWrite) != 0 && property.mSetter.isNative())
                {
                    natives.push_back(
                        { std::string(file.str(object.mName)), base + ".set", property.mSetter.isGlobal() });
                }
            }
            facts.emplace(name, std::move(facts1));
        }

        std::size_t wanted = fallout4 ? 100 : 200;
        if (const char* override_ = std::getenv("OPENEDEN_PAPYRUS_SAMPLE"); override_ != nullptr)
        {
            wanted = static_cast<std::size_t>(std::strtoull(override_, nullptr, 10));
        }
        std::vector<std::pair<std::uint64_t, std::string>> order;
        for (const auto& [name, item] : facts)
        {
            order.emplace_back(stableHash(name), name);
        }
        std::sort(order.begin(), order.end());
        std::vector<Sampled> sample; // the first sample: by hash of the name alone
        std::vector<Sampled> eventful; // the second: only scripts that define an event besides OnInit
        for (const auto& [hash, name] : order)
        {
            if (sample.size() < wanted)
            {
                sample.push_back(facts[name]);
            }
            const Sampled& item = facts[name];
            const bool hasOther = std::any_of(
                item.mEvents.begin(), item.mEvents.end(), [](const auto& event) { return event.first != "OnInit"; });
            if (hasOther && eventful.size() < wanted)
            {
                eventful.push_back(item);
            }
        }
        std::cout << "PAPYRUS-RUN game " << (fallout4 ? "Fallout 4" : "Skyrim SE") << ": game scripts "
                  << scripts.size() << ", natives declared " << natives.size() << ", sample " << sample.size()
                  << ", eventful sample " << eventful.size() << "\n";

        // 1. A recorder for every native (the stub world).
        const Outcome all = runSample(scripts, natives, {}, true, sample, false);
        print("sample A, recorder for every native", all);
        const Outcome allEvents = runSample(scripts, natives, {}, true, eventful, true);
        print("sample B (events), recorder for every native", allEvents);

        // 2. Nothing registered: tolerant mode alone.
        const Outcome none = runSample(scripts, natives, {}, false, sample, false);
        print("sample A, nothing registered", none);
        const Outcome noneEvents = runSample(scripts, natives, {}, false, eventful, true);
        print("sample B (events), nothing registered", noneEvents);

        // 3. The yardstick: only the 106 natives the game's own scripts call most.
        std::set<std::string> top;
        std::uint64_t topCalls = 0;
        std::uint64_t allCalls = 0;
        {
            QuietWorld world;
            auto runtime = ScriptPapyrus::createRuntime(world);
            for (const auto& [name, bytes] : scripts)
            {
                runtime->loadScript(bytes, name);
            }
            const auto sites = runtime->nativeCallSites();
            for (const auto& [key, count] : sites)
            {
                allCalls += count;
            }
            for (std::size_t i = 0; i < sites.size() && i < 106; ++i)
            {
                top.insert(sites[i].first);
                topCalls += sites[i].second;
            }
            std::cout << "PAPYRUS-RUN yardstick: native call sites resolved by link() " << allCalls << " to "
                      << sites.size() << " distinct natives; the top " << top.size() << " cover " << topCalls << " ("
                      << (allCalls == 0 ? 0.0 : 100.0 * static_cast<double>(topCalls) / static_cast<double>(allCalls))
                      << " percent)\n";
        }
        const Outcome yardstick = runSample(scripts, natives, top, false, sample, false);
        print("sample A, recorder for the 106 most called natives", yardstick);
        const Outcome yardstickEvents = runSample(scripts, natives, top, false, eventful, true);
        print("sample B (events), recorder for the 106 most called natives", yardstickEvents);

        EXPECT_GT(all.mRun, 0u);
        EXPECT_GT(all.mComplete, 0u);
    }
}
