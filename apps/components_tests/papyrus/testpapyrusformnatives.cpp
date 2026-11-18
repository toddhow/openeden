// Task 0321: the natives that Skyrim SE and Fallout 4 share (components/script_papyrus/papyrusformnatives.*): the
// query natives answered through the optional PapyrusFormQueries capability, and the cosmetic ones that do nothing on
// purpose. Written from the header before the implementation and run red against a stub first. Every script is
// generated; no game asset anywhere.

#include <gtest/gtest.h>

#include <array>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include <components/script_papyrus/papyrusformnatives.hpp>
#include <components/script_papyrus/papyrusruntime.hpp>

#include "papyrusvmutil.hpp"

namespace
{
    using namespace PapyrusVmTest;

    /// A world that answers the queries from small tables. `withQueries` false hides the capability.
    class QueryWorld : public PapyrusWorld
    {
    public:
        bool formClass(FormId form, std::string& className) const override
        {
            const auto found = mClasses.find(form);
            if (found == mClasses.end())
                return false;
            className = found->second;
            return true;
        }
        void report(std::string_view) override {}
        void log(Severity, std::string_view message) override { mLogs.emplace_back(message); }

        std::map<FormId, std::string> mClasses;
        std::vector<std::string> mLogs;
    };

    class AnsweringWorld final : public QueryWorld, public PapyrusFormQueries
    {
    public:
        FormId formIdOf(const PapyrusValue& value) const override
        {
            if (value.kind != PapyrusKind::Object)
                return 0;
            if (!value.attached)
                return value.handle;
            const auto found = mAttached.find(value.handle);
            return found == mAttached.end() ? 0 : found->second;
        }
        bool actorLevel(FormId actor, std::int32_t& level) const override
        {
            const auto found = mLevels.find(actor);
            if (found == mLevels.end())
                return false;
            level = found->second;
            return true;
        }
        bool raceOf(FormId actor, FormId& race) const override
        {
            const auto found = mRaces.find(actor);
            if (found == mRaces.end())
                return false;
            race = found->second;
            return true;
        }
        bool formListEntries(FormId list, std::vector<FormId>& entries) const override
        {
            const auto found = mLists.find(list);
            if (found == mLists.end())
                return false;
            entries = found->second;
            return true;
        }
        bool referenceRotation(FormId reference, double (&xyz)[3]) const override
        {
            const auto found = mAngles.find(reference);
            if (found == mAngles.end())
                return false;
            for (int i = 0; i < 3; ++i)
                xyz[i] = found->second[i];
            return true;
        }

        std::map<std::uint32_t, FormId> mAttached; ///< instance -> form
        std::map<FormId, std::int32_t> mLevels;
        std::map<FormId, FormId> mRaces;
        std::map<FormId, std::vector<FormId>> mLists;
        std::map<FormId, std::array<double, 3>> mAngles;
    };

    constexpr double kPi = 3.14159265358979323846;
    constexpr FormId kNpc = 0x13BAA;
    constexpr FormId kRace = 0x13741;
    constexpr FormId kList = 0x5001;
    constexpr FormId kRef = 0x6001;

    ScriptBuilder actorClass()
    {
        ScriptBuilder a("Actor");
        a.function("GetLevel", "Int").native();
        a.function("GetRace", "Race").native();
        a.function("SetAlpha").native().param("afTargetAlpha", "Float").param("abFade", "Bool");
        return a;
    }
    ScriptBuilder raceClass() { return ScriptBuilder("Race"); }
    ScriptBuilder formClass() { return ScriptBuilder("Form"); }
    ScriptBuilder actorBaseClass()
    {
        ScriptBuilder a("ActorBase");
        a.function("GetRace", "Race").native();
        return a;
    }
    ScriptBuilder formListClass()
    {
        ScriptBuilder f("FormList");
        f.function("GetSize", "Int").native();
        f.function("GetAt", "Form").native().param("aiIndex", "Int");
        f.function("HasForm", "Bool").native().param("akForm", "Form");
        return f;
    }
    ScriptBuilder referenceClass()
    {
        ScriptBuilder r("ObjectReference");
        for (const char* axis : { "GetAngleX", "GetAngleY", "GetAngleZ" })
            r.function(axis, "Float").native();
        return r;
    }
    ScriptBuilder soundClass()
    {
        ScriptBuilder s("Sound");
        s.function("Play", "Int").native().param("akSource", "ObjectReference");
        s.function("StopInstance").global().native().param("aiPlaybackInstanceID", "Int");
        return s;
    }

