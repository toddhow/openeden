// Task 0093 stage b: loading scripts into the runtime (PapyrusContract::loadScript) and link().
//
// In plain words: the runtime keeps every loaded script by its object name. These tests check what it hands back for
// a good file (an id and the facts of the file), what it does for a deviant size convention (accepts it with one log
// line in tolerant mode, refuses it in strict mode), that a second script of the same name is refused, that a failed
// file leaves no trace, and that link() sorts every call in the loaded scripts into: a script function, a native, a
// missing class, a missing function, or a receiver whose class cannot be known.

#include <gtest/gtest.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <components/script_papyrus/papyruslimits.hpp>
#include <components/script_papyrus/papyrusruntime.hpp>

#include "papyrustestutil.hpp"

namespace
{
    using namespace PapyrusTest;
    using namespace PexAsm;
    using namespace openeden::script;

    class TestWorld final : public PapyrusWorld
    {
    public:
        bool formClass(FormId, std::string&) const override { return false; }
        void report(std::string_view message) override { mReports.emplace_back(message); }
        void log(Severity, std::string_view message) override { mLogs.emplace_back(message); }

        std::vector<std::string> mReports;
        std::vector<std::string> mLogs;
    };

    struct Fixture
    {
        TestWorld mWorld;
        std::unique_ptr<PapyrusContract> mRuntime
            = ScriptPapyrus::createRuntime(mWorld, UnknownFunctionPolicy::Tolerant);

        LoadResult load(const Bytes& bytes, std::string_view name = "test.pex") const
        {
            return mRuntime->loadScript(bytes, name);
        }
    };

    TEST(PapyrusLoad, a_good_file_gives_an_id_and_its_facts)
    {
        for (const Target target : { Target::SkyrimSE, Target::Fallout4 })
        {
            Fixture fixture;
            const LoadResult result = fixture.load(richScript(target).build(optionsFor(target)).mBytes);
            ASSERT_TRUE(result.ok) << (result.reports.empty() ? "" : result.reports[0].format());
            EXPECT_NE(result.script, 0u);
            EXPECT_EQ(result.info.className, "RichQuest");
            EXPECT_EQ(result.info.parent, "Quest");
            EXPECT_EQ(result.info.major, 3);
            EXPECT_EQ(result.info.minor, target == Target::Fallout4 ? 9 : 2);
            EXPECT_EQ(result.info.gameId, target == Target::Fallout4 ? 2 : 1);
            EXPECT_EQ(result.info.bigEndian, target == Target::SkyrimSE);
            EXPECT_EQ(result.info.sizeConvention, PexSizeConvention::IncludesSelf);
            EXPECT_TRUE(result.reports.empty());
            EXPECT_TRUE(fixture.mWorld.mLogs.empty());
        }
    }

    TEST(PapyrusLoad, each_script_gets_its_own_id)
    {
        Fixture fixture;
        const LoadResult a = fixture.load(ScriptBuilder("One").build().mBytes);
        const LoadResult b = fixture.load(ScriptBuilder("Two").build().mBytes);
        ASSERT_TRUE(a.ok);
        ASSERT_TRUE(b.ok);
        EXPECT_NE(a.script, b.script);
    }

    TEST(PapyrusLoad, a_deviant_size_is_accepted_in_tolerant_mode_with_one_log_line_naming_it)
    {
        for (const SizeConv conv : { SizeConv::ExcludesSelf, SizeConv::ExcludesSelfOtherOrder })
        {
            Fixture fixture;
            Options options;
            options.mSizeConv = conv;
            const LoadResult result = fixture.load(ScriptBuilder("Deviant").build(options).mBytes, "deviant.pex");
            ASSERT_TRUE(result.ok);
            EXPECT_EQ(result.info.sizeConvention,
                conv == SizeConv::ExcludesSelf ? PexSizeConvention::ExcludesSelf
                                               : PexSizeConvention::ExcludesSelfOtherOrder);
            ASSERT_EQ(fixture.mWorld.mLogs.size(), 1u);
            EXPECT_TRUE(contains(fixture.mWorld.mLogs[0], "deviant.pex")) << fixture.mWorld.mLogs[0];
            EXPECT_TRUE(contains(fixture.mWorld.mLogs[0], "size")) << fixture.mWorld.mLogs[0];
            ASSERT_EQ(result.reports.size(), 1u);
            EXPECT_EQ(result.reports[0].severity, Severity::Warning);
        }
    }

