// Task 0317 (Hearthfire house building, `[extension.skyrimse] house building`): the form list natives, the scripts of
// unplaced item forms, and the House bench of the crafting service. Generated scripts, a generated plugin and a real
// inventory; no game data. Written AFTER the code (the red evidence is by mutation, see the task's stage log).
#include <game_skyrimse/crafting.hpp>
#include <game_skyrimse/crafting_service.hpp>
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/quest.hpp>
#include <game_skyrimse/vmad.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <vector>

#include "../contentsource/pluginbuilder.hpp"
#include "../papyrus/papyrustestutil.hpp"
#include "testskyrimsecraftingdata.hpp"

namespace
{
    using namespace ContentTests;
    using namespace game_skyrimse;
    using PexAsm::Bool;
    using PexAsm::Id;
    using PexAsm::Int;
    using PexAsm::Nil;
    using PexAsm::ScriptBuilder;

    constexpr FormId kQuest = 0x01000800;
    constexpr FormId kActor = 0x01000A01;
    constexpr FormId kOther = 0x01000A02;
    constexpr FormId kListForm = 0x01000C01;
    constexpr FormId kPartA = 0x01000B01;
    constexpr FormId kPartB = 0x01000B02;
    constexpr FormId kPartC = 0x01000B03;
    constexpr FormId kBase = 0x01000B00;

    LoadOrder order2()
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
        bool formListEntries(FormId list, std::vector<FormId>& out) const override
        {
            out.clear();
            const auto it = mLists.find(list);
            if (it == mLists.end() || !mListsReady)
                return false;
            out = it->second;
            return true;
        }
        bool baseScripts(FormId form, std::vector<vmad::Script>& out) override
        {
            out.clear();
            const auto it = mBaseScripts.find(form);
            if (it == mBaseScripts.end())
                return false;
            out = it->second;
            return true;
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
        std::map<FormId, std::vector<FormId>> mLists;
        std::map<FormId, std::vector<vmad::Script>> mBaseScripts;
        bool mListsReady = true;
    };

    class FakeHooks final : public PapyrusWorldHooks
    {
    public:
        std::int32_t removeItem(FormId owner, FormId item, std::int32_t count) override
        {
            std::int32_t& have = mItems[{ owner, item }];
            const std::int32_t take = std::min(have, count);
            have -= take;
            mLog.push_back("remove " + std::to_string(item) + " x" + std::to_string(take));
            return take;
        }
        std::int32_t itemCount(FormId owner, FormId item) override { return mItems[{ owner, item }]; }
        bool transferItem(FormId from, FormId to, FormId item, std::int32_t count) override
        {
            mItems[{ from, item }] -= count;
            mItems[{ to, item }] += count;
            mLog.push_back("transfer " + std::to_string(item) + " x" + std::to_string(count));
            return true;
        }
        std::int32_t held(FormId owner, FormId item) { return mItems[{ owner, item }]; }
        std::map<std::pair<FormId, FormId>, std::int32_t> mItems;
        std::vector<std::string> mLog;
    };

