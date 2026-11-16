// Task 0213 (1): the game clock after a load. A neutral (Skyrim-only) session has no Morrowind.esm, so the World
// inserts default globals, and the default `gamehour` was an INTEGER variant: every frame the clock wrote the hour into
// it with setFloat, which truncates, the save wrote that global and a load read the whole hour (16.7 loaded at 16.0).
// Written from the cause in the task document before the fix; the first run was against the old table.
#include <gtest/gtest.h>

#include <memory>
#include <sstream>
#include <string>

#include <components/esm3/esmreader.hpp>
#include <components/esm3/esmwriter.hpp>
#include <components/esm3/loadglob.hpp>
#include <components/loadinglistener/loadinglistener.hpp>

#include "apps/openmw/mwworld/defaultglobals.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/globals.hpp"

namespace
{
    // The World's date setup also reads "month"; the neutral start inserts it beside the defaults.
    void addMonth(MWWorld::ESMStore& store)
    {
        ESM::Global record;
        record.mId = ESM::RefId::stringRefId("month");
        record.mValue = ESM::Variant(0);
        record.mRecordFlags = 0;
        store.insertStatic(record);
    }

    std::string writeGlobals(const MWWorld::Globals& globals)
    {
        std::stringstream stream;
        ESM::ESMWriter writer;
        writer.setFormatVersion(ESM::DefaultFormatVersion);
        writer.save(stream);
        Loading::Listener listener;
        globals.write(writer, listener);
        writer.close();
        return stream.str();
    }

    void readGlobals(const std::string& bytes, MWWorld::Globals& globals)
    {
        ESM::ESMReader reader;
        reader.open(std::make_unique<std::istringstream>(bytes), "save");
        while (reader.hasMoreRecs())
        {
            ESM::NAME n = reader.getRecName();
            reader.getRecHeader();
            if (!globals.readRecord(reader, n.toInt()))
                reader.skipRecord();
        }
    }

    // What DateTimeManager does with the clock (datetimemanager.cpp): advanceTime writes the hour with
    // `globals[gamehour].setFloat(hour)` every frame and setup (a load) reads it back with getFloat. Both are private
    // to the World, so the test does the same two calls on the global.
    struct Clock
    {
        MWWorld::ESMStore mStore;
        MWWorld::Globals mGlobals;
        double mHours = 0.0; // the manager's own float-precision hour

        Clock()
        {
            addMonth(mStore);
            MWWorld::insertMissingDefaultGlobals(mStore);
            mGlobals.fill(mStore);
            setup();
        }
        void setup() { mHours = mGlobals[MWWorld::Globals::sGameHour].getFloat(); }
        void advance(double hours)
        {
            mHours += hours;
            mGlobals[MWWorld::Globals::sGameHour].setFloat(static_cast<float>(mHours));
        }
        double hour() const { return mHours; }
    };

    constexpr double kOneSecondInHours = 1.0 / 3600.0;
}

TEST(NeutralClock, the_default_gamehour_is_a_float_global)
{
    Clock clock;
    EXPECT_EQ(clock.mGlobals.getType(MWWorld::Globals::sGameHour), 'f');
}

TEST(NeutralClock, every_other_default_keeps_its_type)
{
    Clock clock;
    EXPECT_EQ(clock.mGlobals.getType(MWWorld::Globals::sTimeScale), 'f');
    EXPECT_EQ(clock.mGlobals.getType(MWWorld::Globals::sDay), 'l');
    EXPECT_EQ(clock.mGlobals.getType(MWWorld::Globals::sYear), 'l');
    EXPECT_EQ(clock.mGlobals.getType(MWWorld::Globals::sDaysPassed), 'l');
}

TEST(NeutralClock, inserting_the_defaults_twice_adds_nothing_the_second_time)
{
    MWWorld::ESMStore store;
    addMonth(store);
    EXPECT_GT(MWWorld::insertMissingDefaultGlobals(store), 0u);
    EXPECT_EQ(MWWorld::insertMissingDefaultGlobals(store), 0u);
}

