// Task 0228: the player's chosen name reaching the core (PlayerIdentitySource) and the race powers: the character
// service's power functions and chunk, and the combat's list of a race's powers and their cast. Generated records and
// fakes only; no game data.
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

    struct PowerFx : testing::Test
    {
        std::unique_ptr<ActorIndex> mIndex = makeIndex();
        std::vector<std::string> mNames;
        std::vector<std::vector<FormId>> mAsked;
        std::vector<FormId> mCast;
        bool mCastAccepts = true;
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
                out.push_back({ kPowerHeal, "HealPower", PowerSupport::Works });
                out.push_back({ kPowerNight, "NightEyePower", PowerSupport::Unsupported });
                out.push_back({ kPowerRage, "RagePower", PowerSupport::StatusOnly });
                return out;
            };
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

    TEST_F(PowerFx, theFinishedNameGoesToTheNameHookAndAnEmptyOneBecomesTheDefault)
    {
        EXPECT_TRUE(mNames.empty()) << "nothing before the creation is done";
        finishAs("Ria");
        ASSERT_EQ(mNames.size(), 1u);
        EXPECT_EQ(mNames[0], "Ria");
    }

    TEST_F(PowerFx, aRestoredChoiceGivesItsNameAtOnce)
    {
        finishAs("Ria");
        MemoryWriter writer;
        mService->chunk().write(writer);
        mNames.clear();
        auto second = make();
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(second->chunk().read(reader, 1));
        ASSERT_FALSE(mNames.empty());
        EXPECT_EQ(mNames.front(), "Ria");
    }

    TEST_F(PowerFx, theNamespaceIsOnePointOneAndHasThePowerFunctions)
    {
        EXPECT_EQ(mBindings.mVersion.major, 1);
        EXPECT_EQ(mBindings.mVersion.minor, 1);
        for (const char* name : { "power_count", "power_name", "power_support", "power_ready", "use_power" })
            EXPECT_TRUE(mBindings.mFunctions.count(name)) << name;
        EXPECT_EQ(mBindings.mFunctions["use_power"].access, openeden::sdk::BindingAccess::Mutate);
        EXPECT_EQ(mBindings.mFunctions["power_ready"].access, openeden::sdk::BindingAccess::Read);
    }

    TEST_F(PowerFx, thereAreNoPowersBeforeTheCreationIsDoneAndThenTheRacesSpellsAreAsked)
    {
        EXPECT_TRUE(mService->powers().empty());
        EXPECT_EQ(mBindings.call("power_count").value.integer, 0);
        finishAs("Ria");
        EXPECT_EQ(mBindings.call("power_count").value.integer, 3);
        ASSERT_FALSE(mAsked.empty());
        EXPECT_EQ(mAsked.back(), (std::vector<FormId>{ kAbility, kPowerHeal, kPowerNight, kUnknown }));
        EXPECT_EQ(std::string(mBindings.call("power_name", { i32(0) }).value.text), "HealPower");
        EXPECT_EQ(std::string(mBindings.call("power_support", { i32(0) }).value.text), "works");
        EXPECT_EQ(std::string(mBindings.call("power_support", { i32(1) }).value.text), "none");
        EXPECT_EQ(std::string(mBindings.call("power_support", { i32(2) }).value.text), "status");
        EXPECT_EQ(std::string(mBindings.call("power_name", { i32(9) }).value.text), "");
    }

    TEST_F(PowerFx, aPowerCanBeUsedOnceAGameDay)
    {
        finishAs("Ria");
        EXPECT_TRUE(mService->powerReady(0, 1000));
        EXPECT_TRUE(mService->usePower(0, 1000));
        ASSERT_EQ(mCast.size(), 1u);
        EXPECT_EQ(mCast[0], kPowerHeal);
        EXPECT_FALSE(mService->powerReady(0, 1000 + kPowerCooldownSeconds - 1));
        EXPECT_FALSE(mService->usePower(0, 1000 + 60));
        EXPECT_EQ(mCast.size(), 1u);
        EXPECT_TRUE(mService->powerReady(0, 1000 + kPowerCooldownSeconds));
        EXPECT_TRUE(mService->usePower(0, 1000 + kPowerCooldownSeconds));
        EXPECT_EQ(mCast.size(), 2u);
    }

    TEST_F(PowerFx, anUnsupportedPowerOrABadIndexIsRefusedAndARefusedCastDoesNotStartTheDay)
    {
        finishAs("Ria");
        EXPECT_FALSE(mService->powerReady(1, 5));
        EXPECT_FALSE(mService->usePower(1, 5));
        EXPECT_FALSE(mService->usePower(7, 5));
        EXPECT_TRUE(mCast.empty());
        mCastAccepts = false;
        EXPECT_FALSE(mService->usePower(0, 5));
        mCastAccepts = true;
        EXPECT_TRUE(mService->usePower(0, 6)) << "the refused cast used nothing up";
    }

    TEST_F(PowerFx, aPowerCannotBeUsedBeforeTheCreationIsDone)
    {
        EXPECT_FALSE(mService->usePower(0, 5));
        EXPECT_TRUE(mCast.empty());
    }

    TEST_F(PowerFx, theBindingsUseAndReportThePowers)
    {
        finishAs("Ria");
        EXPECT_TRUE(mBindings.call("power_ready", { i32(0), i32(100) }).value.boolean);
        mBindings.call("use_power", { i32(0), i32(100) });
        EXPECT_EQ(mCast.size(), 1u);
        EXPECT_FALSE(mBindings.call("power_ready", { i32(0), i32(200) }).value.boolean);
        EXPECT_FALSE(mBindings.call("power_ready", { i32(-1), i32(200) }).value.boolean);
    }

    TEST_F(PowerFx, theDayIsSavedInItsOwnChunkAndTheCharacterChunkIsStillVersionOne)
    {
        EXPECT_EQ(mService->chunk().chunkVersion(), 1u);
        EXPECT_EQ(mService->powerChunk().chunkName(), "character_powers");
        finishAs("Ria");
        ASSERT_TRUE(mService->usePower(0, 500));
        MemoryWriter chr, pow;
        mService->chunk().write(chr);
        mService->powerChunk().write(pow);
        auto second = make();
        MemoryReader r1(chr.mBytes);
        MemoryReader r2(pow.mBytes);
        ASSERT_TRUE(second->chunk().read(r1, 1));
        ASSERT_TRUE(second->powerChunk().read(r2, 1));
        ASSERT_TRUE(second->poll());
        EXPECT_FALSE(second->powerReady(0, 500 + 60)) << "the day went on across the load";
        EXPECT_TRUE(second->powerReady(0, 500 + kPowerCooldownSeconds));
    }

    TEST_F(PowerFx, aDamagedPowerChunkIsRefused)
    {
        const std::vector<unsigned char> none;
        MemoryReader empty(none);
        EXPECT_FALSE(mService->powerChunk().read(empty, 1));
        std::vector<unsigned char> bad(4, 0);
        bad[0] = 99; // 99 records: over the cap
        MemoryReader tooMany(bad);
        EXPECT_FALSE(mService->powerChunk().read(tooMany, 1));
        std::vector<unsigned char> trailing(5, 0);
        MemoryReader extra(trailing);
        EXPECT_FALSE(mService->powerChunk().read(extra, 1));
        MemoryReader wrongVersion(trailing);
        EXPECT_FALSE(mService->powerChunk().read(wrongVersion, 2));
    }

    // ---- the world view's identity ---------------------------------------------------------------------------------

    TEST(SkyrimPlayerIdentity, theViewHasNoNameUntilOneIsSetAndTheRevisionMovesOnlyOnAChange)
    {
        SkyrimWorldView view({}, nullptr);
        PlayerIdentityState state;
        ASSERT_TRUE(view.playerIdentity(state));
        EXPECT_EQ(state.revision, 0u);
        view.setPlayerName("Ria");
        ASSERT_TRUE(view.playerIdentity(state));
        EXPECT_NE(state.revision, 0u);
        EXPECT_EQ(state.name, "Ria");
        const std::uint32_t first = state.revision;
        view.setPlayerName("Ria");
        ASSERT_TRUE(view.playerIdentity(state));
        EXPECT_EQ(state.revision, first);
        view.setPlayerName("Bo");
        ASSERT_TRUE(view.playerIdentity(state));
        EXPECT_NE(state.revision, first);
        EXPECT_EQ(state.name, "Bo");
    }

    // ---- the combat's list and cast of powers ----------------------------------------------------------------------

    class PowerData final : public CombatData
    {
    public:
        PowerData()
        {
            addEffect(kHealFx, 0, 24, 0x201000); // restore health: a vital value
            addEffect(kNightFx, 15, -1, 0); // night eye: an archetype the runtime does not do
            addEffect(kResistFx, 0, 39, 0); // a modifier of a value the pack does not track
            addEffect(kFearFx, MagicArchetype::Demoralize, 1, 0x409903);
            addSpell(kAbility, SpellRecord::kTypeAbility, { { kResistFx, 10.f, 0 } });
            addSpell(kPowerHeal, 2, { { kHealFx, 50.f, 0 } });
            addSpell(kPowerNight, 3, { { kNightFx, 0.f, 60 } });
            addSpell(kPowerRage, 2, { { kResistFx, 50.f, 60 } });
            addSpell(kPowerCry, 2, { { kFearFx, 99.f, 30, 30 } });
            addSpell(kPlainSpell, 0, { { kHealFx, 5.f, 0 } });
            mEffects[kFearFx].mConditions = 4; // gated on its targets in the real Battle Cry
            FighterProfile player;
            player.mHealthMax = 100.f;
            player.mStaminaMax = 100.f;
            player.mMagickaMax = 100.f;
            player.mSkills.fill(15);
            mPlayer = player;
        }
        bool fighter(WorldId base, FighterProfile& out) override
        {
            if (base != kNear && base != kFar)
                return false;
            out = FighterProfile();
            out.mHealthMax = 50.f;
            out.mStaminaMax = 50.f;
            out.mMagickaMax = 50.f;
            return true;
        }
        FighterProfile playerFighter() override { return mPlayer; }
        const WeaponRecord* weaponByEditorId(std::string_view) override { return nullptr; }
        CombatSettings settings() override { return CombatSettings(); }
        const SpellRecord* spellById(FormId id) override
        {
            const auto it = mSpells.find(id);
            return it == mSpells.end() ? nullptr : &it->second;
        }
        const MagicEffectRecord* effectById(FormId id) override
        {
            const auto it = mEffects.find(id);
            return it == mEffects.end() ? nullptr : &it->second;
        }
        MagicSettings magicSettings() override { return MagicSettings(); }

        struct E
        {
            FormId mEffect;
            float mMagnitude;
            std::uint32_t mDuration;
            std::uint32_t mArea = 0;
        };
        void addEffect(FormId id, std::uint32_t archetype, std::int32_t value, std::uint32_t flags)
        {
            MagicEffectRecord m;
            m.mCommon.mId = id;
            m.mCommon.mEditorId = "Fx" + std::to_string(id);
            m.mHasData = true;
            m.mArchetype = archetype;
            m.mPrimaryValue = value;
            m.mFlags = flags;
            mEffects[id] = m;
        }
        void addSpell(FormId id, std::uint32_t type, const std::vector<E>& effects)
        {
            SpellRecord s;
            s.mCommon.mId = id;
            s.mCommon.mEditorId = "Spell" + std::to_string(id);
            s.mHasData = true;
            s.mType = type;
            s.mCastType = MagicCast::FireAndForget;
            s.mDelivery = MagicDelivery::Self;
            for (const E& e : effects)
            {
                SpellEffect se;
                se.mEffect = e.mEffect;
                se.mMagnitude = e.mMagnitude;
                se.mDuration = e.mDuration;
                se.mArea = e.mArea;
                s.mEffects.push_back(se);
            }
            mSpells[id] = s;
        }
        std::map<FormId, SpellRecord> mSpells;
        std::map<FormId, MagicEffectRecord> mEffects;
        FighterProfile mPlayer;
    };

    class PowerWriter final : public CombatRequestWriter
    {
    public:
        bool playClip(CombatSubject, const ClipRequest&) override { return true; }
        bool stopClip(CombatSubject) override { return true; }
        bool clipEventTime(std::string_view clip, bool, ClipEventKind kind, float& out) override
        {
            if (kind != ClipEventKind::SpellFire || clip != "MRh_SelfRelease")
                return false;
            out = 0.367f;
            return true;
        }
        bool reportDamage(const DamageReport& report) override
        {
            mDamage.push_back(report);
            return true;
        }
        bool subjectState(CombatSubject subject, SubjectState& out) override
        {
            out = {};
            if (!subject.player)
            {
                const auto it = mStates.find(subject.actor.reference);
                if (it == mStates.end())
                    return false;
                out = it->second;
                return true;
            }
            out = mPlayerState;
            return true;
        }
        std::map<WorldId, SubjectState> mStates;
        bool clipTiming(std::string_view, bool, ClipTiming& out) override
        {
            out = { true, 0.367f, 1.287f };
            return true;
        }
        std::vector<DamageReport> mDamage;
        SubjectState mPlayerState;
    };

    struct PowerCombat : testing::Test
    {
        PowerData mData;
        SkyrimCombat mCombat;
        PowerWriter mWriter;
        std::vector<std::string> mTrace;
        void SetUp() override
        {
            mCombat.setTrace([this](const std::string& line) { mTrace.push_back(line); });
            mCombat.setData(&mData);
            mWriter.mPlayerState.healthMax = 100.f;
            mWriter.mPlayerState.health = 100.f;
        }
        void updates(int n)
        {
            for (int i = 0; i < n; ++i)
                mCombat.combatUpdate(0.25f, mWriter);
        }
        bool traceHas(const std::string& text) const
        {
            for (const std::string& line : mTrace)
                if (line.find(text) != std::string::npos)
                    return true;
            return false;
        }
    };

    TEST_F(PowerCombat, onlyPowersAndLesserPowersAreListedWithWhatTheRuntimeDoesWithThem)
    {
        const std::vector<SkyrimCombat::RacePower> list
            = mCombat.racePowers({ kAbility, kPowerHeal, kPowerNight, kPowerRage, kPowerCry, kPlainSpell, kUnknown });
        ASSERT_EQ(list.size(), 4u);
        EXPECT_EQ(list[0].mId, kPowerHeal);
        EXPECT_EQ(list[0].mSupport, SkyrimCombat::PowerSupport::Works);
        EXPECT_EQ(list[1].mId, kPowerNight);
        EXPECT_EQ(list[1].mSupport, SkyrimCombat::PowerSupport::Unsupported);
        EXPECT_EQ(list[2].mId, kPowerRage);
        EXPECT_EQ(list[2].mSupport, SkyrimCombat::PowerSupport::StatusOnly);
        EXPECT_EQ(list[3].mId, kPowerCry);
        EXPECT_EQ(list[3].mSupport, SkyrimCombat::PowerSupport::Works);
        EXPECT_EQ(list[0].mName, "Spell" + std::to_string(kPowerHeal));
    }

    TEST_F(PowerCombat, aPowerTakesItsEffectsConditionsAsMet)
    {
        mData.mEffects[kHealFx].mConditions = 2;
        const std::vector<SkyrimCombat::RacePower> list = mCombat.racePowers({ kPowerHeal, kPowerCry });
        ASSERT_EQ(list.size(), 2u);
        EXPECT_EQ(list[0].mSupport, SkyrimCombat::PowerSupport::Works);
        EXPECT_EQ(list[1].mSupport, SkyrimCombat::PowerSupport::Works) << "the Demoralize effect has 4 conditions";
    }

    TEST_F(PowerCombat, thereIsNoListWithoutData)
    {
        SkyrimCombat bare;
        EXPECT_TRUE(bare.racePowers({ kPowerHeal }).empty());
        EXPECT_FALSE(bare.castPlayerPower(kPowerHeal));
    }

    TEST_F(PowerCombat, onlyAKnownPowerCanBeQueuedAndOnlyOneAtATime)
    {
        EXPECT_FALSE(mCombat.castPlayerPower(kUnknown));
        EXPECT_FALSE(mCombat.castPlayerPower(kAbility));
        EXPECT_FALSE(mCombat.castPlayerPower(kPlainSpell));
        EXPECT_TRUE(mCombat.castPlayerPower(kPowerHeal));
        EXPECT_FALSE(mCombat.castPlayerPower(kPowerCry));
    }

    TEST_F(PowerCombat, aQueuedPowerIsCastThroughTheMagicRuntimeAndItsEffectsApply)
    {
        ASSERT_TRUE(mCombat.castPlayerPower(kPowerHeal));
        updates(8);
        EXPECT_EQ(mCombat.spellsCast(), 1u);
        EXPECT_GE(mCombat.effectsApplied(), 1u);
        EXPECT_TRUE(traceHas("cast begin"));
        // the queue is empty again: a second power can be queued
        EXPECT_TRUE(mCombat.castPlayerPower(kPowerHeal));
    }

    ActorInfo actorInfo(WorldId reference, WorldId base)
    {
        ActorInfo i;
        i.handle = { reference, 1 };
        i.base = base;
        i.editorId = "Foe" + std::to_string(reference);
        return i;
    }

    TEST_F(PowerCombat, aPowerWithAnAreaReachesTheOthersWithinItAndNotTheCasterOrTheFarOnes)
    {
        mCombat.onActorAdded(actorInfo(10, kNear));
        mCombat.onActorAdded(actorInfo(11, kFar));
        SubjectState near;
        near.x = 600.0; // 30 feet are 640 units
        mWriter.mStates[10] = near;
        SubjectState far;
        far.x = 700.0;
        mWriter.mStates[11] = far;
        ASSERT_TRUE(mCombat.castPlayerPower(kPowerCry));
        updates(8);
        EXPECT_EQ(mCombat.spellsCast(), 1u);
        EXPECT_TRUE(traceHas("area 30 ft reached 1"));
        EXPECT_TRUE(traceHas("demoralize on actor 10"));
        EXPECT_FALSE(traceHas("demoralize on actor 11"));
        EXPECT_FALSE(traceHas("demoralize on player"));
    }
}
