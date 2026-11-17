// Task 0198 (milestone M-S6): the world natives of the Papyrus host (inventory, actor values, deaths, plugin lookup,
// notifications), the events combat and the inventory raise (OnItemAdded, OnItemRemoved, OnContainerChanged, OnHit,
// OnDying, OnDeath), the hooks over the pack's inventory model, and the combat bridge. Generated scripts, a fake game
// and a real InventoryModel; no game data. Written AFTER the code (the first run of the task's design was built in one
// go); the red evidence is by mutation (see the task's stage log), not against stubs.
#include <game_skyrimse/inventory.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Bool;
    using PexAsm::Flt;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;
    using PexAsm::Str;

    constexpr FormId kQuestA = 0x01000800;
    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kOther = 0x01000A02;
    constexpr FormId kChest = 0x01000A03;
    constexpr FormId kItemA = 0x01000B01;
    constexpr FormId kItemB = 0x01000B02;
    constexpr FormId kBase = 0x01000B00;

    LoadOrder order()
    {
        LoadOrder o;
        std::string err;
        EXPECT_TRUE(o.add("Skyrim.esm", false, err));
        EXPECT_TRUE(o.add("test.esp", false, err));
        return o;
    }

    class FakeData final : public PapyrusGameData
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
        bool referencePosition(FormId reference, double& x, double& y, double& z) const override
        {
            const auto it = mPositions.find(reference);
            if (it == mPositions.end())
                return false;
            x = it->second[0];
            y = it->second[1];
            z = it->second[2];
            return true;
        }
        bool initiallyDead(FormId actor) const override { return mDeadAtStart.count(actor) != 0; }
        FormId linkedRef(FormId reference, FormId keyword) const override
        {
            const auto it = mLinks.find({ reference, keyword });
            return it != mLinks.end() ? it->second : 0;
        }
        FormId formFromFile(std::string_view file, std::uint32_t local) const override
        {
            return file == "skyrim.esm" ? (local & 0xFFFFFF) : 0;
        }
        void add(const ScriptBuilder& script)
        {
            std::string lower = script.name();
            for (char& c : lower)
                c = static_cast<char>(c >= 'A' && c <= 'Z' ? c + 32 : c);
            mScripts[lower] = script.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        }
        std::map<std::string, std::vector<std::uint8_t>> mScripts;
        std::map<FormId, std::string> mClasses;
        std::map<FormId, std::array<double, 3>> mPositions;
        std::set<FormId> mDeadAtStart;
        std::map<std::pair<FormId, FormId>, FormId> mLinks;
    };

    // A hook that keeps an inventory of its own as plain maps, an actor value table, and a list of what it was asked.
    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        bool addItem(FormId owner, FormId item, std::int32_t count) override
        {
            mItems[{ owner, item }] += count;
            mLog.push_back("add " + std::to_string(owner) + " " + std::to_string(item) + " " + std::to_string(count));
            return true;
        }
        std::int32_t removeItem(FormId owner, FormId item, std::int32_t count) override
        {
            std::int32_t& have = mItems[{ owner, item }];
            const std::int32_t take = std::min(have, count);
            have -= take;
            mLog.push_back(
                "remove " + std::to_string(owner) + " " + std::to_string(item) + " " + std::to_string(count));
            return take;
        }
        std::int32_t itemCount(FormId owner, FormId item) override { return mItems[{ owner, item }]; }
        bool transferItem(FormId from, FormId to, FormId item, std::int32_t count) override
        {
            mLog.push_back("transfer " + std::to_string(from) + " " + std::to_string(to) + " " + std::to_string(count));
            mItems[{ from, item }] -= count;
            mItems[{ to, item }] += count;
            return true;
        }
        std::int64_t removeAllItems(FormId owner, FormId target) override
        {
            mLog.push_back("removeall " + std::to_string(owner) + " " + std::to_string(target));
            return 0;
        }
        bool actorValue(FormId actor, std::string_view name, double& current, double& maximum) override
        {
            const auto it = mValues.find({ actor, std::string(name) });
            if (it == mValues.end())
                return false;
            current = it->second.first;
            maximum = it->second.second;
            return true;
        }
        bool setHealth(FormId actor, double health) override
        {
            if (!mCanSetHealth)
                return false;
            mLog.push_back("sethealth " + std::to_string(actor) + " " + std::to_string(static_cast<int>(health)));
            mValues[{ actor, "health" }].first = health;
            return true;
        }
        bool isDead(FormId actor) override { return mDead.count(actor) != 0; }
        bool killActor(FormId actor) override
        {
            mKills.push_back(actor);
            return true;
        }
        bool isItem(FormId form) override { return form == kItemA || form == kItemB; }
        void notification(const std::string& text) override { mNotes.push_back(text); }

        std::int32_t held(FormId owner, FormId item) { return mItems[{ owner, item }]; }
        std::map<std::pair<FormId, FormId>, std::int32_t> mItems;
        std::map<std::pair<FormId, std::string>, std::pair<double, double>> mValues;
        std::set<FormId> mDead;
        std::vector<FormId> mKills;
        std::vector<std::string> mNotes;
        std::vector<std::string> mLog;
        bool mCanSetHealth = true;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("AddItem").native().param("akItemToAdd", "Form").param("aiCount", "Int").param("abSilent", "Bool");
        ref.function("RemoveItem")
            .native()
            .param("akItemToRemove", "Form")
            .param("aiCount", "Int")
            .param("abSilent", "Bool")
            .param("akOtherContainer", "ObjectReference");
        ref.function("GetItemCount", "Int").native().param("akItem", "Form");
        ref.function("RemoveAllItems").native().param("akTransferTo", "ObjectReference");
        ref.function("GetLinkedRef", "ObjectReference").native().param("apKeyword", "Form");
        ref.function("GetDistance", "Float").native().param("akOther", "ObjectReference");
        ref.function("AddInventoryEventFilter").native().param("akFilter", "Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        actor.function("GetActorValue", "Float").native().param("asValueName", "String");
        actor.function("GetBaseActorValue", "Float").native().param("asValueName", "String");
        actor.function("GetActorValuePercentage", "Float").native().param("asValueName", "String");
        actor.function("SetActorValue").native().param("asValueName", "String").param("afValue", "Float");
        actor.function("ModActorValue").native().param("asValueName", "String").param("afAmount", "Float");
        actor.function("DamageActorValue").native().param("asValueName", "String").param("afDamage", "Float");
        actor.function("RestoreActorValue").native().param("asValueName", "String").param("afAmount", "Float");
        actor.function("IsDead", "Bool").native();
        actor.function("Kill").native().param("akKiller", "Actor");
        actor.function("Resurrect").native();
        actor.function("Banish").native();
        data.add(actor);
        ScriptBuilder game("Game");
        game.function("GetFormFromFile", "Form")
            .global()
            .native()
            .param("aiFormID", "Int")
            .param("asFilename", "String");
        data.add(game);
        ScriptBuilder debug("Debug");
        debug.function("Notification").global().native().param("asNotificationText", "String");
        data.add(debug);
    }

    // On an actor: calls the natives and keeps the answers.
    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("Actor");
        s.autoProperty("Item", "Form");
        s.autoProperty("Other", "ObjectReference");
        s.var("count", "Int");
        s.var("av", "Float");
        s.var("pct", "Float");
        s.var("base", "Float");
        s.var("dead", "Bool");
        s.var("dist", "Float");
        s.var("fileForm", "Int");
        s.var("linked", "ObjectReference");
        s.var("added", "Int");
        s.var("removed", "Int");
        s.var("lastAdded", "Form");
        s.var("lastSource", "ObjectReference");
        s.var("changed", "Int");
        s.var("hits", "Int");
        s.var("blockedHits", "Int");
        s.var("dying", "Int");
        s.var("deaths", "Int");
        s.var("killer", "Actor");
        s.function("Add")
            .local("n", "None")
            .callMethod("AddItem", Id("self"), Id("n"), { Id("::Item_var"), Int(3), Bool(false) });
        s.function("Remove")
            .local("n", "None")
            .callMethod("RemoveItem", Id("self"), Id("n"), { Id("::Item_var"), Int(2), Bool(false), Nil() });
        s.function("RemoveTo")
            .local("n", "None")
            .callMethod(
                "RemoveItem", Id("self"), Id("n"), { Id("::Item_var"), Int(2), Bool(false), Id("::Other_var") });
        s.function("Count")
            .local("v", "Int")
            .callMethod("GetItemCount", Id("self"), Id("v"), { Id("::Item_var") })
            .assign(Id("count"), Id("v"));
        s.function("All").local("n", "None").callMethod("RemoveAllItems", Id("self"), Id("n"), { Id("::Other_var") });
        s.function("Filter")
            .local("n", "None")
            .callMethod("AddInventoryEventFilter", Id("self"), Id("n"), { Id("::Item_var") });
        s.function("ReadHealth")
            .local("v", "Float")
            .callMethod("GetActorValue", Id("self"), Id("v"), { Str("Health") })
            .assign(Id("av"), Id("v"))
            .callMethod("GetActorValuePercentage", Id("self"), Id("v"), { Str("health") })
            .assign(Id("pct"), Id("v"))
            .callMethod("GetBaseActorValue", Id("self"), Id("v"), { Str("Health") })
            .assign(Id("base"), Id("v"));
        s.function("ReadStamina")
            .local("v", "Float")
            .callMethod("GetActorValue", Id("self"), Id("v"), { Str("Stamina") })
            .assign(Id("av"), Id("v"));
        s.function("Damage")
            .local("n", "None")
            .callMethod("DamageActorValue", Id("self"), Id("n"), { Str("Health"), Flt(30.0f) });
        s.function("DamageStamina")
            .local("n", "None")
            .callMethod("DamageActorValue", Id("self"), Id("n"), { Str("Stamina"), Flt(30.0f) });
        s.function("RestoreStamina")
            .local("n", "None")
            .callMethod("RestoreActorValue", Id("self"), Id("n"), { Str("Stamina"), Flt(500.0f) });
        s.function("SetStamina")
            .local("n", "None")
            .callMethod("SetActorValue", Id("self"), Id("n"), { Str("Stamina"), Flt(7.0f) });
        s.function("ModStamina")
            .local("n", "None")
            .callMethod("ModActorValue", Id("self"), Id("n"), { Str("Stamina"), Flt(-100.0f) });
        s.function("CheckDead")
            .local("b", "Bool")
            .callMethod("IsDead", Id("self"), Id("b"))
            .assign(Id("dead"), Id("b"));
        s.function("KillSelf").local("n", "None").callMethod("Kill", Id("self"), Id("n"), { Nil() });
        s.function("Distance")
            .local("v", "Float")
            .callMethod("GetDistance", Id("self"), Id("v"), { Id("::Other_var") })
            .assign(Id("dist"), Id("v"));
        s.function("FromFile")
            .local("f", "Form")
            .local("v", "Int")
            .callStatic("Game", "GetFormFromFile", Id("f"), { Int(0x12345), Str("Skyrim.esm") })
            .callMethod("GetFormID", Id("f"), Id("v"))
            .assign(Id("fileForm"), Id("v"));
        s.function("Revive")
            .local("n", "None")
            .callMethod("Resurrect", Id("self"), Id("n"))
            .callMethod("Resurrect", Id("self"), Id("n"));
        s.function("BanishIt").local("n", "None").callMethod("Banish", Id("self"), Id("n"));
        s.function("Linked")
            .local("r", "ObjectReference")
            .callMethod("GetLinkedRef", Id("self"), Id("r"), { Nil() })
            .assign(Id("linked"), Id("r"));
        s.function("Notify").local("n", "None").callStatic("Debug", "Notification", Id("n"), { Str("Hello there") });
        s.function("OnItemAdded")
            .param("akBaseItem", "Form")
            .param("aiItemCount", "Int")
            .param("akItemReference", "ObjectReference")
            .param("akSourceContainer", "ObjectReference")
            .iadd(Id("added"), Id("added"), Id("aiItemCount"))
            .assign(Id("lastAdded"), Id("akBaseItem"))
            .assign(Id("lastSource"), Id("akSourceContainer"));
        s.function("OnItemRemoved")
            .param("akBaseItem", "Form")
            .param("aiItemCount", "Int")
            .param("akItemReference", "ObjectReference")
            .param("akDestContainer", "ObjectReference")
            .iadd(Id("removed"), Id("removed"), Id("aiItemCount"));
        s.function("OnContainerChanged")
            .param("akNewContainer", "ObjectReference")
            .param("akOldContainer", "ObjectReference")
            .iadd(Id("changed"), Id("changed"), Int(1));
        s.function("OnHit")
            .param("akAggressor", "ObjectReference")
            .param("akSource", "Form")
            .param("akProjectile", "Form")
            .param("abPowerAttack", "Bool")
            .param("abSneakAttack", "Bool")
            .param("abBashAttack", "Bool")
            .param("abHitBlocked", "Bool")
            .iadd(Id("hits"), Id("hits"), Int(1))
            .jmpf(Id("abHitBlocked"), 2)
            .iadd(Id("blockedHits"), Id("blockedHits"), Int(1));
        s.function("OnDying").param("akKiller", "Actor").iadd(Id("dying"), Id("dying"), Int(1));
        s.function("OnDeath")
            .param("akKiller", "Actor")
            .iadd(Id("deaths"), Id("deaths"), Int(1))
            .assign(Id("killer"), Id("akKiller"));
        return s;
    }

    vmad::Property objProp(const std::string& name, FormId form)
    {
        vmad::Property p;
        p.mName = name;
        p.mType = 1;
        p.mValue.mKind = vmad::ValueKind::Object;
        p.mValue.mForm = form;
        p.mValue.mAlias = -1;
        return p;
    }

    vmad::Script probeOn(FormId item = kItemA, FormId other = kOther)
    {
        vmad::Script s;
        s.mName = "Probe";
        s.mProperties = { objProp("Item", item), objProp("Other", other) };
        return s;
    }

    struct Rig
    {
        Rig()
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.mClasses[kQuestA] = "Quest";
            mData.mClasses[kActor] = "Actor";
            mData.mClasses[kOther] = "Actor";
            mData.mClasses[kChest] = "ObjectReference";
            mData.mClasses[kItemA] = "Form";
            mData.mClasses[kItemB] = "Form";
            mData.mPositions[kActor] = { 0.0, 0.0, 0.0 };
            mData.mPositions[kOther] = { 3.0, 4.0, 12.0 };
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuestA, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order()));
            mState = QuestState::initial(mIndex);
            mHost->setQuests(&mIndex, &mState, [](FormId) -> const vmad::Vmad* { return nullptr; });
            mHost->startGame();
        }
        ~Rig()
        {
            if (::testing::Test::HasFailure())
                for (const std::string& line : mErrors)
                    std::cerr << "  error: " << line << std::endl;
        }
        void attach(FormId ref, vmad::Script script, bool actor = true)
        {
            ASSERT_EQ(mHost->attachReference(ref, kBase, actor, { std::move(script) }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(FormId ref, const char* function)
        {
            const bool ok = mHost->callFunction(ref, "Probe", function);
            tick();
            return ok;
        }
        std::optional<double> number(FormId ref, const char* name) { return mHost->numberVariable(ref, "Probe", name); }

        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    // ---- inventory natives -----------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusNatives2, the_inventory_natives_reach_the_hook_with_owner_item_and_count)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "Add"));
        EXPECT_EQ(rig.mHooks.held(kActor, kItemA), 3);
        ASSERT_TRUE(rig.call(kActor, "Remove"));
        EXPECT_EQ(rig.mHooks.held(kActor, kItemA), 1);
        ASSERT_TRUE(rig.call(kActor, "Count"));
        EXPECT_EQ(rig.number(kActor, "count"), 1.0);
        ASSERT_TRUE(rig.call(kActor, "RemoveTo"));
        EXPECT_EQ(rig.mHooks.held(kOther, kItemA), 2) << "RemoveItem with a container transfers";
        ASSERT_TRUE(rig.call(kActor, "All"));
        EXPECT_EQ(rig.mHooks.mLog.back(), "removeall " + std::to_string(kActor) + " " + std::to_string(kOther));
    }

    TEST(SkyrimPapyrusNatives2, without_hooks_the_inventory_natives_are_tolerant_and_count_zero)
    {
        Rig rig;
        rig.mHost->setWorldHooks(nullptr);
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "Add"));
        ASSERT_TRUE(rig.call(kActor, "Count"));
        EXPECT_EQ(rig.number(kActor, "count"), 0.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    // ---- actor values
    // ------------------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusNatives2, health_is_read_from_the_pack_and_damage_goes_to_the_pack)
    {
        Rig rig;
        rig.mHooks.mValues[{ kActor, "health" }] = { 100.0, 120.0 };
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "ReadHealth"));
        EXPECT_EQ(rig.number(kActor, "av"), 100.0);
        EXPECT_NEAR(*rig.number(kActor, "pct"), 100.0 / 120.0, 1e-6);
        EXPECT_EQ(rig.number(kActor, "base"), 120.0) << "the base value is the maximum";
        ASSERT_TRUE(rig.call(kActor, "Damage"));
        EXPECT_EQ(rig.mHooks.mLog.back(), "sethealth " + std::to_string(kActor) + " 70");
        ASSERT_TRUE(rig.call(kActor, "ReadHealth"));
        EXPECT_EQ(rig.number(kActor, "av"), 70.0);
    }

    TEST(SkyrimPapyrusNatives2, a_value_the_pack_cannot_write_is_kept_as_a_change_the_script_sees)
    {
        Rig rig;
        rig.mHooks.mValues[{ kActor, "stamina" }] = { 50.0, 100.0 };
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "DamageStamina"));
        ASSERT_TRUE(rig.call(kActor, "ReadStamina"));
        EXPECT_EQ(rig.number(kActor, "av"), 20.0);
        ASSERT_TRUE(rig.call(kActor, "RestoreStamina"));
        ASSERT_TRUE(rig.call(kActor, "ReadStamina"));
        EXPECT_EQ(rig.number(kActor, "av"), 100.0) << "never above the maximum";
        ASSERT_TRUE(rig.call(kActor, "ModStamina"));
        ASSERT_TRUE(rig.call(kActor, "ReadStamina"));
        EXPECT_EQ(rig.number(kActor, "av"), 0.0) << "never below zero";
        ASSERT_TRUE(rig.call(kActor, "SetStamina"));
        ASSERT_TRUE(rig.call(kActor, "ReadStamina"));
        EXPECT_EQ(rig.number(kActor, "av"), 7.0);
    }

    TEST(SkyrimPapyrusNatives2, health_the_pack_cannot_write_falls_back_to_a_change)
    {
        Rig rig;
        rig.mHooks.mCanSetHealth = false;
        rig.mHooks.mValues[{ kActor, "health" }] = { 100.0, 100.0 };
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "Damage"));
        ASSERT_TRUE(rig.call(kActor, "ReadHealth"));
        EXPECT_EQ(rig.number(kActor, "av"), 70.0);
    }

    TEST(SkyrimPapyrusNatives2, an_unknown_value_reads_zero)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "ReadStamina"));
        EXPECT_EQ(rig.number(kActor, "av"), 0.0);
    }

    // ---- death, plugin lookup, distance, notifications -----------------------------------------------------------

    TEST(SkyrimPapyrusNatives2, IsDead_reads_the_data_the_pack_and_a_kill)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "CheckDead"));
        EXPECT_EQ(rig.number(kActor, "dead"), 0.0);
        rig.mHooks.mDead.insert(kActor);
        ASSERT_TRUE(rig.call(kActor, "CheckDead"));
        EXPECT_EQ(rig.number(kActor, "dead"), 1.0);
        rig.mHooks.mDead.clear();
        ASSERT_TRUE(rig.call(kActor, "KillSelf"));
        ASSERT_EQ(rig.mHooks.mKills.size(), 1u);
        EXPECT_EQ(rig.mHooks.mKills[0], kActor);
        ASSERT_TRUE(rig.call(kActor, "CheckDead"));
        EXPECT_EQ(rig.number(kActor, "dead"), 1.0) << "dead as soon as the kill was asked";
    }

    TEST(SkyrimPapyrusNatives2, an_actor_the_data_places_dead_is_dead)
    {
        Rig rig;
        rig.mData.mDeadAtStart.insert(kActor);
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "CheckDead"));
        EXPECT_EQ(rig.number(kActor, "dead"), 1.0);
        ASSERT_TRUE(rig.call(kActor, "KillSelf"));
        EXPECT_TRUE(rig.mHooks.mKills.empty()) << "a dead actor is not killed again";
    }

    TEST(SkyrimPapyrusNatives2, GetFormFromFile_asks_the_data_and_GetDistance_uses_positions)
    {
        Rig rig;
        rig.mData.mClasses[0x12345] = "Form";
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "FromFile"));
        EXPECT_EQ(rig.number(kActor, "fileForm"), 74565.0);
        ASSERT_TRUE(rig.call(kActor, "Distance"));
        EXPECT_NEAR(*rig.number(kActor, "dist"), 13.0, 1e-4) << "3-4-12 is 13";
    }

    TEST(SkyrimPapyrusNatives2, GetLinkedRef_asks_the_data_and_none_gives_none)
    {
        Rig rig;
        rig.mData.mLinks[{ kActor, 0 }] = kChest;
        rig.attach(kActor, probeOn());
        ASSERT_TRUE(rig.call(kActor, "Linked"));
        EXPECT_EQ(rig.mHost->formVariable(kActor, "Probe", "linked"), kChest);
        rig.mData.mLinks.clear();
        ASSERT_TRUE(rig.call(kActor, "Linked"));
        const auto none = rig.mHost->formVariable(kActor, "Probe", "linked");
        EXPECT_TRUE(!none.has_value() || *none == 0);
    }

    TEST(SkyrimPapyrusNatives2, Debug_Notification_is_kept_in_a_ring_and_given_to_the_hook)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        EXPECT_EQ(rig.mHost->notificationCount(), 0u);
        ASSERT_TRUE(rig.call(kActor, "Notify"));
        ASSERT_TRUE(rig.call(kActor, "Notify"));
        EXPECT_EQ(rig.mHost->notificationCount(), 2u);
        EXPECT_EQ(rig.mHost->notificationAt(2), "Hello there");
        EXPECT_EQ(rig.mHost->notificationAt(0), "");
        EXPECT_EQ(rig.mHost->notificationAt(3), "");
        ASSERT_EQ(rig.mHooks.mNotes.size(), 2u);
        for (int i = 0; i < 40; ++i)
            ASSERT_TRUE(rig.call(kActor, "Notify"));
        EXPECT_EQ(rig.mHost->notificationAt(1), "") << "left the ring";
        EXPECT_EQ(rig.mHost->notificationAt(42), "Hello there");
    }

    // ---- events
    // ------------------------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusNatives2, item_events_go_to_the_owner_that_gained_and_the_one_that_lost)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        rig.attach(kOther, probeOn());
        rig.mHost->onItemMoved(kOther, kActor, kItemA, 4);
        rig.tick();
        EXPECT_EQ(rig.number(kActor, "added"), 4.0);
        EXPECT_EQ(rig.number(kActor, "removed"), 0.0);
        EXPECT_EQ(rig.mHost->formVariable(kActor, "Probe", "lastAdded"), kItemA);
        EXPECT_EQ(rig.mHost->formVariable(kActor, "Probe", "lastSource"), kOther);
        EXPECT_EQ(rig.number(kOther, "removed"), 4.0);
        EXPECT_EQ(rig.number(kOther, "added"), 0.0);
        EXPECT_EQ(rig.mHost->counters().mItemEvents, 2u);
    }

    TEST(SkyrimPapyrusNatives2, an_item_from_nowhere_has_no_source_and_a_bad_count_is_ignored)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        rig.mHost->onItemMoved(0, kActor, kItemA, 2);
        rig.mHost->onItemMoved(0, kActor, kItemA, 0);
        rig.mHost->onItemMoved(0, kActor, 0, 5);
        rig.tick();
        EXPECT_EQ(rig.number(kActor, "added"), 2.0);
        EXPECT_FALSE(rig.mHost->formVariable(kActor, "Probe", "lastSource").has_value()
            && *rig.mHost->formVariable(kActor, "Probe", "lastSource") != 0);
    }

    TEST(SkyrimPapyrusNatives2, an_inventory_event_filter_limits_the_events_to_its_items)
    {
        Rig rig;
        rig.attach(kActor, probeOn(kItemA));
        ASSERT_TRUE(rig.call(kActor, "Filter")); // filters on kItemA
        rig.mHost->onItemMoved(0, kActor, kItemB, 5);
        rig.mHost->onItemMoved(0, kActor, kItemA, 1);
        rig.tick();
        EXPECT_EQ(rig.number(kActor, "added"), 1.0);
    }

    TEST(SkyrimPapyrusNatives2, a_filter_on_something_that_is_not_an_item_restricts_nothing)
    {
        Rig rig;
        rig.attach(kActor, probeOn(0x01000FFF)); // not an item to the hook (a form list, say)
        ASSERT_TRUE(rig.call(kActor, "Filter"));
        rig.mHost->onItemMoved(0, kActor, kItemB, 5);
        rig.tick();
        EXPECT_EQ(rig.number(kActor, "added"), 5.0);
    }

    TEST(SkyrimPapyrusNatives2, OnContainerChanged_goes_to_scripts_on_the_item_form)
    {
        Rig rig;
        ASSERT_EQ(rig.mHost->attachReference(kItemA, kBase, false, { probeOn() }), 1u);
        rig.tick();
        rig.mHost->onItemMoved(kOther, kActor, kItemA, 1);
        rig.tick();
        EXPECT_EQ(rig.number(kItemA, "changed"), 1.0);
    }

    TEST(SkyrimPapyrusNatives2, a_hit_reaches_the_victim_with_the_block_flag)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        rig.mHost->onActorHit(kActor, kOther, false, false);
        rig.mHost->onActorHit(kActor, kOther, true, true);
        rig.tick();
        EXPECT_EQ(rig.number(kActor, "hits"), 2.0);
        EXPECT_EQ(rig.number(kActor, "blockedHits"), 1.0);
        EXPECT_EQ(rig.mHost->counters().mCombatEvents, 2u);
    }

    TEST(SkyrimPapyrusNatives2, a_death_raises_OnDying_then_OnDeath_once_with_the_killer)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        rig.mHost->onActorDied(kActor, kOther);
        rig.mHost->onActorDied(kActor, kOther);
        rig.tick();
        EXPECT_EQ(rig.number(kActor, "dying"), 1.0);
        EXPECT_EQ(rig.number(kActor, "deaths"), 1.0);
        EXPECT_EQ(rig.mHost->formVariable(kActor, "Probe", "killer"), kOther);
        ASSERT_TRUE(rig.call(kActor, "CheckDead"));
        EXPECT_EQ(rig.number(kActor, "dead"), 1.0);
    }

    // ---- save state
    // --------------------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusNatives2, actor_value_changes_deaths_and_filters_survive_a_save)
    {
        Rig a;
        a.mHooks.mValues[{ kActor, "stamina" }] = { 50.0, 100.0 };
        a.attach(kActor, probeOn());
        ASSERT_TRUE(a.call(kActor, "DamageStamina"));
        ASSERT_TRUE(a.call(kActor, "Filter"));
        a.mHost->onActorDied(kOther, 0);
        const std::vector<std::uint8_t> bytes = a.mHost->saveState();
        ASSERT_FALSE(bytes.empty());

        Rig b;
        b.mHooks.mValues[{ kActor, "stamina" }] = { 50.0, 100.0 };
        const PapyrusRestoreOutcome outcome = b.mHost->restoreSavedState(bytes);
        ASSERT_TRUE(outcome.mOk) << outcome.mError;
        EXPECT_EQ(b.mHost->saveState(), bytes) << "restoring and saving again gives the same bytes";
        ASSERT_TRUE(b.call(kActor, "ReadStamina"));
        EXPECT_EQ(b.number(kActor, "av"), 20.0);
        b.mHost->onItemMoved(0, kActor, kItemB, 9); // the filter came back
        b.tick();
        EXPECT_EQ(b.number(kActor, "added"), 0.0);
    }

    // ---- the tolerant ranking ---------------------------------------------------------------------------------------

    TEST(SkyrimPapyrusNatives2, tolerant_calls_are_ranked_by_calls_then_name)
    {
        Rig rig;
        rig.attach(kActor, probeOn());
        EXPECT_TRUE(rig.mHost->tolerantRanking().empty());
        ASSERT_TRUE(rig.call(kActor, "Revive")); // two calls of a native nobody answers
        ASSERT_TRUE(rig.call(kActor, "Revive"));
        ASSERT_TRUE(rig.call(kActor, "BanishIt")); // one call of another
        const auto ranking = rig.mHost->tolerantRanking();
        ASSERT_EQ(ranking.size(), 2u);
        EXPECT_EQ(ranking[0].first, "actor.resurrect");
        EXPECT_EQ(ranking[0].second, 4u);
        EXPECT_EQ(ranking[1].first, "actor.banish");
        EXPECT_EQ(ranking[1].second, 1u);
    }

    // ---- the hooks over a real inventory model
    // -----------------------------------------------------------------------

    struct ModelRig
    {
        ModelRig()
        {
            PackWorldEnv env;
            env.model = [this] { return &mModel; };
            env.ensureOwner = [this](std::uint64_t owner, FormId) {
                ++mEnsured;
                return mModel.ensureOwner(owner, {});
            };
            mHooks = std::make_unique<PackWorldHooks>(std::move(env));
            mModel.setPlayer({});
            mModel.setChangeSink([this](std::uint64_t from, std::uint64_t to, FormId item, std::int32_t count) {
                mEvents.push_back({ PackWorldHooks::referenceOf(from), PackWorldHooks::referenceOf(to), item, count });
            });
        }
        struct Event
        {
            FormId mFrom, mTo, mItem;
            std::int32_t mCount;
            friend bool operator==(const Event&, const Event&) = default;
        };
        inventory::InventoryModel mModel;
        std::unique_ptr<PackWorldHooks> mHooks;
        std::vector<Event> mEvents;
        int mEnsured = 0;
    };

    TEST(SkyrimPapyrusNatives2, the_hooks_add_and_remove_in_the_model_and_tell_the_sink)
    {
        ModelRig rig;
        ASSERT_TRUE(rig.mHooks->addItem(kPapyrusPlayerReference, kItemA, 5));
        EXPECT_EQ(rig.mModel.count(inventory::kPlayerOwner, kItemA), 5);
        EXPECT_EQ(rig.mHooks->itemCount(kPapyrusPlayerReference, kItemA), 5);
        EXPECT_EQ(rig.mHooks->removeItem(kPapyrusPlayerReference, kItemA, 9), 5) << "fewer than asked";
        EXPECT_EQ(rig.mModel.count(inventory::kPlayerOwner, kItemA), 0);
        ASSERT_EQ(rig.mEvents.size(), 2u);
        EXPECT_EQ(rig.mEvents[0], (ModelRig::Event{ 0, kPapyrusPlayerReference, kItemA, 5 }));
        EXPECT_EQ(rig.mEvents[1], (ModelRig::Event{ kPapyrusPlayerReference, 0, kItemA, 5 }));
    }

    TEST(SkyrimPapyrusNatives2, an_owner_the_model_lacks_is_registered_through_the_pack)
    {
        ModelRig rig;
        EXPECT_EQ(rig.mHooks->itemCount(kChest, kItemA), 0);
        EXPECT_EQ(rig.mEnsured, 1);
        ASSERT_TRUE(rig.mHooks->addItem(kChest, kItemA, 2));
        EXPECT_EQ(rig.mEnsured, 1) << "once";
        EXPECT_EQ(rig.mModel.count(kChest, kItemA), 2);
    }

    TEST(SkyrimPapyrusNatives2, transfer_and_remove_all_move_between_owners)
    {
        ModelRig rig;
        ASSERT_TRUE(rig.mHooks->addItem(kChest, kItemA, 4));
        ASSERT_TRUE(rig.mHooks->addItem(kChest, kItemB, 1));
        rig.mEvents.clear();
        ASSERT_TRUE(rig.mHooks->transferItem(kChest, kPapyrusPlayerReference, kItemA, 3));
        EXPECT_EQ(rig.mModel.count(inventory::kPlayerOwner, kItemA), 3);
        EXPECT_EQ(rig.mEvents.back(), (ModelRig::Event{ kChest, kPapyrusPlayerReference, kItemA, 3 }));
        EXPECT_EQ(rig.mHooks->removeAllItems(kChest, kPapyrusPlayerReference), 2);
        EXPECT_EQ(rig.mModel.count(inventory::kPlayerOwner, kItemB), 1);
        EXPECT_EQ(rig.mHooks->removeAllItems(kPapyrusPlayerReference, 0), 5) << "to nowhere";
        EXPECT_EQ(rig.mModel.stacks(inventory::kPlayerOwner).size(), 0u);
    }

    TEST(SkyrimPapyrusNatives2, bad_requests_change_nothing)
    {
        ModelRig rig;
        EXPECT_FALSE(rig.mHooks->addItem(0, kItemA, 1));
        EXPECT_FALSE(rig.mHooks->addItem(kPapyrusPlayerReference, kItemA, 0));
        EXPECT_FALSE(rig.mHooks->addItem(kPapyrusPlayerReference, 0, 1));
        EXPECT_FALSE(rig.mHooks->addItem(kPapyrusPlayerReference, kItemA, 2000000));
        EXPECT_EQ(rig.mHooks->removeItem(kPapyrusPlayerReference, kItemA, 1), 0);
        EXPECT_FALSE(rig.mHooks->transferItem(kChest, kChest, kItemA, 1));
        EXPECT_TRUE(rig.mEvents.empty());
    }

    TEST(SkyrimPapyrusNatives2, the_model_reports_transfers_and_consumes_and_no_other_change)
    {
        ModelRig rig;
        ASSERT_EQ(rig.mModel.add(inventory::kPlayerOwner, kItemA, 3), inventory::TransferStatus::Ok);
        ASSERT_TRUE(rig.mModel.ensureOwner(77, {}));
        ASSERT_EQ(rig.mModel.transfer(inventory::kPlayerOwner, 77, kItemA, 1), inventory::TransferStatus::Ok);
        ASSERT_EQ(rig.mModel.consume(77, kItemA, 1), inventory::TransferStatus::Ok);
        ASSERT_EQ(rig.mEvents.size(), 3u);
        EXPECT_EQ(rig.mEvents[1], (ModelRig::Event{ kPapyrusPlayerReference, 77, kItemA, 1 }));
        EXPECT_EQ(rig.mEvents[2], (ModelRig::Event{ 77, 0, kItemA, 1 }));
        EXPECT_EQ(rig.mModel.add(99, kItemA, 1), inventory::TransferStatus::UnknownOwner);
        EXPECT_EQ(rig.mModel.consume(77, kItemA, 5), inventory::TransferStatus::NotEnough);
        EXPECT_EQ(rig.mEvents.size(), 3u) << "a refusal tells nobody";
    }

    // ---- the combat bridge ------------------------------------------------------------------------------------------

    class RecordingWriter final : public openeden::sdk::game::CombatRequestWriter
    {
    public:
        bool reportDamage(const openeden::sdk::game::DamageReport&) override
        {
            ++mDamage;
            return mAccept;
        }
        bool reportDeath(openeden::sdk::game::ActorHandle actor) override
        {
            mDeaths.push_back(actor.reference);
            return mAccept;
        }
        bool playSound(const openeden::sdk::game::SoundRequest&) override { return ++mSounds > 0; }
        bool hideEffect(std::uint32_t) override { return ++mHides > 0; }
        bool setClipSpeed(openeden::sdk::game::CombatSubject, float) override { return ++mSpeeds > 0; }
        int mDamage = 0;
        int mSounds = 0;
        int mHides = 0;
        int mSpeeds = 0;
        bool mAccept = true;
        std::vector<std::uint32_t> mDeaths;
    };

    openeden::sdk::game::ActorHandle handle(FormId reference)
    {
        openeden::sdk::game::ActorHandle h;
        h.reference = reference;
        h.session = 1;
        return h;
    }

    struct BridgeRig
    {
        BridgeRig()
        {
            mBridge.setSinks(
                [this](FormId v, FormId a, bool b, bool bash) {
                    mHits.push_back(
                        std::to_string(v) + ":" + std::to_string(a) + (b ? ":b" : ":-") + (bash ? "b" : "-"));
                },
                [this](FormId v, FormId k) { mDeaths.push_back(std::to_string(v) + ":" + std::to_string(k)); },
                [this](FormId actor, openeden::sdk::game::CombatRequestWriter& w) {
                    mKilled.push_back(actor);
                    (void)w.reportDeath(handle(actor));
                });
        }
        ScriptCombatBridge mBridge;
        RecordingWriter mInner;
        std::vector<std::string> mHits;
        std::vector<std::string> mDeaths;
        std::vector<FormId> mKilled;
    };

    TEST(SkyrimPapyrusNatives2, a_damage_report_that_went_through_is_an_OnHit_and_names_the_player)
    {
        BridgeRig rig;
        ScriptEventWriter writer(rig.mInner, rig.mBridge);
        openeden::sdk::game::DamageReport report;
        report.attackerIsPlayer = true;
        report.victim = handle(kActor);
        report.amount = 5.f;
        report.blocked = true;
        EXPECT_TRUE(writer.reportDamage(report));
        ASSERT_EQ(rig.mHits.size(), 1u);
        EXPECT_EQ(rig.mHits[0], std::to_string(kActor) + ":20:b-");
        rig.mInner.mAccept = false;
        EXPECT_FALSE(writer.reportDamage(report));
        EXPECT_EQ(rig.mHits.size(), 1u) << "a report the core refused is not an event";
    }

    TEST(SkyrimPapyrusNatives2, a_death_report_names_the_last_attacker_once)
    {
        BridgeRig rig;
        ScriptEventWriter writer(rig.mInner, rig.mBridge);
        openeden::sdk::game::DamageReport report;
        report.attacker = handle(kOther);
        report.victim = handle(kActor);
        report.amount = 5.f;
        (void)writer.reportDamage(report);
        EXPECT_TRUE(writer.reportDeath(handle(kActor)));
        ASSERT_EQ(rig.mDeaths.size(), 1u);
        EXPECT_EQ(rig.mDeaths[0], std::to_string(kActor) + ":" + std::to_string(kOther));
        EXPECT_TRUE(writer.reportDeath(handle(kActor)));
        EXPECT_EQ(rig.mDeaths.back(), std::to_string(kActor) + ":0") << "the killer was spent";
    }

    TEST(SkyrimPapyrusNatives2, queued_kills_start_inside_the_next_update_and_are_reported)
    {
        BridgeRig rig;
        ScriptEventWriter writer(rig.mInner, rig.mBridge);
        EXPECT_TRUE(rig.mBridge.queueKill(kActor));
        EXPECT_TRUE(rig.mBridge.queueKill(kActor)) << "twice is once";
        EXPECT_FALSE(rig.mBridge.queueKill(0));
        EXPECT_EQ(rig.mBridge.pendingKills(), 1u);
        EXPECT_TRUE(rig.mInner.mDeaths.empty()) << "nothing outside the update";
        rig.mBridge.drain(writer);
        ASSERT_EQ(rig.mKilled.size(), 1u);
        ASSERT_EQ(rig.mInner.mDeaths.size(), 1u);
        ASSERT_EQ(rig.mDeaths.size(), 1u);
        EXPECT_EQ(rig.mBridge.pendingKills(), 0u);
        rig.mBridge.drain(writer);
        EXPECT_EQ(rig.mKilled.size(), 1u);
    }

    TEST(SkyrimPapyrusNatives2, the_kill_queue_is_bounded)
    {
        ScriptCombatBridge bridge;
        for (std::size_t i = 1; i <= ScriptCombatBridge::kMaxKills; ++i)
            ASSERT_TRUE(bridge.queueKill(static_cast<FormId>(i)));
        EXPECT_FALSE(bridge.queueKill(0x7000));
    }

    TEST(SkyrimPapyrusNatives2, the_wrapper_forwards_the_calls_it_does_not_watch)
    {
        BridgeRig rig;
        ScriptEventWriter writer(rig.mInner, rig.mBridge);
        EXPECT_TRUE(writer.playSound({}));
        EXPECT_TRUE(writer.hideEffect(3));
        EXPECT_TRUE(writer.setClipSpeed(openeden::sdk::game::CombatSubject::ofPlayer(), 1.f));
        EXPECT_EQ(rig.mInner.mSounds, 1);
        EXPECT_EQ(rig.mInner.mHides, 1);
        EXPECT_EQ(rig.mInner.mSpeeds, 1);
    }

    TEST(SkyrimPapyrusNatives2, a_bridge_with_no_sinks_changes_nothing)
    {
        ScriptCombatBridge bridge;
        RecordingWriter inner;
        ScriptEventWriter writer(inner, bridge);
        EXPECT_FALSE(bridge.active());
        openeden::sdk::game::DamageReport report;
        report.victim = handle(kActor);
        EXPECT_TRUE(writer.reportDamage(report));
        EXPECT_TRUE(writer.reportDeath(handle(kActor)));
        EXPECT_EQ(inner.mDamage, 1);
        EXPECT_EQ(inner.mDeaths.size(), 1u);
    }
}