    void addClasses(FakeData& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("RemoveItem")
            .native()
            .param("akItemToRemove", "Form")
            .param("aiCount", "Int")
            .param("abSilent", "Bool")
            .param("akOtherContainer", "ObjectReference");
        data.add(ref);
        ScriptBuilder list("FormList");
        list.parent("Form");
        list.function("GetSize", "Int").native();
        list.function("GetAt", "Form").native().param("aiIndex", "Int");
        list.function("Find", "Int").native().param("apForm", "Form");
        list.function("HasForm", "Bool").native().param("apForm", "Form");
        data.add(list);
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

    ScriptBuilder probeScript()
    {
        ScriptBuilder s("Probe");
        s.parent("ObjectReference");
        s.autoProperty("List", "FormList");
        s.autoProperty("Item", "Form");
        s.autoProperty("Other", "ObjectReference");
        s.autoProperty("Peer", "ObjectReference");
        s.autoProperty("Big", "Int[]");
        s.var("peerCount", "Int");
        s.var("sample", "Part"); // the string table names the part's class, as the game's building script's does
        s.var("size", "Int");
        s.var("found", "Int");
        s.var("has", "Bool");
        s.var("atId", "Int");
        s.function("Size")
            .local("v", "Int")
            .callMethod("GetSize", Id("::List_var"), Id("v"))
            .assign(Id("size"), Id("v"));
        s.function("At1")
            .local("f", "Form")
            .local("v", "Int")
            .callMethod("GetAt", Id("::List_var"), Id("f"), { Int(1) })
            .callMethod("GetFormID", Id("f"), Id("v"))
            .assign(Id("atId"), Id("v"));
        s.function("At9")
            .local("f", "Form")
            .local("v", "Int")
            .callMethod("GetAt", Id("::List_var"), Id("f"), { Int(9) })
            .assign(Id("atId"), Int(-5));
        s.function("FindItem")
            .local("v", "Int")
            .callMethod("Find", Id("::List_var"), Id("v"), { Id("::Item_var") })
            .assign(Id("found"), Id("v"));
        s.function("HasItem")
            .local("v", "Bool")
            .callMethod("HasForm", Id("::List_var"), Id("v"), { Id("::Item_var") })
            .assign(Id("has"), Id("v"));
        // a property of the script attached to the form Peer holds (the game's trigger reads the building quest's flag)
        s.function("ReadPeer")
            .local("v", "Int")
            .op(28, { Id("Count"), Id("::Peer_var"), Id("v") })
            .assign(Id("peerCount"), Id("v"));
        s.function("WritePeer").op(29, { Id("Count"), Id("::Peer_var"), Int(42) });
        s.function("RemoveList")
            .local("n", "None")
            .callMethod("RemoveItem", Id("self"), Id("n"), { Id("::List_var"), Int(1), Bool(true), Nil() });
        s.function("RemoveListTo")
            .local("n", "None")
            .callMethod("RemoveItem", Id("self"), Id("n"), { Id("::List_var"), Int(1), Bool(true), Id("::Other_var") });
        return s;
    }

    // a house part: a script on a MISC that no cell places
    ScriptBuilder partScript()
    {
        ScriptBuilder s("Part");
        s.parent("Form");
        s.autoProperty("ID", "Int");
        s.var("changed", "Int");
        s.function("OnContainerChanged")
            .param("akNewContainer", "ObjectReference")
            .param("akOldContainer", "ObjectReference")
            .iadd(Id("changed"), Id("changed"), Int(1));
        return s;
    }

    ScriptBuilder counterScript()
    {
        ScriptBuilder s("Counter");
        s.parent("ObjectReference");
        s.autoProperty("Count", "Int", Int(7));
        return s;
    }

    vmad::Script partOn()
    {
        vmad::Script s;
        s.mName = "Part";
        return s;
    }

    struct Rig
    {
        explicit Rig(bool formScripts)
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.add(partScript());
            mData.add(counterScript());
            mData.mClasses[kQuest] = "Quest";
            mData.mClasses[kActor] = "ObjectReference";
            mData.mClasses[kOther] = "ObjectReference";
            mData.mClasses[kListForm] = "FormList";
            mData.mClasses[kPartA] = "Form";
            mData.mLists[kListForm] = { kPartA, kPartB, kPartC };
            mData.mBaseScripts[kPartA] = { partOn() };
            mData.mBaseScripts[kPartB] = { partOn() };
            PapyrusHostOptions options;
            options.mFormScripts = formScripts;
            mHost = std::make_unique<PapyrusHost>(mData, options);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            Bytes bytes = tes4Header(1.7f, 0, "A", "B", 1, { { "Skyrim.esm", 0 } }, 24, 44);
            Bytes quest;
            quest.append(sub4("EDID", Bytes().zstring("Q1")));
            quest.append(sub4("DNAM", Bytes().u16(QuestFlags::StartGameEnabled).u8(30).u8(0).u32(0).u32(6)));
            quest.append(sub4("NEXT", Bytes()));
            bytes.append(group4(Content::fourCC("QUST"), 0, record4("QUST", 0u, kQuest, quest)));
            QuestRecords parsed;
            Content::Error error;
            EXPECT_TRUE(parseQuestRecordsBytes(bytes.str(), "test.esp", order2(), QuestLimits(), parsed, error));
            std::unique_ptr<Content::Plugin> plugin = Content::Plugin::open(
                std::make_unique<std::istringstream>(bytes.str(), std::ios::binary), "test.esp", error);
            mIndex.add(std::move(parsed), FormIdResolver(plugin->header(), "test.esp", order2()));
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
        void attach()
        {
            vmad::Script s;
            s.mName = "Probe";
            s.mProperties = { objProp("List", kListForm), objProp("Item", kPartB), objProp("Other", kOther),
                objProp("Peer", kOther) };
            ASSERT_EQ(mHost->attachReference(kActor, kBase, false, { std::move(s) }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kActor, "Probe", function);
            tick();
            return ok;
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kActor, "Probe", name); }

        FakeData mData;
        FakeHooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        QuestIndex mIndex;
        QuestState mState;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    // ---- form lists ------------------------------------------------------------------------------------------------

    TEST(SkyrimHouseFormList, sizeAtFindAndHasFormAnswerFromTheListInItsOwnOrder)
    {
        Rig rig(false);
        rig.attach();
        ASSERT_TRUE(rig.call("Size"));
        EXPECT_EQ(rig.number("size"), 3.0);
        ASSERT_TRUE(rig.call("At1"));
        EXPECT_EQ(rig.number("atId"), static_cast<double>(kPartB));
        ASSERT_TRUE(rig.call("FindItem"));
        EXPECT_EQ(rig.number("found"), 1.0) << "the index is the record's own order (every EnableList[i] hangs on it)";
        ASSERT_TRUE(rig.call("HasItem"));
        EXPECT_EQ(rig.number("has"), 1.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimHouseFormList, aFormOutsideTheListIsMinusOneAndAnIndexPastTheEndIsNone)
    {
        Rig rig(false);
        rig.mData.mLists[kListForm] = { kPartA, kPartC };
        rig.attach();
        ASSERT_TRUE(rig.call("FindItem"));
        EXPECT_EQ(rig.number("found"), -1.0);
        ASSERT_TRUE(rig.call("HasItem"));
        EXPECT_EQ(rig.number("has"), 0.0);
        ASSERT_TRUE(rig.call("At9"));
        EXPECT_EQ(rig.number("atId"), -5.0) << "no error: GetAt past the end answers None";
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimHouseFormList, aListThatIsNotReadYetIsEmptyAndSaidOnce)
    {
        Rig rig(false);
        rig.mData.mListsReady = false;
        rig.attach();
        ASSERT_TRUE(rig.call("Size"));
        ASSERT_TRUE(rig.call("Size"));
        EXPECT_EQ(rig.number("size"), 0.0);
        std::size_t said = 0;
        for (const std::string& line : rig.mLogs)
            said += line.find("before its data was read") != std::string::npos ? 1 : 0;
        EXPECT_EQ(said, 1u);
        rig.mData.mListsReady = true;
        ASSERT_TRUE(rig.call("Size"));
        EXPECT_EQ(rig.number("size"), 3.0) << "asked again once the data is there";
    }

    TEST(SkyrimHouseFormList, removeItemWithAListMovesUpToCountOfEachFormTheOwnerHolds)
    {
        Rig rig(false);
        rig.attach();
        rig.mHooks.mItems[{ kActor, kPartA }] = 5;
        rig.mHooks.mItems[{ kActor, kPartC }] = 1;
        ASSERT_TRUE(rig.call("RemoveListTo"));
        EXPECT_EQ(rig.mHooks.held(kOther, kPartA), 1) << "one of each, not the whole stack";
        EXPECT_EQ(rig.mHooks.held(kOther, kPartC), 1);
        EXPECT_EQ(rig.mHooks.held(kOther, kPartB), 0) << "nothing held, nothing moved";
        EXPECT_EQ(rig.mHooks.held(kActor, kPartA), 4);
        ASSERT_TRUE(rig.call("RemoveList"));
        EXPECT_EQ(rig.mHooks.held(kActor, kPartA), 3) << "without a container the items are removed";
        EXPECT_EQ(rig.mHooks.held(kActor, kPartC), 0);
    }

    // ---- the house lists hold 224 references ----------------------------------------------------------------------

    vmad::Property bigArray(std::size_t n)
    {
        vmad::Property p;
        p.mName = "Big";
        p.mArray = true;
        p.mType = 3;
        for (std::size_t i = 0; i < n; ++i)
        {
            vmad::Value v;
            v.mKind = vmad::ValueKind::Int;
            v.mInteger = static_cast<std::int32_t>(i);
            p.mElements.push_back(v);
        }
        return p;
    }

    TEST(SkyrimHouseArrays, anArrayOf224ElementsIsRefusedByDefaultAndKeptWithTheOption)
    {
        for (const bool on : { false, true })
        {
            Rig rig(on);
            if (on)
                rig.mHost->setFormScripts(true); // the host option the pack sets when the setting is on
            vmad::Script s;
            s.mName = "Probe";
            s.mProperties = { bigArray(224) };
            ASSERT_EQ(rig.mHost->attachReference(kActor, kBase, false, { std::move(s) }), 1u);
            EXPECT_EQ(rig.mHost->counters().mPropertiesRejected, on ? 0u : 1u) << "option " << on;
        }
    }

    // ---- a property of the script attached to a form value --------------------------------------------------------

    TEST(SkyrimHouseFormProperty, aPropertyOfAFormValueIsReadAndWrittenOnTheScriptAttachedToThatForm)
    {
        Rig rig(false);
        vmad::Script counter;
        counter.mName = "Counter";
        ASSERT_EQ(rig.mHost->attachReference(kOther, kBase, false, { std::move(counter) }), 1u);
        rig.attach();
        ASSERT_TRUE(rig.call("ReadPeer"));
        EXPECT_EQ(rig.number("peerCount"), 7.0)
            << "Peer holds a form value of class ObjectReference, which has no Count";
        ASSERT_TRUE(rig.call("WritePeer"));
        ASSERT_TRUE(rig.call("ReadPeer"));
        EXPECT_EQ(rig.number("peerCount"), 42.0);
        EXPECT_EQ(rig.mHost->numberVariable(kOther, "Counter", "Count"), 42.0);
        EXPECT_TRUE(rig.mErrors.empty());
    }

    // ---- scripts on unplaced forms ---------------------------------------------------------------------------------

    TEST(SkyrimHouseFormScripts, anItemAddedToTheInventoryHearsOnContainerChangedWhenTheOptionIsOn)
    {
        Rig rig(true);
        EXPECT_FALSE(rig.mHost->hasInstances(kPartB));
        rig.mHost->onItemMoved(0, 0x14, kPartB, 1);
        rig.tick();
        EXPECT_TRUE(rig.mHost->hasInstances(kPartB));
        EXPECT_EQ(rig.mHost->numberVariable(kPartB, "Part", "changed"), 1.0);
        rig.mHost->onItemMoved(0, 0x14, kPartB, 1);
        rig.tick();
        EXPECT_EQ(rig.mHost->numberVariable(kPartB, "Part", "changed"), 2.0) << "attached once";
    }

    TEST(SkyrimHouseFormScripts, withTheOptionOffAnUnplacedFormGetsNoScript)
    {
        Rig rig(false);
        rig.mHost->onItemMoved(0, 0x14, kPartA, 1);
        rig.tick();
        EXPECT_FALSE(rig.mHost->hasInstances(kPartA));
    }

    TEST(SkyrimHouseFormScripts, aFormTheListHandsOutGetsItsScriptAndOneWithNoScriptNone)
    {
        Rig rig(true);
        rig.attach();
        EXPECT_FALSE(rig.mHost->hasInstances(kPartB));
        ASSERT_TRUE(rig.call("At1")); // kPartB
        EXPECT_TRUE(rig.mHost->hasInstances(kPartB));
        EXPECT_FALSE(rig.mHost->hasInstances(kPartC)) << "a record with no script has no instance";
        EXPECT_TRUE(rig.mErrors.empty());
    }

    TEST(SkyrimHouseFormScripts, aSaveMadeAfterTheAttachIsStillRestorable)
    {
        Rig rig(true);
        rig.mHost->onItemMoved(0, 0x14, kPartA, 1);
        rig.tick();
        const openeden::script::SaveBlob blob = rig.mHost->serialiseState();
        EXPECT_TRUE(rig.mHost->restoreState(blob)) << "the key of an unplaced form's script is something the game has";
        EXPECT_EQ(rig.mHost->numberVariable(kPartA, "Part", "changed"), 1.0);
    }

    // ---- the House bench -------------------------------------------------------------------------------------------

    using namespace CraftingTestData;
    constexpr FormId kKwDrafting = kSelf | 0x1001;
    constexpr FormId kKwCarpenter = kSelf | 0x1002;
    constexpr FormId kKwRoom = kSelf | 0x1003;
    constexpr FormId kKwPlain = kSelf | 0x1004;
    constexpr FormId kFurnDraft = kSelf | 0x1101;
    constexpr FormId kFurnBench = kSelf | 0x1102;
    constexpr FormId kFurnStool = kSelf | 0x1103;
    constexpr FormId kLayout = kSelf | 0x1201;
    constexpr FormId kFoundation = kSelf | 0x1202;
    constexpr FormId kLogs = kSelf | 0x1203;
    constexpr FormId kRecLayout = kSelf | 0x1301;
    constexpr FormId kRecFoundation = kSelf | 0x1302;
    constexpr FormId kRecFrame = kSelf | 0x1303;

    Bytes ctdaItemCount(FormId item, int compare, float value)
    {
        Bytes d;
        d.u8(static_cast<std::uint8_t>(compare << 5)).u8(0).u8(0).u8(0);
        d.f32(value).u16(47).u16(0).u32(item).u32(0).u32(0).u32(0).u32(0);
        return sub4("CTDA", d);
    }

    Bytes houseCobj(FormId id, FormId result, FormId bench, std::vector<std::pair<FormId, int>> items,
        std::vector<std::tuple<FormId, int, float>> conditions)
    {
        Bytes d;
        d.append(sub4("COCT", Bytes().u32(static_cast<std::uint32_t>(items.size()))));
        for (const auto& [item, n] : items)
            d.append(sub4("CNTO", Bytes().u32(item).u32(static_cast<std::uint32_t>(n))));
        for (const auto& [item, compare, value] : conditions)
            d.append(ctdaItemCount(item, compare, value));
        d.append(sub4("CNAM", Bytes().u32(result)));
        d.append(sub4("BNAM", Bytes().u32(bench)));
        d.append(sub4("NAM1", Bytes().u16(1)));
        return record4("COBJ", 0, id, d);
    }

    Bytes housePlugin()
    {
        Bytes all = tes4Header(1.7f, 0, "A", "B", 12, { { "Skyrim.esm", 0 } }, 24, 44);
        Bytes misc;
        for (const FormId id : { kLayout, kFoundation, kLogs })
            misc.append(record4("MISC", 0, id, sub4("DATA", Bytes().u32(1).f32(1.f))));
        misc.append(record4("MISC", 0, 0x0000000F, sub4("DATA", Bytes().u32(1).f32(0.f))));
        all.append(grp("MISC", misc));
        Bytes kw;
        kw.append(record4("KYWD", 0, kKwDrafting, edid("BYOHBuildingDrafting")));
        kw.append(record4("KYWD", 0, kKwCarpenter, edid("BYOHBuildingCarpenter")));
        kw.append(record4("KYWD", 0, kKwRoom, edid("BYOHBuildingInteriorPart001Fireplace01")));
        kw.append(record4("KYWD", 0, kKwPlain, edid("VendorItemFood")));
        all.append(grp("KYWD", kw));
        Bytes fu;
        fu.append(furn(kFurnDraft, "TestDraftingTable", { kKwDrafting }));
        fu.append(furn(kFurnBench, "TestWorkbench", { kKwCarpenter, kKwRoom }));
        fu.append(furn(kFurnStool, "TestStool", { kKwPlain }));
        all.append(grp("FURN", fu));
        Bytes co;
        // the layout: one at most; the foundation: needs the layout and no foundation yet; the frame: needs logs
        co.append(houseCobj(kRecLayout, kLayout, kKwDrafting, {}, { { kLayout, 0, 0.f } }));
        co.append(
            houseCobj(kRecFoundation, kFoundation, kKwCarpenter, {}, { { kLayout, 0, 1.f }, { kFoundation, 0, 0.f } }));
        co.append(houseCobj(kRecFrame, kFoundation, kKwCarpenter, { { kLogs, 2 } }, { { kLayout, 0, 1.f } }));
        all.append(grp("COBJ", co));
        return all;
    }

    struct HouseWorld
    {
        std::filesystem::path mDir, mPath;
        std::unique_ptr<inventory::InventoryService> mInventory;
        std::unique_ptr<crafting::CraftingService> mCrafting;
        std::vector<std::string> mLog;
        std::vector<std::pair<std::string, float>> mSkillUsed;
        std::vector<std::pair<std::uint64_t, FormId>> mOpened;
        std::vector<std::tuple<std::uint64_t, std::uint64_t, FormId>> mMoves;

        void build(bool house)
        {
            mDir = std::filesystem::temp_directory_path() / "oe_house_building";
            std::filesystem::create_directories(mDir);
            mPath = mDir / "test.esp";
            const Bytes all = housePlugin();
            std::ofstream(mPath, std::ios::binary)
                .write(all.str().data(), static_cast<std::streamsize>(all.str().size()));
            inventory::Hooks ih;
            ih.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            ih.mName = [](FormId) { return std::string(); };
            mInventory = std::make_unique<inventory::InventoryService>(std::move(ih), 100.f, 1);
            crafting::CraftingHooks hooks;
            hooks.mInputs = [this](inventory::Inputs& out) { return inputs(out); };
            hooks.mName = [](FormId) { return std::string(); };
            hooks.mEffectInfo = [](FormId) { return crafting::EffectInfo(); };
            hooks.mSkill = [](std::string_view) { return 15.f; };
            hooks.mSkillUsed = [this](std::string_view s, float a) { mSkillUsed.emplace_back(std::string(s), a); };
            hooks.mLog = [this](const std::string& l) { mLog.push_back(l); };
            hooks.mBenchOpened = [this](std::uint64_t placement, FormId f) { mOpened.emplace_back(placement, f); };
            mCrafting = std::make_unique<crafting::CraftingService>(
                *mInventory, std::move(hooks), crafting::CraftingSettings(), false, false);
            mCrafting->setHouseBuilding(house);
            mInventory->model().setChangeSink([this](std::uint64_t from, std::uint64_t to, FormId item, std::int32_t) {
                mMoves.emplace_back(from, to, item);
            });
            for (int i = 0; i < 4000 && !(mInventory->poll() && (mCrafting->poll(), mCrafting->ready())); ++i)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            ASSERT_TRUE(mCrafting->ready());
        }
        bool inputs(inventory::Inputs& out)
        {
            out.mPaths = { mPath };
            std::string err;
            (void)out.mOrder.add("Skyrim.esm", false, err);
            (void)out.mOrder.add("test.esp", false, err);
            return true;
        }
        void open(FormId furniture)
        {
            mCrafting->openBench(0x5001, furniture, "Bench");
            mCrafting->poll();
        }
        std::int32_t held(FormId item) const { return mInventory->model().count(inventory::kPlayerOwner, item); }
        std::set<FormId> visible() const
        {
            std::set<FormId> out;
            for (const crafting::Recipe* r : mCrafting->visibleRecipes())
                out.insert(r->mId);
            return out;
        }
        ~HouseWorld()
        {
            mCrafting.reset();
            mInventory.reset();
            std::error_code ec;
            if (!mDir.empty())
                std::filesystem::remove_all(mDir, ec);
        }
    };

    TEST(SkyrimHouseBench, theKeywordsOfTheHouseFurnitureMakeAHouseBenchOnlyWhenAskedFor)
    {
        HouseWorld w;
        w.build(true);
        EXPECT_EQ(w.mCrafting->table().benchOfKeyword(kKwDrafting), crafting::BenchKind::House);
        EXPECT_EQ(w.mCrafting->table().benchOfKeyword(kKwPlain), crafting::BenchKind::None);
        EXPECT_TRUE(w.mCrafting->table().benchesOf(kFurnDraft).empty()) << "off by default in the table";
        EXPECT_EQ(w.mCrafting->table().benchesOf(kFurnDraft, true),
            std::vector<crafting::BenchKind>({ crafting::BenchKind::House }));
        EXPECT_TRUE(w.mCrafting->table().furnitureOffers(kFurnBench, kKwRoom));
        EXPECT_FALSE(w.mCrafting->table().furnitureOffers(kFurnDraft, kKwCarpenter));
        EXPECT_EQ(w.mCrafting->benchAnswer(kFurnDraft), "yes");
        EXPECT_EQ(w.mCrafting->benchAnswer(kFurnStool), "no");
    }

    TEST(SkyrimHouseBench, withTheSettingOffTheDraftingTableIsNoBench)
    {
        HouseWorld w;
        w.build(false);
        EXPECT_EQ(w.mCrafting->benchAnswer(kFurnDraft), "no");
        w.open(kFurnDraft);
        EXPECT_EQ(w.mCrafting->session(), crafting::CraftingService::State::Refused);
        EXPECT_TRUE(w.mOpened.empty());
    }

    TEST(SkyrimHouseBench, eachFurnitureOffersTheRecipesOfItsOwnKeywordAndTheConditionsDecide)
    {
        HouseWorld w;
        w.build(true);
        w.open(kFurnDraft);
        ASSERT_EQ(w.mCrafting->session(), crafting::CraftingService::State::Ready);
        EXPECT_EQ(w.visible(), std::set<FormId>({ kRecLayout }))
            << "the workbench's recipes are not the drafting table's";
        w.open(kFurnBench);
        EXPECT_TRUE(w.visible().empty()) << "no layout yet: both workbench recipes need it";
    }

    TEST(SkyrimHouseBench, craftingAPartKeepsItInTheInventoryReportsItAndTrainsNothing)
    {
        HouseWorld w;
        w.build(true);
        w.open(kFurnDraft);
        EXPECT_EQ(w.mCrafting->craft(kRecLayout), crafting::Status::Ok) << "a recipe with no materials is craftable";
        EXPECT_EQ(w.held(kLayout), 1);
        ASSERT_EQ(w.mMoves.size(), 1u) << "the inventory model reports it (OnContainerChanged hangs on this)";
        EXPECT_EQ(std::get<0>(w.mMoves[0]), inventory::InventoryModel::kNoOwner) << "from no container";
        EXPECT_EQ(std::get<1>(w.mMoves[0]), inventory::kPlayerOwner);
        EXPECT_EQ(std::get<2>(w.mMoves[0]), kLayout);
        EXPECT_TRUE(w.mSkillUsed.empty());
        EXPECT_EQ(w.mCrafting->craft(kRecLayout), crafting::Status::UnknownRecipe)
            << "GetItemCount(layout) == 0 no longer holds";
        EXPECT_EQ(w.held(kLayout), 1);
    }

    TEST(SkyrimHouseBench, theWorkbenchRecipesFollowTheTokensThePlayerHolds)
    {
        HouseWorld w;
        w.build(true);
        w.open(kFurnDraft);
        ASSERT_EQ(w.mCrafting->craft(kRecLayout), crafting::Status::Ok);
        w.open(kFurnBench);
        // the foundation recipe (layout 1, foundation 0) is open; the frame recipe needs two logs the player lacks
        EXPECT_EQ(w.visible(), std::set<FormId>({ kRecFoundation, kRecFrame }));
        EXPECT_EQ(w.mCrafting->craft(kRecFrame), crafting::Status::MissingItems);
        EXPECT_EQ(w.mCrafting->craft(kRecFoundation), crafting::Status::Ok);
        EXPECT_EQ(w.held(kFoundation), 1);
        EXPECT_EQ(w.visible(), std::set<FormId>({ kRecFrame })) << "foundation == 0 fails for the first recipe now";
        EXPECT_EQ(w.mCrafting->craft(kRecFoundation), crafting::Status::UnknownRecipe);
        // a recipe of the drafting table is refused at the workbench
        EXPECT_EQ(w.mCrafting->craft(kRecLayout), crafting::Status::NoBench);
    }

    TEST(SkyrimHouseBench, openingAHouseBenchTellsTheGameSoTheWorkbenchScriptRuns)
    {
        HouseWorld w;
        w.build(true);
        w.open(kFurnBench);
        ASSERT_EQ(w.mOpened.size(), 1u);
        EXPECT_EQ(w.mOpened[0].first, 0x5001u);
        EXPECT_EQ(w.mOpened[0].second, kFurnBench);
    }

    // ---- the player's own Hearthfire data (gated) ------------------------------------------------------------------

    TEST(SkyrimHouseBuildingReal, theBuildingMasterListHoldsThePartsInOrderAndTheirScriptsAreReadable)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !std::filesystem::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::filesystem::path data = env;
        std::vector<std::filesystem::path> paths;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
            paths.push_back(data / name);
        DataFolderFiles files(data);
        SkyrimWorldView view({}, &files);
        std::vector<std::string> problems;
        ASSERT_GT(view.load(paths, problems), 0u);
        constexpr FormId kMasterList = 0x0300305F; // BYOHHouseBuildingMasterList
        constexpr FormId kLayoutPart = 0x03003060; // BYOHHousePart001Room1Layout "Small House Layout"
        constexpr FormId kFoundationPart = 0x03003014; // BYOHHousePart002Room1Foundation
        std::vector<FormId> entries;
        for (int spins = 0; spins < 600 && !view.formListEntries(kMasterList, entries); ++spins)
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        ASSERT_GT(entries.size(), 50u) << "the list is not truncated";
        const auto layout = std::find(entries.begin(), entries.end(), kLayoutPart);
        const auto foundation = std::find(entries.begin(), entries.end(), kFoundationPart);
        ASSERT_NE(layout, entries.end());
        ASSERT_NE(foundation, entries.end());
        EXPECT_LT(layout - entries.begin(), foundation - entries.begin());
        EXPECT_EQ(view.scriptClassOf(kMasterList), "FormList");
        const std::vector<vmad::Script>* scripts = view.scriptsOfBase(kFoundationPart);
        ASSERT_NE(scripts, nullptr) << "a MISC that no cell places carries its script";
        ASSERT_FALSE(scripts->empty());
        EXPECT_EQ((*scripts)[0].mName, "BYOHBuildingObjectScript");
        std::int32_t id = -1;
        for (const vmad::Property& p : (*scripts)[0].mProperties)
            if (p.mName == "ID")
                id = p.mValue.mInteger;
        EXPECT_EQ(id, 2) << "the part's own number: the ID GetFormListIndex compares";
    }

    TEST(SkyrimHouseBuildingReal, theCarpentersWorkbenchOffersTheFoundationRecipeWhenTheLayoutIsHeld)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !std::filesystem::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const std::filesystem::path data = env;
        LoadOrder order;
        std::string err;
        crafting::CraftingTable table;
        for (const char* name : { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" })
        {
            ASSERT_TRUE(order.add(name, false, err)) << err;
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / name, error);
            ASSERT_NE(plugin, nullptr) << error.mMessage;
            crafting::CraftingRecords records;
            ASSERT_TRUE(crafting::parseCraftingRecords(*plugin, order, crafting::CraftingLimits(), records));
            table.add(std::move(records));
        }
        table.finish();
        constexpr FormId kWorkbench = 0x03003065, kDrafting = 0x0300305B, kCarpenterKeyword = 0x03003015;
        constexpr FormId kFoundationRecipe = 0x03003017, kFoundationPart = 0x03003014, kLayoutPart = 0x03003060;
        EXPECT_EQ(table.benchesOf(kWorkbench, true), std::vector<crafting::BenchKind>({ crafting::BenchKind::House }));
        EXPECT_TRUE(table.benchesOf(kWorkbench).empty());
        EXPECT_EQ(table.benchesOf(kDrafting, true), std::vector<crafting::BenchKind>({ crafting::BenchKind::House }));
        EXPECT_TRUE(table.furnitureOffers(kWorkbench, kCarpenterKeyword));
        const crafting::Recipe* recipe = table.recipe(kFoundationRecipe);
        ASSERT_NE(recipe, nullptr);
        EXPECT_EQ(recipe->mBench, kCarpenterKeyword);
        EXPECT_EQ(recipe->mResult, kFoundationPart);
        ASSERT_EQ(recipe->mConditions, 2u);
        ASSERT_EQ(recipe->mConditionList.size(), 2u) << "the conditions decode whole";
        EXPECT_TRUE(crafting::recipeConditionsItemCountOnly(recipe->mConditionList));
        std::map<FormId, std::int32_t> held{ { kLayoutPart, 1 } };
        const auto count = [&held](FormId item) { return held[item]; };
        for (const survey::Ctda& c : recipe->mConditionList)
            std::cout << "HOUSE condition fn " << c.mFunction << " compare " << int(c.mCompare) << " flags "
                      << int(c.mFlags) << " value " << c.mComparison << " param1 " << std::hex << c.mParam1 << std::dec
                      << std::endl;
        EXPECT_TRUE(crafting::itemCountConditionsHold(recipe->mConditionList, count));
        held[kFoundationPart] = 1;
        EXPECT_FALSE(crafting::itemCountConditionsHold(recipe->mConditionList, count));
        ASSERT_EQ(recipe->mItems.size(), 2u);
        EXPECT_EQ(recipe->mItems[1].mCount, 10);
        bool listed = false;
        for (const crafting::Recipe* r : table.recipesFor(crafting::BenchKind::House, true))
            listed = listed || r->mId == kFoundationRecipe;
        EXPECT_TRUE(listed);
    }
}