    struct Rig
    {
        explicit Rig(PapyrusWorld& world, Target target = Target::SkyrimSE)
            : mTarget(target)
            , mRuntime(ScriptPapyrus::createRuntime(world, UnknownFunctionPolicy::Tolerant))
            , mNatives(world)
        {
            for (const ScriptBuilder& c :
                { actorClass(), raceClass(), formClass(), actorBaseClass(), formListClass(), referenceClass(),
                    soundClass() })
                EXPECT_TRUE(load(c));
            mReport = mNatives.registerAll(*mRuntime);
        }
        bool load(const ScriptBuilder& script)
        {
            const LoadResult result = mRuntime->loadScript(script.build(optionsFor(mTarget)).mBytes, script.name());
            if (!result.ok)
                ADD_FAILURE() << script.name() << ": " << (result.reports.empty() ? "?" : result.reports[0].format());
            return result.ok;
        }
        /// A script "Probe" holding a wrapper per native; self is a plain instance, the arguments name forms.
        InstanceId probe()
        {
            ScriptBuilder p("Probe");
            p.function("Level", "Int")
                .param("a", "Actor")
                .local("r", "Int")
                .callMethod("GetLevel", Id("a"), Id("r"))
                .ret(Id("r"));
            p.function("RaceOf", "Race")
                .param("a", "Actor")
                .local("r", "Race")
                .callMethod("GetRace", Id("a"), Id("r"))
                .ret(Id("r"));
            p.function("BaseRace", "Race")
                .param("a", "ActorBase")
                .local("r", "Race")
                .callMethod("GetRace", Id("a"), Id("r"))
                .ret(Id("r"));
            p.function("Size", "Int")
                .param("l", "FormList")
                .local("r", "Int")
                .callMethod("GetSize", Id("l"), Id("r"))
                .ret(Id("r"));
            p.function("At", "Form")
                .param("l", "FormList")
                .param("i", "Int")
                .local("r", "Form")
                .callMethod("GetAt", Id("l"), Id("r"), { Id("i") })
                .ret(Id("r"));
            p.function("Has", "Bool")
                .param("l", "FormList")
                .param("f", "Form")
                .local("r", "Bool")
                .callMethod("HasForm", Id("l"), Id("r"), { Id("f") })
                .ret(Id("r"));
            for (const char* axis : { "GetAngleX", "GetAngleY", "GetAngleZ" })
                p.function(axis, "Float")
                    .param("o", "ObjectReference")
                    .local("r", "Float")
                    .callMethod(axis, Id("o"), Id("r"))
                    .ret(Id("r"));
            p.function("Alpha", "Int")
                .param("a", "Actor")
                .local("n", "None")
                .callMethod("SetAlpha", Id("a"), Id("n"), { Flt(0.5f), Bool(true) })
                .ret(Int(7));
            p.function("PlaySound", "Int")
                .param("s", "Sound")
                .param("o", "ObjectReference")
                .local("r", "Int")
                .callMethod("Play", Id("s"), Id("r"), { Id("o") })
                .ret(Id("r"));
            p.function("Stop", "Int")
                .local("n", "None")
                .callStatic("Sound", "StopInstance", Id("n"), { Int(5) })
                .ret(Int(9));
            EXPECT_TRUE(load(p));
            ScriptBuilder npc("Npc");
            npc.parent("Actor");
            npc.function("SelfLevel", "Int").local("r", "Int").callMethod("GetLevel", Id("self"), Id("r")).ret(Id("r"));
            EXPECT_TRUE(load(npc));
            AttachKey key;
            key.kind = AttachKind::Form;
            key.formId = 0x77777;
            const InstanceId id = mRuntime->attach(key, "Probe");
            EXPECT_NE(id, 0u);
            mProbeForm = key.formId;
            return id;
        }
        PapyrusValue run(InstanceId id, std::string_view fn, std::vector<PapyrusValue> args = {})
        {
            const CallResult result = mRuntime->call(id, fn, args);
            EXPECT_TRUE(result.ok) << fn << ": " << result.error.format();
            return result.returned;
        }
        Target mTarget;
        std::unique_ptr<ScriptPapyrus::PapyrusRuntime> mRuntime;
        ScriptPapyrus::FormNatives mNatives;
        ScriptPapyrus::PureNativesReport mReport;
        FormId mProbeForm = 0;
    };