TEST(NeutralClock, a_save_at_a_fractional_hour_loads_at_the_same_time_to_the_second)
{
    Clock before;
    before.advance(16.7);
    EXPECT_NEAR(before.hour(), 16.7, kOneSecondInHours);

    const std::string bytes = writeGlobals(before.mGlobals);

    Clock after;
    readGlobals(bytes, after.mGlobals);
    after.setup();
    EXPECT_NEAR(after.hour(), 16.7, kOneSecondInHours);
}

TEST(NeutralClock, a_save_early_in_the_day_does_not_load_at_midnight)
{
    Clock before;
    before.advance(0.43);
    Clock after;
    readGlobals(writeGlobals(before.mGlobals), after.mGlobals);
    after.setup();
    EXPECT_NEAR(after.hour(), 0.43, kOneSecondInHours);
}

TEST(NeutralClock, many_small_steps_keep_the_clock_through_a_save)
{
    Clock before;
    for (int i = 0; i < 1000; ++i)
        before.advance(0.0123);
    Clock after;
    readGlobals(writeGlobals(before.mGlobals), after.mGlobals);
    after.setup();
    EXPECT_NEAR(after.hour(), before.hour(), kOneSecondInHours);
    EXPECT_EQ(after.mGlobals.getType(MWWorld::Globals::sGameHour), 'f');
}

// A save made before the fix holds an integer gamehour. Globals::readRecord replaces the whole record, type included,
// so without a coercion the load would bring the truncation back for the rest of the game.
TEST(NeutralClock, an_old_save_with_an_integer_gamehour_loads_into_a_float_global)
{
    // build the old save: a store whose gamehour is an integer
    MWWorld::ESMStore oldStore;
    addMonth(oldStore);
    for (const auto& [name, value] : MWWorld::generateDefaultGlobals())
    {
        ESM::Global record;
        record.mId = ESM::RefId::stringRefId(name.getValue());
        record.mValue = name == MWWorld::Globals::sGameHour ? ESM::Variant(static_cast<int32_t>(16)) : value;
        record.mRecordFlags = 0;
        oldStore.insertStatic(record);
    }
    MWWorld::Globals oldGlobals;
    oldGlobals.fill(oldStore);
    ASSERT_EQ(oldGlobals.getType(MWWorld::Globals::sGameHour), 'l');
    const std::string bytes = writeGlobals(oldGlobals);

    Clock clock;
    readGlobals(bytes, clock.mGlobals);
    EXPECT_EQ(clock.mGlobals.getType(MWWorld::Globals::sGameHour), 'f');
    clock.setup();
    EXPECT_NEAR(clock.hour(), 16.0, kOneSecondInHours);
    clock.advance(0.7);
    EXPECT_NEAR(clock.mGlobals[MWWorld::Globals::sGameHour].getFloat(), 16.7, 1e-4);
}

// Morrowind defines GameHour itself, as a float; its saves must not change.
TEST(NeutralClock, a_global_the_content_defines_as_a_float_is_never_replaced_by_the_default)
{
    MWWorld::ESMStore store;
    addMonth(store);
    ESM::Global hour;
    hour.mId = ESM::RefId::stringRefId("gamehour");
    hour.mValue = ESM::Variant(9.5f);
    hour.mRecordFlags = 0;
    store.insertStatic(hour);
    MWWorld::insertMissingDefaultGlobals(store);
    MWWorld::Globals globals;
    globals.fill(store);
    EXPECT_FLOAT_EQ(globals[MWWorld::Globals::sGameHour].getFloat(), 9.5f);
}

// The coercion is for a float global that a save stored as an integer only: an integer stays an integer.
TEST(NeutralClock, an_integer_global_stays_an_integer_through_a_save)
{
    Clock before;
    before.mGlobals[MWWorld::Globals::sDay].setInteger(12);
    Clock after;
    readGlobals(writeGlobals(before.mGlobals), after.mGlobals);
    EXPECT_EQ(after.mGlobals.getType(MWWorld::Globals::sDay), 'l');
    EXPECT_EQ(after.mGlobals[MWWorld::Globals::sDay].getInteger(), 12);
}
