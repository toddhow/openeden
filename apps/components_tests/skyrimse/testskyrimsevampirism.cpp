// Task 0260 (vampirism): the pure rules (the two game clocks the scripts rely on, the sunlight test, the ability
// filter), the service over a fake script port, and the save chunk. No game data.
#include <game_skyrimse/vampirism.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <vector>

namespace
{
    using namespace game_skyrimse;
    using namespace game_skyrimse::vampirism;

    constexpr double kDay = 86400.0;

    // ---- a fake library with the sun damage spell of the data (conditions copied from the CTDA records of Skyrim.esm)
    // -------

    survey::Ctda cond(std::uint16_t fn, std::uint8_t compare, float value, std::uint32_t p1 = 0)
    {
        survey::Ctda c;
        c.mCompare = compare;
        c.mTypeByte = static_cast<std::uint8_t>(compare << 5);
        c.mComparison = value;
        std::memcpy(&c.mComparisonBits, &value, sizeof(value));
        c.mFunction = fn;
        c.mParam1 = p1;
        return c;
    }

    class SunLibrary final : public PerkLibrary
    {
    public:
        SunLibrary()
        {
            SpellRecord s;
            s.mCommon.mId = kSunDamageSpells[0];
            s.mHasData = true;
            s.mType = SpellRecord::kTypeAbility;
            for (int i = 0; i < 3; ++i)
            {
                SpellEffect e;
                e.mEffect = 0x100 + i;
                // GetInWorldspace(0x2EE41) == 0, GetGlobalValue(GameHour) <= 19, >= 5, IsInInterior == 0
                e.mConditionList = { cond(310, 0, 0.f, 0x2EE41), cond(74, 5, 19.f, kGameHourGlobal),
                    cond(74, 3, 5.f, kGameHourGlobal), cond(300, 0, 0.f) };
                e.mConditions = static_cast<std::uint32_t>(e.mConditionList.size());
                s.mEffects.push_back(e);
            }
            mSpell = s;
        }
        const PerkRecord* perk(FormId) const override { return nullptr; }
        const SpellRecord* spell(FormId id) const override { return id == kSunDamageSpells[0] ? &mSpell : nullptr; }
        const MagicEffectRecord* magicEffect(FormId) const override { return nullptr; }
        SpellRecord mSpell;
    };

    effectconditions::Place outdoors()
    {
        effectconditions::Place p;
        p.mKnown = true;
        p.mWorld = 0x3C;
        return p;
    }

    // ---- the two clocks
    // ----------------------------------------------------------------------------------------------------

    TEST(SkyrimVampirismMachine, the_change_comes_three_game_days_after_the_infection_and_not_before)
    {
        Machine m;
        m.observeInfection(true, 10.0);
        EXPECT_TRUE(m.infected());
        EXPECT_EQ(m.step(12.99), Machine::Action::None);
        EXPECT_EQ(m.step(13.0), Machine::Action::Change);
        EXPECT_EQ(m.step(13.5), Machine::Action::Change); // repeats until the script reports the turn
    }

    TEST(SkyrimVampirismMachine, a_cure_forgets_the_three_days_and_a_new_infection_starts_them_again)
    {
        Machine m;
        m.observeInfection(true, 10.0);
        m.observeInfection(false, 11.0);
        EXPECT_FALSE(m.infected());
        EXPECT_EQ(m.step(20.0), Machine::Action::None);
        m.observeInfection(true, 20.0);
        EXPECT_DOUBLE_EQ(m.infectedAt(), 20.0);
        m.observeInfection(true, 21.0); // the first sighting counts
        EXPECT_DOUBLE_EQ(m.infectedAt(), 20.0);
    }

    TEST(SkyrimVampirismMachine, a_vampire_ticks_every_twelve_game_hours_and_infection_no_longer_matters)
    {
        Machine m;
        m.observeInfection(true, 1.0);
        m.becameVampire(4.0);
        EXPECT_TRUE(m.vampire());
        EXPECT_FALSE(m.infected());
        EXPECT_EQ(m.step(4.49), Machine::Action::None);
        EXPECT_EQ(m.step(4.5), Machine::Action::Tick);
        EXPECT_EQ(m.step(4.9), Machine::Action::None);
        EXPECT_EQ(m.step(5.0), Machine::Action::Tick);
        m.observeInfection(true, 5.0); // a vampire cannot be infected again
        EXPECT_FALSE(m.infected());
        EXPECT_EQ(m.step(30.0), Machine::Action::Tick);
    }