    PapyrusValue form(const char* cls, FormId id)
    {
        return PapyrusValue::makeForm(cls, id);
    }

    TEST(PapyrusFormNatives, the_sets_have_their_names_sorted_in_lower_case)
    {
        const auto queries = ScriptPapyrus::FormNatives::queryNames();
        const auto cosmetic = ScriptPapyrus::FormNatives::cosmeticNames();
        EXPECT_EQ(queries.size(), 9u);
        EXPECT_GE(cosmetic.size(), 10u);
        for (const auto set : { queries, cosmetic })
            for (std::size_t i = 0; i < set.size(); ++i)
            {
                EXPECT_NE(set[i].find('.'), std::string_view::npos);
                for (const char c : set[i])
                    EXPECT_FALSE(c >= 'A' && c <= 'Z') << set[i];
                if (i > 0)
                    EXPECT_LT(set[i - 1], set[i]);
            }
    }

    TEST(PapyrusFormNatives, a_level_is_read_from_the_world)
    {
        for (const Target target : kBothTargets)
        {
            AnsweringWorld world;
            world.mLevels[kNpc] = 23;
            Rig rig(world, target);
            EXPECT_TRUE(rig.mReport.ok());
            const InstanceId id = rig.probe();
            const PapyrusValue level = rig.run(id, "Level", { form("Actor", kNpc) });
            EXPECT_EQ(level.kind, PapyrusKind::Int);
            EXPECT_EQ(level.integer, 23);
            EXPECT_EQ(rig.run(id, "Level", { form("Actor", 0x999) }).integer, 0) << "an actor the world does not know";
            EXPECT_EQ(rig.mNatives.unanswered(), 1u);
        }
    }

    TEST(PapyrusFormNatives, the_receiver_may_be_an_attached_script)
    {
        AnsweringWorld world;
        Rig rig(world);
        (void)rig.probe();
        AttachKey key;
        key.kind = AttachKind::Form;
        key.formId = kNpc;
        const InstanceId npc = rig.mRuntime->attach(key, "Npc"); // a script on an actor: self is the instance
        ASSERT_NE(npc, 0u);
        world.mAttached[npc] = kNpc;
        world.mLevels[kNpc] = 11;
        EXPECT_EQ(rig.run(npc, "SelfLevel").integer, 11);
    }

    TEST(PapyrusFormNatives, a_race_is_a_race_form_of_the_world_s_class)
    {
        AnsweringWorld world;
        world.mRaces[kNpc] = kRace;
        world.mClasses[kRace] = "Race";
        Rig rig(world);
        const InstanceId id = rig.probe();
        for (const char* fn : { "RaceOf", "BaseRace" })
        {
            const PapyrusValue race = rig.run(id, fn, { form(fn[0] == 'R' ? "Actor" : "ActorBase", kNpc) });
            EXPECT_EQ(race.kind, PapyrusKind::Object) << fn;
            EXPECT_EQ(race.handle, kRace) << fn;
            EXPECT_EQ(race.text, "Race") << fn;
        }
        EXPECT_EQ(rig.run(id, "RaceOf", { form("Actor", 0x999) }).kind, PapyrusKind::None);
    }

    TEST(PapyrusFormNatives, a_form_list_has_a_size_members_by_index_and_membership)
    {
        for (const Target target : kBothTargets)
        {
            AnsweringWorld world;
            world.mLists[kList] = { 0x10, 0x20, 0x30 };
            world.mClasses[0x20] = "Keyword";
            Rig rig(world, target);
            const InstanceId id = rig.probe();
            EXPECT_EQ(rig.run(id, "Size", { form("FormList", kList) }).integer, 3);
            const PapyrusValue second = rig.run(id, "At", { form("FormList", kList), PapyrusValue::makeInt(1) });
            EXPECT_EQ(second.handle, 0x20u);
            EXPECT_EQ(second.text, "Keyword");
            EXPECT_EQ(rig.run(id, "At", { form("FormList", kList), PapyrusValue::makeInt(0) }).handle, 0x10u);
            // out of range, either side: none
            EXPECT_EQ(rig.run(id, "At", { form("FormList", kList), PapyrusValue::makeInt(3) }).kind, PapyrusKind::None);
            EXPECT_EQ(rig.run(id, "At", { form("FormList", kList), PapyrusValue::makeInt(-1) }).kind,
                PapyrusKind::None);
            EXPECT_TRUE(rig.run(id, "Has", { form("FormList", kList), form("Form", 0x30) }).integer != 0);
            EXPECT_FALSE(rig.run(id, "Has", { form("FormList", kList), form("Form", 0x40) }).integer != 0);
            EXPECT_FALSE(rig.run(id, "Has", { form("FormList", kList), PapyrusValue::makeNone() }).integer != 0)
                << "None is never a member";
            // a list the world does not know is empty
            EXPECT_EQ(rig.run(id, "Size", { form("FormList", 0x4242) }).integer, 0);
            EXPECT_EQ(rig.run(id, "At", { form("FormList", 0x4242), PapyrusValue::makeInt(0) }).kind,
                PapyrusKind::None);
        }
    }

