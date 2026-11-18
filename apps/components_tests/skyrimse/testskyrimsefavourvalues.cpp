// Task 0285: the two natives the favour jobs need to pay their reward: Form.GetGoldValue (the base value of an item, from
// the pack's inventory model) and GlobalVariable.GetValueInt (the whole part of a global). The wheat, potato, leek,
// gourd, cabbage and nirnroot sales (FavorJobsGatherWheatScript) and the ore sales (FavorJobsMineOreScript) compute
// count times value from them; before, both were tolerant and the reward was 0 gold. Generated scripts and a fake game;
// no game data. Written first and run red against the tolerant natives.
#include <game_skyrimse/papyrus_host.hpp>
#include <game_skyrimse/papyrus_world.hpp>
#include <game_skyrimse/vmad.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "../papyrus/papyrustestutil.hpp"

namespace
{
    using namespace game_skyrimse;
    using PexAsm::Id;
    using PexAsm::ScriptBuilder;

    constexpr FormId kSelf = 0x01000A01;
    constexpr FormId kBase = 0x01000B00;
    constexpr FormId kWheat = 0x01000E01;
    constexpr FormId kUnknownItem = 0x01000E02;
    constexpr FormId kGlobal = 0x01000C01;
    constexpr FormId kGold = 0x0000000F;

    class Data final : public PapyrusGameData
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
        bool referencePosition(FormId, double& x, double& y, double& z) const override
        {
            x = y = z = 0.0;
            return true;
        }
        bool globalValue(FormId global, float& value) const override
        {
            const auto it = mGlobals.find(global);
            if (it == mGlobals.end())
                return false;
            value = it->second;
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
        std::map<FormId, float> mGlobals;
    };

    class Hooks final : public PapyrusWorldHooks
    {
    public:
        bool addItem(FormId owner, FormId item, std::int32_t count) override
        {
            mItems[{ owner, item }] += count;
            return true;
        }
        std::int32_t removeItem(FormId owner, FormId item, std::int32_t count) override
        {
            std::int32_t& have = mItems[{ owner, item }];
            const std::int32_t take = std::min(have, count);
            have -= take;
            return take;
        }
        std::int32_t itemCount(FormId owner, FormId item) override { return mItems[{ owner, item }]; }
        std::int32_t goldValue(FormId item) override { return item == kWheat ? 5 : -1; }
        std::map<std::pair<FormId, FormId>, std::int32_t> mItems;
    };

    void addClasses(Data& data)
    {
        ScriptBuilder form("Form");
        form.function("GetFormID", "Int").native();
        form.function("GetGoldValue", "Int").native();
        data.add(form);
        ScriptBuilder ref("ObjectReference");
        ref.parent("Form");
        ref.function("AddItem").native().param("akItemToAdd", "Form").param("aiCount", "Int").param("abSilent", "Bool");
        ref.function("GetItemCount", "Int").native().param("akItem", "Form");
        data.add(ref);
        ScriptBuilder actor("Actor");
        actor.parent("ObjectReference");
        data.add(actor);
        ScriptBuilder global("GlobalVariable");
        global.parent("Form");
        global.function("GetValue", "Float").native();
        global.function("GetValueInt", "Int").native();
        data.add(global);
    }