    TEST(SkyrimVampirismMachine, feeding_restarts_the_tick_clock_and_a_cure_stops_it)
    {
        Machine m;
        m.becameVampire(0.0);
        m.restartTicks(2.0);
        EXPECT_DOUBLE_EQ(m.nextTick(), 2.5);
        m.cured();
        EXPECT_FALSE(m.vampire());
        EXPECT_EQ(m.step(100.0), Machine::Action::None);
    }

    TEST(SkyrimVampirismMachine, a_clock_that_is_not_a_number_does_nothing)
    {
        Machine m;
        m.observeInfection(true, 1.0);
        EXPECT_EQ(m.step(std::nan("")), Machine::Action::None);
        m.restore(true, std::nan(""), std::nan(""));
        EXPECT_FALSE(m.infected());
        EXPECT_EQ(m.step(5.0), Machine::Action::None);
    }

    // ---- the sun
    // -------------------------------------------------------------------------------------------------------------

    TEST(SkyrimVampirismSun, the_hour_of_a_game_clock)
    {
        EXPECT_FLOAT_EQ(hourOfClock(0.0), 0.f);
        EXPECT_FLOAT_EQ(hourOfClock(13.5 * 3600.0), 13.5f);
        EXPECT_FLOAT_EQ(hourOfClock(7.0 * kDay + 6.0 * 3600.0), 6.f);
        EXPECT_FLOAT_EQ(hourOfClock(-3600.0), 23.f);
        EXPECT_FLOAT_EQ(hourOfClock(std::nan("")), 0.f);
    }