    TEST(PapyrusLoad, strict_mode_refuses_a_deviant_size)
    {
        Fixture fixture;
        fixture.mRuntime->setPolicy(UnknownFunctionPolicy::Strict);
        Options options;
        options.mSizeConv = SizeConv::ExcludesSelf;
        const LoadResult refused = fixture.load(ScriptBuilder("Deviant").build(options).mBytes);
        EXPECT_FALSE(refused.ok);
        EXPECT_FALSE(refused.reports.empty());
        // The usual convention is still fine, and the refusal left no trace of the name.
        EXPECT_TRUE(fixture.load(ScriptBuilder("Deviant").build().mBytes).ok);
    }

    TEST(PapyrusLoad, a_failed_file_changes_nothing_and_the_next_file_loads)
    {
        Fixture fixture;
        Bytes broken = ScriptBuilder("Broken").build().mBytes;
        broken.resize(broken.size() / 2);
        const LoadResult bad = fixture.load(broken, "broken.pex");
        EXPECT_FALSE(bad.ok);
        EXPECT_EQ(bad.script, 0u);
        ASSERT_FALSE(bad.reports.empty());
        EXPECT_EQ(bad.reports[0].script, "broken.pex");
        EXPECT_TRUE(fixture.load(ScriptBuilder("Broken").build().mBytes).ok);
        EXPECT_EQ(fixture.mRuntime->link().callSites, 0u);
    }

    TEST(PapyrusLoad, a_second_script_with_the_same_name_is_refused_without_regard_to_case)
    {
        Fixture fixture;
        ASSERT_TRUE(fixture.load(ScriptBuilder("Twin").build().mBytes, "a.pex").ok);
        const LoadResult second = fixture.load(ScriptBuilder("TWIN").build().mBytes, "b.pex");
        EXPECT_FALSE(second.ok);
        ASSERT_FALSE(second.reports.empty());
        EXPECT_TRUE(contains(second.reports[0].message, "already loaded")) << second.reports[0].message;
        EXPECT_EQ(second.reports[0].script, "b.pex");
    }

    TEST(PapyrusLoad, hostile_input_gives_a_report_and_never_throws)
    {
        Fixture fixture;
        EXPECT_FALSE(fixture.load({}).ok);
        EXPECT_FALSE(fixture.load(Bytes(100, 0xFF)).ok);
        EXPECT_FALSE(fixture.load(Bytes(ScriptPapyrus::Limits::kMaxFileBytes + 1, 0)).ok);
        // The name is only used in reports, however long it is.
        EXPECT_TRUE(fixture.load(ScriptBuilder("X").build().mBytes, std::string(100000, 'n')).ok);
    }

    TEST(PapyrusLoad, the_number_of_loaded_scripts_is_capped)
    {
        Fixture fixture;
        std::size_t loaded = 0;
        for (std::size_t i = 0; i < ScriptPapyrus::Limits::kMaxScripts + 1; ++i)
        {
            const LoadResult result = fixture.load(ScriptBuilder("S" + std::to_string(i)).build().mBytes);
            if (result.ok)
            {
                ++loaded;
            }
            else
            {
                EXPECT_EQ(i, ScriptPapyrus::Limits::kMaxScripts);
                ASSERT_FALSE(result.reports.empty());
                EXPECT_TRUE(contains(result.reports[0].message, "cap")) << result.reports[0].message;
            }
        }
        EXPECT_EQ(loaded, ScriptPapyrus::Limits::kMaxScripts);
    }

    // ---------------------------------------------------------------- link

    TEST(PapyrusLink, an_empty_runtime_has_nothing_to_report)
    {
        Fixture fixture;
        const LinkReport report = fixture.mRuntime->link();
        EXPECT_EQ(report.callSites, 0u);
        EXPECT_TRUE(report.unresolved.empty());
    }

    void loadLibrary(Fixture& fixture)
    {
        ScriptBuilder utility("Utility");
        utility.function("Wait").global().native().param("afSeconds", "Float");
        ScriptBuilder base("Base");
        base.function("Hello");
        base.function("NativeHi").native();
        base.function("Twice", "Int").param("a", "Int").ret(Id("a"));
        ScriptBuilder derived("Derived");
        derived.parent("Base");
        derived.function("Hello").local("::r", "None").callParent("Hello", Id("::r")).ret(Nil());
        for (const ScriptBuilder* script : { &utility, &base, &derived })
        {
            ASSERT_TRUE(fixture.load(script->build().mBytes).ok);
        }
    }

