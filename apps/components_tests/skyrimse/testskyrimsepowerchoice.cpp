// Task 0281: the chosen power of the P key: the character service's choice, cooldown and source functions, bindings
// and the chunk `power_choice`. Generated records and fakes only; no game data. Written before the code.
#include <game_skyrimse/actors.hpp>
#include <game_skyrimse/character.hpp>
#include <game_skyrimse/character_service.hpp>
#include <game_skyrimse/combat.hpp>
#include <game_skyrimse/world_view.hpp>

#include <gtest/gtest.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::character;
    using namespace openeden::sdk::game;
    using openeden::sdk::BindingType;
    using openeden::sdk::BindingValue;

    constexpr FormId kNord = 0x13746;
    constexpr FormId kList = 0x900;
    constexpr FormId kHair1 = 0x1001;
    constexpr FormId kEyes1 = 0x1010;
    constexpr FormId kAbility = 0x5000, kPowerHeal = 0x5001, kPowerNight = 0x5002, kPowerRage = 0x5003,
                     kPowerCry = 0x5004, kPlainSpell = 0x5005, kUnknown = 0x5999;
    constexpr WorldId kNear = 200, kFar = 201;
    constexpr FormId kHealFx = 0x6001, kNightFx = 0x6002, kResistFx = 0x6003, kFearFx = 0x6004;

    // ---- the character service ------------------------------------------------------------------------------------

    std::unique_ptr<ActorIndex> makeIndex()
    {
        ActorRecords r;
        RaceRecord nord;
        nord.mCommon.mId = kNord;
        nord.mCommon.mEditorId = "NordRace";
        nord.mName.mPresent = true;
        nord.mName.mText = "Nord";
        nord.mFlags = 1;
        nord.mSkeleton[0] = "m.nif";
        nord.mSkeleton[1] = "f.nif";
        nord.mHeadParts = { { Sex::Male, 0, kHair1 }, { Sex::Male, 1, kEyes1 }, { Sex::Female, 0, kHair1 },
            { Sex::Female, 1, kEyes1 } };
        nord.mSpells = { kAbility, kPowerHeal, kPowerNight, kUnknown };
        r.mRaces.push_back(nord);
        ActorFormList list;
        list.mCommon.mId = kList;
        list.mForms = { kNord };
        r.mFormLists.push_back(list);
        HeadPartRecord hair;
        hair.mCommon.mId = kHair1;
        hair.mCommon.mEditorId = "HairA";
        hair.mType = 3;
        hair.mFlags = 7;
        hair.mValidRaces = kList;
        HeadPartRecord eyes = hair;
        eyes.mCommon.mId = kEyes1;
        eyes.mCommon.mEditorId = "EyesA";
        eyes.mType = 2;
        r.mHeadParts = { hair, eyes };
        auto index = std::make_unique<ActorIndex>();
        index->add(r);
        return index;
    }

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* bytes = static_cast<const unsigned char*>(data);
            mBytes.insert(mBytes.end(), bytes, bytes + size);
        }
        std::vector<unsigned char> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(const std::vector<unsigned char>& bytes)
            : mBytes(bytes)
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mPos, size);
            mPos += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mPos; }

    private:
        const std::vector<unsigned char>& mBytes;
        std::size_t mPos = 0;
    };

    class PowerBindings final : public openeden::sdk::BindingsService
    {
    public:
        openeden::sdk::DeclareResult declareNamespace(const openeden::sdk::NamespaceDescriptor& d) override
        {
            mVersion = d.version;
            for (const auto& f : d.functions)
                mFunctions[std::string(f.name)] = f;
            return { true, {} };
        }
        openeden::sdk::BindingResult call(const std::string& name, std::vector<BindingValue> args = {})
        {
            const auto it = mFunctions.find(name);
            EXPECT_NE(it, mFunctions.end()) << name;
            mArgs = std::move(args);
            return it->second.handler->call(mArgs);
        }
        openeden::sdk::Version mVersion{ 0, 0 };
        std::map<std::string, openeden::sdk::FunctionDescriptor> mFunctions;
        std::vector<BindingValue> mArgs;
    };

    BindingValue i32(std::int32_t v)
    {
        BindingValue b;
        b.type = BindingType::Int32;
        b.integer = v;
        return b;
    }

    struct ChoiceFx : testing::Test
    {
        std::unique_ptr<ActorIndex> mIndex = makeIndex();
        std::vector<std::string> mNames;
        std::vector<std::vector<FormId>> mAsked;
        std::vector<FormId> mCast;
        bool mCastAccepts = true;
        std::vector<FormId> mExtra;
        std::unique_ptr<CharacterService> mService;
        PowerBindings mBindings;

        Hooks hooks()
        {
            Hooks h;
            h.mAccess = [this](Access& out) {
                out.mIndex = mIndex.get();
                out.mText = [](const LString& s, std::uint32_t, StringTableKind) { return s.mText; };
                return true;
            };
            h.mName = [this](const std::string& n) { mNames.push_back(n); };
            h.mPowers = [this](const std::vector<FormId>& spells) {
                mAsked.push_back(spells);
                std::vector<PowerInfo> out;
                out.push_back({ kPowerHeal, "HealPower", PowerSupport::Works, {} });
                out.push_back({ kPowerNight, "NightEyePower", PowerSupport::Unsupported, {} });
                out.push_back({ kPowerRage, "RagePower", PowerSupport::StatusOnly, {} });
                return out;
            };
            h.mExtraPowerSpells = [this] { return mExtra; };
            h.mPowerSource = [](const PowerInfo& p) { return p.mName == "RagePower" ? "Vampirism" : "Standing stone"; };
            h.mCastPower = [this](FormId id) {
                mCast.push_back(id);
                return mCastAccepts;
            };
            return h;
        }
        std::unique_ptr<CharacterService> make()
        {
            auto s = std::make_unique<CharacterService>(hooks());
            EXPECT_TRUE(s->declare(mBindings).ok);
            return s;
        }
        void SetUp() override
        {
            mService = make();
            ASSERT_TRUE(mService->poll());
        }
        void finishAs(const std::string& name)
        {
            mService->setName(name);
            ASSERT_TRUE(mService->finish());
        }
    };

    std::string text(openeden::sdk::BindingResult r)
    {
        return std::string(r.value.text);
    }

    BindingValue str(const char* s)
    {
        BindingValue b;
        b.type = BindingType::String;
        b.text = s;
        return b;
    }

    TEST_F(ChoiceFx, theNamespaceStaysOnePointOneAndHasTheChoiceFunctions)
    {
        EXPECT_EQ(mBindings.mVersion.minor, 1);
        for (const char* name : { "power_source", "power_cooldown", "power_chosen", "choose_power" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        EXPECT_EQ(mBindings.mFunctions["choose_power"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["power_chosen"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(ChoiceFx, thereIsNoChoiceAtFirstAndAnUnknownOrUnsupportedPowerCannotBeChosen)
    {
        finishAs("Ria");
        EXPECT_EQ(mService->chosenPower(), -1);
        EXPECT_EQ(mBindings.call("power_chosen").value.integer, -1);
        EXPECT_FALSE(mService->choosePower("NoSuchPower"));
        EXPECT_FALSE(mService->choosePower("NightEyePower")) << "unsupported: nothing to use";
        EXPECT_EQ(mService->chosenPower(), -1);
    }

    TEST_F(ChoiceFx, aPowerIsChosenByItsNameAndAnEmptyNameClearsTheChoice)
    {
        finishAs("Ria");
        const std::uint64_t before = mService->revision();
        ASSERT_TRUE(mService->choosePower("RagePower"));
        EXPECT_EQ(mService->chosenPower(), 2);
        EXPECT_EQ(mBindings.call("power_chosen").value.integer, 2);
        EXPECT_NE(mService->revision(), before) << "the window follows the revision";
        mBindings.call("choose_power", { str("HealPower") });
        EXPECT_EQ(mService->chosenPower(), 0);
        mBindings.call("choose_power", { str("") });
        EXPECT_EQ(mService->chosenPower(), -1);
        EXPECT_TRUE(mService->choosePower("")) << "clearing is always fine";
    }

    TEST_F(ChoiceFx, nothingCanBeChosenBeforeTheCreationIsDone)
    {
        EXPECT_FALSE(mService->choosePower("HealPower"));
        EXPECT_EQ(mService->chosenPower(), -1);
    }

    TEST_F(ChoiceFx, theCooldownLeftCountsDownAndIsZeroWhenReadyOrWhenThePowerCannotBeUsed)
    {
        finishAs("Ria");
        EXPECT_EQ(mService->powerCooldownLeft(0, 1000), 0);
        ASSERT_TRUE(mService->usePower(0, 1000));
        EXPECT_EQ(mService->powerCooldownLeft(0, 1000), kPowerCooldownSeconds);
        EXPECT_EQ(mService->powerCooldownLeft(0, 1000 + 3600), kPowerCooldownSeconds - 3600);
        EXPECT_EQ(mService->powerCooldownLeft(0, 1000 + kPowerCooldownSeconds), 0);
        EXPECT_EQ(mService->powerCooldownLeft(0, 500), 0) << "a clock set back frees it, as powerReady says";
        EXPECT_EQ(mService->powerCooldownLeft(1, 1000), 0) << "unsupported";
        EXPECT_EQ(mService->powerCooldownLeft(9, 1000), 0);
        EXPECT_EQ(mBindings.call("power_cooldown", { i32(0), i32(1000 + 60) }).value.integer,
            static_cast<std::int32_t>(kPowerCooldownSeconds - 60));
    }

    TEST_F(ChoiceFx, theSourceOfARacePowerIsTheRace)
    {
        finishAs("Ria");
        EXPECT_EQ(text(mBindings.call("power_source", { i32(0) })), "Race");
        EXPECT_EQ(text(mBindings.call("power_source", { i32(9) })), "");
    }

    TEST_F(ChoiceFx, anExtraPowerGetsItsSourceFromTheHook)
    {
        auto h = hooks();
        h.mExtraPowerSpells = [] { return std::vector<FormId>{ 0x7777 }; };
        h.mPowers = [](const std::vector<FormId>& spells) {
            std::vector<PowerInfo> out;
            if (spells.size() == 1)
                out.push_back({ 0x7777, "VampireInvisibilityPC", PowerSupport::Works, {} });
            return out;
        };
        h.mPowerSource
            = [](const PowerInfo& p) { return std::string(p.mName.rfind("Vampire", 0) == 0 ? "Vampirism" : "Stone"); };
        CharacterService svc(std::move(h));
        PowerBindings b;
        ASSERT_TRUE(svc.declare(b).ok);
        ASSERT_TRUE(svc.poll());
        svc.setName("Ria");
        ASSERT_TRUE(svc.finish());
        ASSERT_EQ(svc.powers().size(), 1u);
        EXPECT_EQ(text(b.call("power_source", { i32(0) })), "Vampirism");
    }

    TEST_F(ChoiceFx, theChoiceIsSavedInItsOwnChunkAndComesBackAfterALoad)
    {
        EXPECT_EQ(mService->choiceChunk().chunkName(), "power_choice");
        EXPECT_EQ(mService->choiceChunk().chunkVersion(), 1u);
        finishAs("Ria");
        ASSERT_TRUE(mService->choosePower("RagePower"));
        MemoryWriter chr, ch;
        mService->chunk().write(chr);
        mService->choiceChunk().write(ch);
        auto second = make();
        MemoryReader r1(chr.mBytes);
        MemoryReader r2(ch.mBytes);
        ASSERT_TRUE(second->chunk().read(r1, 1));
        ASSERT_TRUE(second->choiceChunk().read(r2, 1));
        ASSERT_TRUE(second->poll());
        EXPECT_EQ(second->chosenPower(), 2);
    }

    TEST_F(ChoiceFx, aSavedChoiceOfAPowerTheCharacterNoLongerHasReadsAsNoChoice)
    {
        finishAs("Ria");
        ASSERT_TRUE(mService->choosePower("RagePower"));
        MemoryWriter ch;
        mService->choiceChunk().write(ch);
        auto second = make(); // creation not done in this one: it holds no powers
        MemoryReader r2(ch.mBytes);
        ASSERT_TRUE(second->choiceChunk().read(r2, 1));
        EXPECT_EQ(second->chosenPower(), -1);
    }

    TEST_F(ChoiceFx, aDamagedChoiceChunkIsRefusedAndAZeroIdMeansNoChoice)
    {
        const std::vector<unsigned char> none;
        MemoryReader empty(none);
        EXPECT_FALSE(mService->choiceChunk().read(empty, 1));
        std::vector<unsigned char> trailing(8, 0);
        MemoryReader extra(trailing);
        EXPECT_FALSE(mService->choiceChunk().read(extra, 1));
        std::vector<unsigned char> ok(4, 0);
        MemoryReader wrongVersion(ok);
        EXPECT_FALSE(mService->choiceChunk().read(wrongVersion, 2));
        MemoryReader good(ok);
        EXPECT_TRUE(mService->choiceChunk().read(good, 1));
        EXPECT_EQ(mService->chosenPower(), -1);
    }

    TEST_F(ChoiceFx, theChoiceChunkIsCanonical)
    {
        finishAs("Ria");
        ASSERT_TRUE(mService->choosePower("HealPower"));
        MemoryWriter a, b;
        mService->choiceChunk().write(a);
        mService->choiceChunk().write(b);
        EXPECT_EQ(a.mBytes, b.mBytes);
        EXPECT_EQ(a.mBytes.size(), 4u);
        auto second = make();
        MemoryReader r(a.mBytes);
        ASSERT_TRUE(second->choiceChunk().read(r, 1));
        MemoryWriter c;
        second->choiceChunk().write(c);
        EXPECT_EQ(c.mBytes, a.mBytes);
    }

    TEST_F(ChoiceFx, theOldPowerChunkStillLoadsUnchanged)
    {
        finishAs("Ria");
        ASSERT_TRUE(mService->usePower(0, 500));
        MemoryWriter pow;
        mService->powerChunk().write(pow);
        EXPECT_EQ(mService->powerChunk().chunkVersion(), 1u);
        auto second = make();
        MemoryReader r(pow.mBytes);
        EXPECT_TRUE(second->powerChunk().read(r, 1));
    }
}