    TEST(PapyrusFormNatives, angles_are_given_in_degrees)
    {
        AnsweringWorld world;
        world.mAngles[kRef] = { kPi / 2.0, -kPi, 0.25 };
        Rig rig(world);
        const InstanceId id = rig.probe();
        const PapyrusValue ref = form("ObjectReference", kRef);
        EXPECT_NEAR(rig.run(id, "GetAngleX", { ref }).number, 90.0f, 1e-3f);
        EXPECT_NEAR(rig.run(id, "GetAngleY", { ref }).number, -180.0f, 1e-3f);
        EXPECT_NEAR(rig.run(id, "GetAngleZ", { ref }).number, 0.25f * 180.0f / static_cast<float>(kPi), 1e-3f);
        EXPECT_EQ(rig.run(id, "GetAngleX", { form("ObjectReference", 0x1) }).number, 0.0f);
    }

    TEST(PapyrusFormNatives, a_world_without_the_capability_gets_no_query_natives)
    {
        QueryWorld world;
        Rig rig(world);
        EXPECT_TRUE(rig.mNatives.queriesMissing());
        EXPECT_TRUE(rig.mReport.ok());
        // the query natives stay tolerant: a zero value, no error
        const InstanceId id = rig.probe();
        EXPECT_EQ(rig.run(id, "Level", { form("Actor", kNpc) }).integer, 0);
        EXPECT_EQ(rig.mRuntime->stats().unregisteredNativeCalls, 1u);
        // the cosmetic natives do not need it
        EXPECT_EQ(rig.run(id, "Alpha", { form("Actor", kNpc) }).integer, 7);
        EXPECT_EQ(rig.mRuntime->stats().unregisteredNativeCalls, 1u);
    }

    TEST(PapyrusFormNatives, cosmetic_natives_do_nothing_and_a_sound_has_instance_zero)
    {
        for (const Target target : kBothTargets)
        {
            AnsweringWorld world;
            Rig rig(world, target);
            const InstanceId id = rig.probe();
            EXPECT_EQ(rig.run(id, "Alpha", { form("Actor", kNpc) }).integer, 7);
            const PapyrusValue played = rig.run(id, "PlaySound", { form("Sound", 0x11), form("ObjectReference", kRef) });
            EXPECT_EQ(played.kind, PapyrusKind::Int);
            EXPECT_EQ(played.integer, 0);
            EXPECT_EQ(rig.run(id, "Stop").integer, 9);
            EXPECT_EQ(rig.mRuntime->stats().unregisteredNativeCalls, 0u) << "answered, not tolerant";
            EXPECT_EQ(rig.mRuntime->stats().nativeCalls, 3u);
        }
    }

    TEST(PapyrusFormNatives, a_native_the_game_pack_registered_first_stays_its_own)
    {
        AnsweringWorld world;
        world.mLevels[kNpc] = 23;
        Rig rig(world); // the rig registers everything once
        ScriptPapyrus::FormNatives again(world);
        const ScriptPapyrus::PureNativesReport second = again.registerAll(*rig.mRuntime);
        EXPECT_EQ(second.registered, 0u);
        EXPECT_FALSE(second.ok()) << "the pairs are bound already";
        EXPECT_EQ(rig.run(rig.probe(), "Level", { form("Actor", kNpc) }).integer, 23);
    }

    TEST(PapyrusFormNatives, classes_a_game_does_not_declare_are_skipped_not_failed)
    {
        AnsweringWorld world;
        auto runtime = ScriptPapyrus::createRuntime(world, UnknownFunctionPolicy::Tolerant);
        ScriptPapyrus::FormNatives natives(world);
        const auto report = natives.registerAll(*runtime);
        EXPECT_TRUE(report.ok()) << "classes that are not loaded yet wait";
        EXPECT_GT(report.registered, 0u);
    }
}
