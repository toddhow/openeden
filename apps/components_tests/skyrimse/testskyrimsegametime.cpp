// Task 0276 (marriage, second slice): the Papyrus host's game-time updates (RegisterForUpdateGameTime,
// RegisterForSingleUpdateGameTime, UnregisterForUpdateGameTime, OnUpdateGameTime), driven by the game clock the pack
// gives. Generated scripts, no game data.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <iostream>
#include <map>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuestG = 0x01000800;
    constexpr FormId kRefG = 0x01000A00;
    constexpr FormId kRefOther = 0x01000A01;
    constexpr FormId kBaseG = 0x01000B00;
    constexpr double kHour = 3600.0;

    LoadOrder gtOrder()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    Bytes gtQuestRecord()
    {
        Bytes b;
        b.append(sub4("EDID", Bytes().zstring("QGameTime")));
        b.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
        b.append(sub4("NEXT", Bytes()));
        b.append(sub4("INDX", Bytes().u16(10).u8(0).u8(0)));
        b.append(sub4("QSDT", Bytes().u8(0)));
        return record4("QUST", 0u, kQuestG, b);
    }

    class GtData final : public PapyrusGameData
    {
    public:
        bool readPex(const std::string& name, std::vector<std::uint8_t>& bytes) override
        {
            const auto it = mScripts.find(name);
            if (it == mScripts.end())
                return false;
            bytes = it->second;
            return true;
        }
        std::string formClass(FormId form) const override
        {
            const auto it = mClasses.find(form);
            return it != mClasses.end() ? it->second : std::string();
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        // Task 0276: IsInLocation: the reference stands in location 0x500, which is inside location 0x600
        FormId locationOf(FormId reference) const override { return reference == kRefG ? 0x500 : 0; }
        bool locationWithin(FormId location, FormId ancestor) const override
        {
            return location != 0 && (location == ancestor || (location == 0x500 && ancestor == 0x600));
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
    };

    void addGtClasses(GtData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        form.function("RegisterForUpdateGameTime").native().param("afInterval", "Float");
        form.function("RegisterForSingleUpdateGameTime").native().param("afInterval", "Float");
        form.function("UnregisterForUpdateGameTime").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("IsInLocation", "Bool").native().param("akLocation", "Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        data.add(actor);
    }

    ScriptBuilder gtScript()
    {
        ScriptBuilder s("TestTimer");
        s.parent("ObjectReference");
        s.var("ticks", "Int");
        s.var("inside", "Bool");
        s.function("Ask")
            .param("akLocation", "Form")
            .local("b", "Bool")
            .callMethod("IsInLocation", Id("self"), Id("b"), { Id("akLocation") })
            .assign(Id("inside"), Id("b"));
        s.function("AskOther")
            .param("akOther", "ObjectReference")
            .param("akLocation", "Form")
            .local("n", "None")
            .callMethod("Ask", Id("akOther"), Id("n"), { Id("akLocation") });
        s.function("RegOther")
            .param("akOther", "ObjectReference")
            .param("afHours", "Float")
            .local("n", "None")
            .callMethod("RegisterForUpdateGameTime", Id("akOther"), Id("n"), { Id("afHours") });
        s.function("RegRepeat")
            .param("afHours", "Float")
            .local("n", "None")
            .callMethod("RegisterForUpdateGameTime", Id("self"), Id("n"), { Id("afHours") });
        s.function("RegSingle")
            .param("afHours", "Float")
            .local("n", "None")
            .callMethod("RegisterForSingleUpdateGameTime", Id("self"), Id("n"), { Id("afHours") });
        s.function("Unreg").local("n", "None").callMethod("UnregisterForUpdateGameTime", Id("self"), Id("n"), {});
        s.function("OnUpdateGameTime").iadd(Id("ticks"), Id("ticks"), Int(1));
        return s;
    }

    struct GtRig
    {
        explicit GtRig(bool timersOn = true)
        {
            addGtClasses(mData);
            mData.add(gtScript());
            mData.mClasses[kRefG] = "ObjectReference";
            mData.mClasses[kRefOther] = "ObjectReference";
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->enableGameTimers(timersOn);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            bytes.append(group4(Content::fourCC("QUST"), 0, gtQuestRecord()));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", gtOrder(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", gtOrder()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
            vmad::Script s;
            s.mName = "TestTimer";
            EXPECT_EQ(mHost->attachReference(kRefG, kBaseG, false, { s }), 1u);
            EXPECT_EQ(mHost->attachReference(kRefOther, kBaseG, false, { s }), 1u);
            tick();
        }
        ~GtRig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void tick(int n = 2) { for (int i = 0; i < n; ++i) mHost->tick(0.1f); }
        void clock(double seconds)
        {
            mHost->setGameClock(seconds);
            tick();
        }
        void call(FormId ref, const char* function, float hours)
        {
            const PapyrusHost::CallArgument args[] = { PapyrusHost::CallArgument::number(hours) };
            ASSERT_TRUE(mHost->callFunctionWith(ref, "TestTimer", function, args));
        }
        double ticks(FormId ref = kRefG) { return mHost->numberVariable(ref, "TestTimer", "ticks").value_or(-99); }

        GtData mData;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST(SkyrimGameTime, a_single_registration_fires_once_when_the_game_clock_reaches_it)
    {
        GtRig rig;
        rig.clock(1000.0);
        rig.call(kRefG, "RegSingle", 2.0f);
        rig.clock(1000.0 + 2 * kHour - 1.0);
        EXPECT_EQ(rig.ticks(), 0.0);
        rig.clock(1000.0 + 2 * kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 1.0);
        rig.clock(1000.0 + 20 * kHour);
        EXPECT_EQ(rig.ticks(), 1.0) << "single means single";
        EXPECT_EQ(rig.mHost->gameTimerCount(), 0u);
    }

    TEST(SkyrimGameTime, a_repeating_registration_fires_every_interval)
    {
        GtRig rig;
        rig.clock(5000.0);
        rig.call(kRefG, "RegRepeat", 1.0f);
        rig.clock(5000.0 + kHour * 0.5);
        EXPECT_EQ(rig.ticks(), 0.0);
        rig.clock(5000.0 + kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 1.0);
        rig.clock(5000.0 + 2 * kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 2.0);
        EXPECT_EQ(rig.mHost->gameTimerCount(), 1u);
    }

    TEST(SkyrimGameTime, a_long_wait_fires_a_repeating_timer_once_and_it_goes_on_at_its_next_multiple)
    {
        GtRig rig;
        rig.clock(0.0);
        rig.call(kRefG, "RegRepeat", 1.0f);
        rig.clock(30 * kHour + 100.0);
        EXPECT_EQ(rig.ticks(), 1.0);
        rig.clock(30 * kHour + 200.0);
        EXPECT_EQ(rig.ticks(), 1.0) << "the next one is at hour 31";
        rig.clock(31 * kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 2.0);
    }

    TEST(SkyrimGameTime, a_registration_before_the_first_clock_waits_for_it)
    {
        GtRig rig;
        rig.call(kRefG, "RegSingle", 1.0f); // the clock is not known yet
        rig.tick(5);
        EXPECT_EQ(rig.ticks(), 0.0);
        rig.clock(90000.0); // the first clock: the due time is 90000 + 3600
        EXPECT_EQ(rig.ticks(), 0.0);
        rig.clock(90000.0 + kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 1.0);
    }

    TEST(SkyrimGameTime, unregistering_stops_the_updates_and_another_instance_is_left_alone)
    {
        GtRig rig;
        rig.clock(100.0);
        rig.call(kRefG, "RegRepeat", 1.0f);
        rig.call(kRefOther, "RegRepeat", 1.0f);
        ASSERT_TRUE(rig.mHost->callFunction(kRefG, "TestTimer", "Unreg"));
        rig.clock(100.0 + kHour + 1.0);
        EXPECT_EQ(rig.ticks(kRefG), 0.0);
        EXPECT_EQ(rig.ticks(kRefOther), 1.0);
    }

    TEST(SkyrimGameTime, a_repeating_and_a_single_registration_live_side_by_side_and_each_kind_replaces_its_own)
    {
        GtRig rig;
        rig.clock(0.0);
        rig.call(kRefG, "RegRepeat", 3.0f);
        rig.call(kRefG, "RegSingle", 5.0f);
        EXPECT_EQ(rig.mHost->gameTimerCount(), 2u);
        rig.call(kRefG, "RegSingle", 1.0f); // replaces the single, not the repeating one
        EXPECT_EQ(rig.mHost->gameTimerCount(), 2u);
        rig.clock(kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 1.0) << "the single one, now due at hour 1";
        rig.clock(3 * kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 2.0) << "the repeating one at hour 3; the replaced single (hour 5) is gone";
        rig.clock(6 * kHour);
        EXPECT_EQ(rig.ticks(), 3.0) << "the repeating one at hour 6 only";
    }

    TEST(SkyrimGameTime, a_registration_made_on_another_reference_reaches_that_references_script)
    {
        // QF_RelationshipMarriageFIN calls Alias_LoveInterest.RegisterForUpdateGameTime(24) on a property: the receiver is
        // not the script itself, its scripts get the updates
        GtRig rig;
        rig.clock(0.0);
        const PapyrusHost::CallArgument args[]
            = { PapyrusHost::CallArgument::form(kRefOther), PapyrusHost::CallArgument::number(1.0f) };
        ASSERT_TRUE(rig.mHost->callFunctionWith(kRefG, "TestTimer", "RegOther", args));
        EXPECT_EQ(rig.mHost->gameTimerCount(), 1u);
        rig.clock(kHour + 1.0);
        EXPECT_EQ(rig.ticks(kRefOther), 1.0);
        EXPECT_EQ(rig.ticks(kRefG), 0.0);
    }

    TEST(SkyrimGameTime, a_call_through_a_reference_handle_reaches_the_function_of_the_script_attached_to_it)
    {
        // TIF__000D0652 calls LoveInterest.GiveGold(): the property is typed as the alias's script, its value is the
        // alias handle (class ReferenceAlias), and GiveGold is a function of the script attached to the alias
        GtRig rig;
        const PapyrusHost::CallArgument args[]
            = { PapyrusHost::CallArgument::form(kRefG), PapyrusHost::CallArgument::form(0x600) };
        ASSERT_TRUE(rig.mHost->callFunctionWith(kRefOther, "TestTimer", "AskOther", args));
        EXPECT_EQ(rig.mHost->numberVariable(kRefG, "TestTimer", "inside").value_or(-99), 1.0)
            << "Ask ran on kRefG's script: its location 0x500 is inside 0x600";
        EXPECT_EQ(rig.mHost->numberVariable(kRefOther, "TestTimer", "inside").value_or(-99), 0.0);
    }

    TEST(SkyrimGameTime, IsInLocation_is_true_for_the_location_and_the_ones_it_is_inside_only)
    {
        GtRig rig;
        const auto ask = [&](FormId reference, FormId location) {
            const PapyrusHost::CallArgument args[] = { PapyrusHost::CallArgument::form(location) };
            EXPECT_TRUE(rig.mHost->callFunctionWith(reference, "TestTimer", "Ask", args));
            return rig.mHost->numberVariable(reference, "TestTimer", "inside").value_or(-99) == 1.0;
        };
        EXPECT_TRUE(ask(kRefG, 0x500)) << "the location itself";
        EXPECT_TRUE(ask(kRefG, 0x600)) << "a location it is inside";
        EXPECT_FALSE(ask(kRefG, 0x700)) << "another location";
        EXPECT_FALSE(ask(kRefOther, 0x500)) << "a reference with no location";
    }

    TEST(SkyrimGameTime, the_ignored_form_registers_nothing)
    {
        GtRig rig;
        rig.mHost->ignoreGameTimersOf(kRefG);
        rig.clock(0.0);
        rig.call(kRefG, "RegRepeat", 1.0f);
        EXPECT_EQ(rig.mHost->gameTimerCount(), 0u);
        rig.clock(10 * kHour);
        EXPECT_EQ(rig.ticks(), 0.0);
    }

    TEST(SkyrimGameTime, with_game_timers_off_the_natives_do_nothing)
    {
        GtRig rig(false);
        rig.mHost->setGameClock(0.0);
        rig.call(kRefG, "RegRepeat", 1.0f);
        EXPECT_EQ(rig.mHost->gameTimerCount(), 0u);
        rig.clock(10 * kHour);
        EXPECT_EQ(rig.ticks(), 0.0);
    }

    TEST(SkyrimGameTime, the_timers_are_saved_and_a_state_without_them_keeps_its_old_version)
    {
        GtRig rig;
        const std::vector<std::uint8_t> before = rig.mHost->saveState();
        rig.clock(1000.0);
        rig.call(kRefG, "RegRepeat", 2.0f);
        const std::vector<std::uint8_t> bytes = rig.mHost->saveState();
        ASSERT_FALSE(bytes.empty());
        EXPECT_GT(bytes.size(), before.size());
        EXPECT_EQ(rig.mHost->gameTimerCount(), 1u);
        ASSERT_TRUE(rig.mHost->restoreSavedState(bytes).mOk);
        EXPECT_EQ(rig.mHost->gameTimerCount(), 1u);
        rig.clock(1000.0 + 2 * kHour + 1.0);
        EXPECT_EQ(rig.ticks(), 1.0);
        // unregistered: the state is the old layout again (version 3: no perks, no timers)
        ASSERT_TRUE(rig.mHost->callFunction(kRefG, "TestTimer", "Unreg"));
        const std::vector<std::uint8_t> after = rig.mHost->saveState();
        ASSERT_GE(after.size(), 8u);
        EXPECT_EQ(after[4], 3u) << "the version word";
    }
}