    // What SellWheat does, boiled down: reward is count times value, paid with AddItem.
    ScriptBuilder probeScript()
    {
        ScriptBuilder s("FavourProbe");
        s.parent("Actor");
        s.autoProperty("Item", "Form");
        s.autoProperty("Gold", "Form");
        s.autoProperty("Rate", "GlobalVariable");
        s.var("value", "Int");
        s.var("whole", "Int");
        s.var("reward", "Int");
        s.function("ReadValue")
            .local("v", "Int")
            .callMethod("GetGoldValue", Id("::Item_var"), Id("v"))
            .assign(Id("value"), Id("v"));
        s.function("ReadWhole")
            .local("v", "Int")
            .callMethod("GetValueInt", Id("::Rate_var"), Id("v"))
            .assign(Id("whole"), Id("v"));
        s.function("Sell")
            .local("n", "Int")
            .local("v", "Int")
            .local("none", "None")
            .callMethod("GetItemCount", Id("self"), Id("n"), { Id("::Item_var") })
            .callMethod("GetGoldValue", Id("::Item_var"), Id("v"))
            .op(PexAsm::Op::IMul, { Id("reward"), Id("n"), Id("v") })
            .callMethod("AddItem", Id("self"), Id("none"), { Id("::Gold_var"), Id("reward"), PexAsm::Bool(false) });
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

    struct FavourValuesTest : testing::Test
    {
        void SetUp() override
        {
            addClasses(mData);
            mData.add(probeScript());
            mData.mClasses[kSelf] = "Actor";
            mData.mClasses[kWheat] = "Form";
            mData.mClasses[kUnknownItem] = "Form";
            mData.mClasses[kGold] = "Form";
            mData.mClasses[kGlobal] = "GlobalVariable";
            mData.mGlobals[kGlobal] = 2.9f;
            mHost = std::make_unique<PapyrusHost>(mData);
            mHost->setLog([this](bool error, const std::string& line) { (error ? mErrors : mLogs).push_back(line); });
            mHost->setWorldHooks(&mHooks);
            mHost->startGame();
            vmad::Script script;
            script.mName = "FavourProbe";
            script.mProperties = { objProp("Item", kWheat), objProp("Gold", kGold), objProp("Rate", kGlobal) };
            ASSERT_EQ(mHost->attachReference(kSelf, kBase, true, { std::move(script) }), 1u);
            tick();
        }
        void tick(int n = 3)
        {
            for (int i = 0; i < n; ++i)
                mHost->tick(0.1f);
        }
        bool call(const char* function)
        {
            const bool ok = mHost->callFunction(kSelf, "FavourProbe", function);
            tick();
            return ok;
        }
        std::optional<double> number(const char* name) { return mHost->numberVariable(kSelf, "FavourProbe", name); }
        bool tolerant(const std::string& key) const
        {
            const auto names = mHost->tolerantNatives();
            return std::find(names.begin(), names.end(), key) != names.end();
        }

        Data mData;
        Hooks mHooks;
        std::unique_ptr<PapyrusHost> mHost;
        std::vector<std::string> mLogs;
        std::vector<std::string> mErrors;
    };

    TEST_F(FavourValuesTest, formGetGoldValueAsksTheHookForTheItemsBaseValue)
    {
        ASSERT_TRUE(call("ReadValue"));
        EXPECT_EQ(number("value"), 5.0);
        EXPECT_FALSE(tolerant("form.getgoldvalue"));
        EXPECT_TRUE(mErrors.empty());
    }

    TEST_F(FavourValuesTest, anItemTheHookDoesNotKnowIsWorthNothing)
    {
        mHost->setWorldHooks(nullptr);
        ASSERT_TRUE(call("ReadValue"));
        EXPECT_EQ(number("value"), 0.0);
        EXPECT_TRUE(mErrors.empty());
    }

    TEST_F(FavourValuesTest, globalVariableGetValueIntIsTheWholePartOfTheValue)
    {
        ASSERT_TRUE(call("ReadWhole"));
        EXPECT_EQ(number("whole"), 2.0);
        EXPECT_FALSE(tolerant("globalvariable.getvalueint"));
        mData.mGlobals[kGlobal] = -2.9f;
        ASSERT_TRUE(call("ReadWhole"));
        EXPECT_EQ(number("whole"), -2.0) << "truncated toward zero";
    }

    TEST_F(FavourValuesTest, aSaleIsPaidCountTimesTheValue)
    {
        mHooks.mItems[std::make_pair(kSelf, kWheat)] = 4;
        ASSERT_TRUE(call("Sell"));
        EXPECT_EQ(number("reward"), 20.0);
        EXPECT_EQ(mHooks.itemCount(kSelf, kGold), 20);
    }
}