    TEST(SkyrimVampirismSun, daylight_outdoors_hurts_and_night_or_a_roof_does_not)
    {
        SunLibrary lib;
        EXPECT_TRUE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, outdoors()));
        EXPECT_TRUE(sunlit(lib, kSunDamageSpells[0], 5 * 3600.0, outdoors()));
        EXPECT_TRUE(sunlit(lib, kSunDamageSpells[0], 19 * 3600.0, outdoors()));
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 4.9 * 3600.0, outdoors()));
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 19.1 * 3600.0, outdoors()));
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 23 * 3600.0, outdoors()));
        effectconditions::Place inside = outdoors();
        inside.mInterior = true;
        inside.mWorld = 0;
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, inside));
    }

    TEST(SkyrimVampirismSun, the_forgotten_vale_world_and_an_unknown_place_are_no_sun)
    {
        SunLibrary lib;
        effectconditions::Place vale = outdoors();
        vale.mWorld = 0x2EE41;
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, vale));
        effectconditions::Place unknown;
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, unknown));
    }

    // Dawnguard's version of the spell adds a test of a global of its own (GetGlobalValue == 0 on every effect):
    // answered by the caller's lookup (the GLOB's stored value, or what a script set), else the list is unanswered and
    // fails closed.
    TEST(SkyrimVampirismSun, a_global_the_data_adds_is_answered_by_the_callers_lookup_and_unanswered_without_one)
    {
        SunLibrary lib;
        constexpr FormId kOwnGlobal = 0x0200A26E;
        for (SpellEffect& e : lib.mSpell.mEffects)
        {
            e.mConditionList.push_back(cond(74, 0, 0.f, kOwnGlobal));
            e.mConditions = static_cast<std::uint32_t>(e.mConditionList.size());
        }
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, outdoors()));
        float stored = 0.f;
        const effectconditions::Evaluator::GlobalLookup lookup = [&stored](FormId global, float& value) {
            if (global != kOwnGlobal)
                return false;
            value = stored;
            return true;
        };
        EXPECT_TRUE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, outdoors(), lookup));
        stored = 1.f;
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, outdoors(), lookup));
        const effectconditions::Evaluator::GlobalLookup other = [](FormId, float&) { return false; };
        stored = 0.f;
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, outdoors(), other));
    }

    TEST(SkyrimVampirismSun, an_unknown_spell_is_no_sun_and_a_list_the_pack_could_not_decode_fails_closed)
    {
        SunLibrary lib;
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[1], 12 * 3600.0, outdoors()));
        lib.mSpell.mEffects[1].mConditions = 9; // 4 decoded of 9
        EXPECT_FALSE(sunlit(lib, kSunDamageSpells[0], 12 * 3600.0, outdoors()));
    }

    TEST(SkyrimVampirismFilter, sun_damage_spells_leave_with_the_sun_and_a_vampire_gets_the_disease_immunity)
    {
        const std::vector<FormId> held = { 0x1234, kSunDamageSpells[0], kSunDamageSpells[3], 0xED099 };
        const std::vector<FormId> day = filterAbilities(held, true, true);
        EXPECT_EQ(day.size(), 5u);
        EXPECT_NE(std::find(day.begin(), day.end(), kVampirismAbility), day.end());
        EXPECT_NE(std::find(day.begin(), day.end(), kSunDamageSpells[0]), day.end());
        const std::vector<FormId> night = filterAbilities(held, true, false);
        EXPECT_EQ(night.size(), 3u);
        EXPECT_EQ(std::find(night.begin(), night.end(), kSunDamageSpells[3]), night.end());
        const std::vector<FormId> human = filterAbilities(held, false, false);
        EXPECT_EQ(std::find(human.begin(), human.end(), kVampirismAbility), human.end());
        EXPECT_TRUE(std::is_sorted(night.begin(), night.end()));
        EXPECT_TRUE(filterAbilities({}, false, true).empty());
        EXPECT_TRUE(isSunDamageSpell(0xED09B));
        EXPECT_FALSE(isSunDamageSpell(0xED099));
    }

    // ---- the service over a fake script port
    // -------------------------------------------------------------------------------------

    class FakePort final : public ScriptPort
    {
    public:
        bool ready() const override { return mReady; }
        bool change() override
        {
            ++mChanges;
            if (mInstantChange)
                mStatus = 1;
            return true;
        }
        bool feed() override
        {
            ++mFeeds;
            mStatus = 1;
            return true;
        }
        bool tick() override
        {
            ++mTicks;
            return true;
        }
        bool progress(int stage) override
        {
            mProgress.push_back(stage);
            return true;
        }
        int status() const override { return mStatus; }
        bool setStage(int stage, double lastFeed) override
        {
            mStatus = stage;
            mLastFeed = lastFeed;
            return true;
        }
        void setDays(double days) override { mDays = days; }

        bool mReady = true;
        bool mInstantChange = true;
        int mStatus = 0;
        int mChanges = 0, mFeeds = 0, mTicks = 0;
        double mDays = -1.0, mLastFeed = -1.0;
        std::vector<int> mProgress;
    };

    struct ServiceRig
    {
        ServiceRig()
        {
            VampirismService::Hooks hooks;
            hooks.mDays = [this] { return days; };
            hooks.mSanguinare = [this] { return sanguinare; };
            hooks.mCureSanguinare = [this] {
                sanguinare = false;
                ++cures;
            };
            hooks.mSafe = [this] { return safe; };
            hooks.mLog = [this](const std::string& line) { logs.push_back(line); };
            service = std::make_unique<VampirismService>(port, std::move(hooks));
        }
        FakePort port;
        double days = 10.0;
        bool sanguinare = false, safe = true;
        int cures = 0;
        std::vector<std::string> logs;
        std::unique_ptr<VampirismService> service;
    };

    TEST(SkyrimVampirismService, nothing_runs_until_the_script_exists_and_the_days_go_to_the_script)
    {
        ServiceRig rig;
        rig.port.mReady = false;
        rig.sanguinare = true;
        rig.service->poll();
        EXPECT_EQ(rig.port.mDays, -1.0);
        rig.port.mReady = true;
        rig.service->poll();
        EXPECT_EQ(rig.port.mDays, 10.0);
    }

    TEST(SkyrimVampirismService, sanguinare_turns_the_player_after_three_days_and_the_disease_goes)
    {
        ServiceRig rig;
        rig.sanguinare = true;
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 0);
        rig.days = 12.9;
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 0);
        rig.days = 13.0;
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 1);
        rig.service->poll(); // the instant fake reported status 1 in the same poll
        EXPECT_TRUE(rig.service->vampire());
        EXPECT_EQ(rig.service->stage(), 1);
        EXPECT_EQ(rig.cures, 1);
        EXPECT_FALSE(rig.sanguinare);
    }

    TEST(SkyrimVampirismService, a_slow_turn_is_asked_once_not_every_frame)
    {
        ServiceRig rig;
        rig.port.mInstantChange = false;
        rig.sanguinare = true;
        rig.service->poll();
        rig.days = 14.0;
        for (int i = 0; i < 5; ++i)
            rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 1);
        EXPECT_FALSE(rig.service->vampire());
        rig.port.mStatus = 1; // the script finished its waits
        rig.service->poll();
        EXPECT_TRUE(rig.service->vampire());
    }

    TEST(SkyrimVampirismService, a_cure_before_the_three_days_stops_it)
    {
        ServiceRig rig;
        rig.sanguinare = true;
        rig.service->poll();
        rig.sanguinare = false;
        rig.days = 11.0;
        rig.service->poll();
        rig.days = 20.0;
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 0);
        EXPECT_FALSE(rig.service->vampire());
    }

    TEST(SkyrimVampirismService, combat_holds_the_turn_back_until_it_is_over)
    {
        ServiceRig rig;
        rig.sanguinare = true;
        rig.service->poll();
        rig.days = 14.0;
        rig.safe = false;
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 0);
        rig.safe = true;
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 1);
    }

    TEST(SkyrimVampirismService, a_vampire_sends_the_game_time_update_every_twelve_hours_and_not_while_in_combat)
    {
        ServiceRig rig;
        rig.port.mStatus = 2;
        rig.service->poll(); // sees the status: a vampire since day 10
        EXPECT_TRUE(rig.service->vampire());
        EXPECT_EQ(rig.service->stage(), 2);
        rig.days = 10.4;
        rig.service->poll();
        EXPECT_EQ(rig.port.mTicks, 0);
        rig.days = 10.5;
        rig.service->poll();
        EXPECT_EQ(rig.port.mTicks, 1);
        rig.days = 10.9;
        rig.service->poll();
        EXPECT_EQ(rig.port.mTicks, 1);
        rig.days = 11.0;
        rig.safe = false;
        rig.service->poll();
        EXPECT_EQ(rig.port.mTicks, 1);
        rig.safe = true;
        rig.service->poll();
        EXPECT_EQ(rig.port.mTicks, 2);
    }

    TEST(SkyrimVampirismService, feeding_needs_a_vampire_and_resets_the_stage_and_the_clock)
    {
        ServiceRig rig;
        EXPECT_FALSE(rig.service->feed());
        EXPECT_EQ(rig.port.mFeeds, 0);
        rig.port.mStatus = 3;
        rig.service->poll();
        EXPECT_EQ(rig.service->stage(), 3);
        rig.days = 10.3;
        ASSERT_TRUE(rig.service->feed());
        EXPECT_EQ(rig.port.mFeeds, 1);
        rig.service->poll();
        EXPECT_EQ(rig.service->stage(), 1);
        EXPECT_DOUBLE_EQ(rig.service->machine().nextTick(), 10.8);
    }

    TEST(SkyrimVampirismService, a_script_that_forgets_the_status_after_being_seen_means_cured)
    {
        ServiceRig rig;
        rig.port.mStatus = 2;
        rig.service->poll();
        ASSERT_TRUE(rig.service->vampire());
        rig.port.mStatus = 0; // VampireCure
        rig.service->poll();
        EXPECT_FALSE(rig.service->vampire());
        EXPECT_EQ(rig.service->stage(), 0);
    }

    TEST(SkyrimVampirismService, a_status_not_yet_restored_after_a_load_does_not_cure_a_loaded_vampire)
    {
        ServiceRig rig;
        rig.service->restore(true, -1.0, 12.0);
        rig.port.mStatus = 0; // the host has not given the quest its saved variables yet
        rig.service->poll();
        EXPECT_TRUE(rig.service->vampire());
        rig.port.mStatus = 3;
        rig.service->poll();
        EXPECT_EQ(rig.service->stage(), 3);
    }

    TEST(SkyrimVampirismService, the_test_start_turns_the_player_and_sets_the_stage_after_the_turn)
    {
        ServiceRig rig;
        rig.service->startAs(3);
        rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 1);
        rig.service->poll();
        rig.service->poll();
        EXPECT_TRUE(rig.service->vampire());
        EXPECT_EQ(rig.port.mStatus, 3);
        EXPECT_DOUBLE_EQ(rig.port.mLastFeed, 10.0 - 2.0);
        ASSERT_EQ(rig.port.mProgress.size(), 1u);
        EXPECT_EQ(rig.port.mProgress[0], 3);
    }

    TEST(SkyrimVampirismService, the_test_start_at_stage_one_only_turns_the_player)
    {
        ServiceRig rig;
        rig.service->startAs(1);
        for (int i = 0; i < 4; ++i)
            rig.service->poll();
        EXPECT_EQ(rig.port.mChanges, 1);
        EXPECT_TRUE(rig.port.mProgress.empty());
        EXPECT_EQ(rig.service->stage(), 1);
    }

    // ---- the chunk
    // -------------------------------------------------------------------------------------------------------------

    class MemoryWriter final : public openeden::sdk::ChunkWriter
    {
    public:
        void write(const void* data, std::size_t size) override
        {
            const auto* p = static_cast<const std::uint8_t*>(data);
            mBytes.insert(mBytes.end(), p, p + size);
        }
        std::vector<std::uint8_t> mBytes;
    };

    class MemoryReader final : public openeden::sdk::ChunkReader
    {
    public:
        explicit MemoryReader(std::vector<std::uint8_t> bytes)
            : mBytes(std::move(bytes))
        {
        }
        bool read(void* data, std::size_t size) override
        {
            if (size > remaining())
                return false;
            std::memcpy(data, mBytes.data() + mAt, size);
            mAt += size;
            return true;
        }
        std::size_t remaining() const override { return mBytes.size() - mAt; }

    private:
        std::vector<std::uint8_t> mBytes;
        std::size_t mAt = 0;
    };

    TEST(SkyrimVampirismChunk, the_timers_round_trip_and_a_damaged_chunk_changes_nothing)
    {
        ServiceRig a;
        a.service->restore(true, -1.0, 42.5);
        MemoryWriter writer;
        a.service->chunk().write(writer);
        EXPECT_EQ(a.service->chunk().chunkName(), "vampirism");
        EXPECT_EQ(a.service->chunk().chunkVersion(), 1u);
        EXPECT_EQ(writer.mBytes.size(), 17u);

        ServiceRig b;
        MemoryReader good(writer.mBytes);
        ASSERT_TRUE(b.service->chunk().read(good, 1));
        EXPECT_TRUE(b.service->vampire());
        EXPECT_DOUBLE_EQ(b.service->machine().nextTick(), 42.5);

        ServiceRig c;
        MemoryReader wrongVersion(writer.mBytes);
        EXPECT_FALSE(c.service->chunk().read(wrongVersion, 2));
        std::vector<std::uint8_t> cut = writer.mBytes;
        cut.pop_back();
        MemoryReader truncated(cut);
        EXPECT_FALSE(c.service->chunk().read(truncated, 1));
        std::vector<std::uint8_t> longer = writer.mBytes;
        longer.push_back(0);
        MemoryReader tooLong(longer);
        EXPECT_FALSE(c.service->chunk().read(tooLong, 1));
        std::vector<std::uint8_t> badFlag = writer.mBytes;
        badFlag[0] = 7;
        MemoryReader flag(badFlag);
        EXPECT_FALSE(c.service->chunk().read(flag, 1));
        std::vector<std::uint8_t> nan = writer.mBytes;
        const double notANumber = std::nan("");
        std::memcpy(nan.data() + 9, &notANumber, sizeof(double));
        MemoryReader nanReader(nan);
        EXPECT_FALSE(c.service->chunk().read(nanReader, 1));
        EXPECT_FALSE(c.service->vampire());
    }

    TEST(SkyrimVampirismChunk, an_infection_in_progress_is_saved_too)
    {
        ServiceRig a;
        a.service->restore(false, 7.25, -1.0);
        MemoryWriter writer;
        a.service->chunk().write(writer);
        ServiceRig b;
        MemoryReader reader(writer.mBytes);
        ASSERT_TRUE(b.service->chunk().read(reader, 1));
        EXPECT_FALSE(b.service->vampire());
        EXPECT_DOUBLE_EQ(b.service->machine().infectedAt(), 7.25);
    }
}