// ---- the demo script for the in-game check (tools/neutral_session_check/papyrusdemo.sh, DEMO_DIR) -------------------
// Not a test: with OPENEDEN_PAPYRUS_DEMO_DIR_198 set it writes scripts/oedemotick.pex there, a generated script (no
// game asset) that the demo plugin attaches to its activators. Four script seconds after it starts it gives the player
// 50 gold, takes 20 away, damages the player's stamina, reads his health and says what it saw with Debug.Trace and,
// once, with Debug.Notification.
#include <cstdlib>
#include <filesystem>
#include <fstream>

namespace
{
    TEST(SkyrimPapyrusNatives2Demo, writes_the_demo_script_when_asked)
    {
        const char* const dir = std::getenv("OPENEDEN_PAPYRUS_DEMO_DIR_198");
        if (dir == nullptr || *dir == '\0')
            GTEST_SKIP() << "OPENEDEN_PAPYRUS_DEMO_DIR_198 is not set";
        ScriptBuilder s("OEDemoTick");
        s.parent("ObjectReference");
        s.var("done", "Bool");
        s.autoProperty("Limit", "Int");
        s.autoProperty("Label", "String");
        s.function("OnInit")
            .local("nn", "None")
            .callMethod("RegisterForSingleUpdate", Id("self"), Id("nn"), { Flt(4.0f) });
        auto& f = s.function("OnUpdate");
        f.local("nn", "None").local("p", "Actor").local("gold", "Form").local("n", "Int").local("v", "Float");
        f.local("s", "String").local("m", "String").local("d", "Bool").local("run", "Bool");
        f.local("hf", "Form").local("h", "Actor");
        f.op(PexAsm::Op::CompareEq, { Id("run"), Id("done"), Bool(false) });
        f.jmpt(Id("run"), 2);
        f.ret(Nil());
        f.assign(Id("done"), Bool(true));
        f.callStatic("Game", "GetPlayer", Id("p"));
        f.callStatic("Game", "GetFormFromFile", Id("gold"), { Int(15), Str("Skyrim.esm") });
        const auto say = [&f](const char* label, const char* variable, bool isFloat) {
            f.op(PexAsm::Op::Cast, { Id("s"), Id(variable) });
            f.op(PexAsm::Op::StrCat, { Id("m"), Str(label), Id("s") });
            f.callStatic("Debug", "Trace", Id("nn"), { Id("m"), Int(0) });
            (void)isFloat;
        };
        f.callMethod("GetItemCount", Id("p"), Id("n"), { Id("gold") });
        say("OEDEMO198 gold before ", "n", false);
        f.callMethod("AddItem", Id("p"), Id("nn"), { Id("gold"), Int(50), Bool(false) });
        f.callMethod("GetItemCount", Id("p"), Id("n"), { Id("gold") });
        say("OEDEMO198 gold after add ", "n", false);
        f.callMethod("RemoveItem", Id("p"), Id("nn"), { Id("gold"), Int(20), Bool(false), Nil() });
        f.callMethod("GetItemCount", Id("p"), Id("n"), { Id("gold") });
        say("OEDEMO198 gold after remove ", "n", false);
        f.callMethod("GetActorValue", Id("p"), Id("v"), { Str("Health") });
        say("OEDEMO198 health ", "v", true);
        f.callMethod("GetActorValue", Id("p"), Id("v"), { Str("Stamina") });
        say("OEDEMO198 stamina ", "v", true);
        f.callMethod("DamageActorValue", Id("p"), Id("nn"), { Str("Stamina"), Flt(10.0f) });
        f.callMethod("GetActorValue", Id("p"), Id("v"), { Str("Stamina") });
        say("OEDEMO198 stamina after damage ", "v", true);
        f.callMethod("IsDead", Id("p"), Id("d"));
        say("OEDEMO198 dead ", "d", false);
        f.callStatic("Debug", "Notification", Id("nn"), { Str("OEDEMO198 hello from a script") });
        // Hulda (placement 0x1A66E of the Bannered Mare): is she dead, kill her, is she dead
        f.callStatic("Game", "GetForm", Id("hf"), { Int(0x1A66E) });
        f.op(PexAsm::Op::Cast, { Id("h"), Id("hf") });
        f.callMethod("IsDead", Id("h"), Id("d"));
        say("OEDEMO198 hulda dead before ", "d", false);
        f.callMethod("Kill", Id("h"), Id("nn"), { Nil() });
        f.callMethod("IsDead", Id("h"), Id("d"));
        say("OEDEMO198 hulda dead after Kill ", "d", false);
        const std::vector<std::uint8_t> bytes = s.build(PapyrusTest::optionsFor(PexAsm::Target::SkyrimSE)).mBytes;
        const std::filesystem::path folder = std::filesystem::path(dir) / "scripts";
        std::filesystem::create_directories(folder);
        std::ofstream out(folder / "oedemotick.pex", std::ios::binary);
        out.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        ASSERT_TRUE(out.good());
    }
}