    TEST(PapyrusLink, sorts_every_call_by_what_it_resolves_to)
    {
        Fixture fixture;
        loadLibrary(fixture);
        ScriptBuilder caller("Caller");
        caller.parent("Derived");
        caller.var("myBase", "base");
        caller.function("F")
            .local("b", "Base")
            .local("d", "Derived")
            .local("u", "Unknown")
            .local("i", "Int")
            .local("::r", "Int")
            // resolved script functions: direct, through the parent chain, through self, an object variable, and
            // with a receiver type written in another case
            .callMethod("Hello", Id("b"), Id("::r"))
            .callMethod("Twice", Id("d"), Id("::r"), { Int(1) })
            .callMethod("Hello", Id("self"), Id("::r"))
            .callMethod("Hello", Id("myBase"), Id("::r"))
            // a native declared by a loaded class
            .callMethod("NativeHi", Id("b"), Id("::r"))
            .callStatic("Utility", "Wait", Id("::r"), { Flt(1.0f) })
            // the class is loaded and has no such function
            .callMethod("Missing", Id("b"), Id("::r"))
            .callStatic("Utility", "Nope", Id("::r"))
            // the class is not loaded
            .callMethod("Any", Id("u"), Id("::r"))
            .callStatic("Nope", "X", Id("::r"))
            // a receiver that cannot have a class
            .callMethod("Any", Id("i"), Id("::r"))
            // callparent resolves in the parent chain
            .callParent("Hello", Id("::r"))
            .ret(Nil());
        ASSERT_TRUE(fixture.load(caller.build().mBytes).ok);

        const LinkReport report = fixture.mRuntime->link();
        // Caller.F has 12 calls and Derived.Hello has one callparent.
        EXPECT_EQ(report.callSites, 13u);
        EXPECT_EQ(report.resolvedScript, 6u); // Hello x3 (b, self, myBase), Twice, Caller's callparent, Derived's
        EXPECT_EQ(report.resolvedNative, 2u);
        EXPECT_EQ(report.missingFunction, 2u);
        EXPECT_EQ(report.missingClass, 2u);
        EXPECT_EQ(report.dynamicReceiver, 1u);
        EXPECT_EQ(report.resolvedScript + report.resolvedNative + report.missingFunction + report.missingClass
                + report.dynamicReceiver,
            report.callSites);
        // The missing ones are listed, located and named.
        EXPECT_EQ(report.unresolved.size(), 5u);
        EXPECT_EQ(report.unresolvedOmitted, 0u);
        std::string all;
        for (const ErrorReport& entry : report.unresolved)
        {
            all += entry.format() + "\n";
        }
        EXPECT_TRUE(contains(all, "Caller.F@")) << all;
        EXPECT_TRUE(contains(all, "Missing")) << all;
        EXPECT_TRUE(contains(all, "Nope")) << all;
        EXPECT_TRUE(contains(all, "Unknown")) << all;
    }

    TEST(PapyrusLink, link_does_not_depend_on_the_order_the_scripts_were_loaded)
    {
        ScriptBuilder caller("Caller");
        caller.function("F").local("b", "Base").local("::r", "Int").callMethod("Hello", Id("b"), Id("::r")).ret(Nil());
        ScriptBuilder base("Base");
        base.function("Hello");
        Fixture first;
        ASSERT_TRUE(first.load(caller.build().mBytes).ok);
        ASSERT_TRUE(first.load(base.build().mBytes).ok);
        Fixture second;
        ASSERT_TRUE(second.load(base.build().mBytes).ok);
        ASSERT_TRUE(second.load(caller.build().mBytes).ok);
        EXPECT_EQ(first.mRuntime->link().resolvedScript, 1u);
        EXPECT_EQ(second.mRuntime->link().resolvedScript, 1u);
    }

    TEST(PapyrusLink, the_list_of_missing_calls_is_capped_and_counts_the_rest)
    {
        Fixture fixture;
        ScriptBuilder caller("Many");
        caller.function("F").local("::r", "Int");
        const std::size_t total = ScriptPapyrus::Limits::kMaxLinkReports + 40;
        for (std::size_t i = 0; i < total; ++i)
        {
            caller.callStatic("Nowhere", "F" + std::to_string(i), Id("::r"));
        }
        caller.ret(Nil());
        ASSERT_TRUE(fixture.load(caller.build().mBytes).ok);
        const LinkReport report = fixture.mRuntime->link();
        EXPECT_EQ(report.missingClass, total);
        EXPECT_EQ(report.unresolved.size(), ScriptPapyrus::Limits::kMaxLinkReports);
        EXPECT_EQ(report.unresolvedOmitted, total - ScriptPapyrus::Limits::kMaxLinkReports);
    }

    TEST(PapyrusLink, is_deterministic)
    {
        Fixture a;
        Fixture b;
        loadLibrary(a);
        loadLibrary(b);
        const LinkReport x = a.mRuntime->link();
        const LinkReport y = b.mRuntime->link();
        EXPECT_EQ(x.callSites, y.callSites);
        EXPECT_EQ(x.resolvedScript, y.resolvedScript);
        EXPECT_EQ(x.unresolved.size(), y.unresolved.size());
    }
}
