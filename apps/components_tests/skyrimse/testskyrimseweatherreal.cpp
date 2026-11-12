// Task 0127 (TBD-E): the aggregate check of the Skyrim SE weather decoding on the player's own masters. It prints counts
// only (lines starting SKYRIMSE-WEATHER), never a record name, id or mod path, and writes nothing. Skipped when the
// environment is not set.
//
//   OPENEDEN_SKYRIMSE_DATA   the game's Data folder (required)
//
// The numbers asserted at the end are the ones tools/skyrimse_weather_check/survey.py measured independently with a
// separate parser, so a regression in the decoder shows.
#include <game_skyrimse/pack_core.hpp>
#include <game_skyrimse/weather.hpp>

#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    namespace fs = std::filesystem;
    using namespace game_skyrimse;

    const char* const kMasters[] = { "Skyrim.esm", "Update.esm", "Dawnguard.esm", "HearthFires.esm", "Dragonborn.esm" };

    // Every record id the load order defines (to tell a dangling reference from a resolved one).
    class IdCollector final : public Content::Visitor
    {
    public:
        IdCollector(const FormIdResolver& resolver, std::set<FormId>& ids)
            : mResolver(resolver)
            , mIds(ids)
        {
        }
        bool onRecord(const Content::RawRecord& record) override
        {
            if (const std::optional<FormId> id = mResolver.resolve(record.mFormId))
                mIds.insert(*id);
            return true;
        }

    private:
        const FormIdResolver& mResolver;
        std::set<FormId>& mIds;
    };

    TEST(SkyrimWeatherRealData, theFiveMastersDecodeAndTheNumbersMatchTheIndependentSurvey)
    {
        const char* env = std::getenv("OPENEDEN_SKYRIMSE_DATA");
        if (env == nullptr || !fs::is_directory(env))
            GTEST_SKIP() << "OPENEDEN_SKYRIMSE_DATA is not set";
        const fs::path data = env;

        LoadOrder order;
        std::string err;
        for (const char* name : kMasters)
            ASSERT_TRUE(order.add(name, false, err)) << err;

        std::set<FormId> defined;
        for (const char* name : kMasters)
        {
            Content::Error error;
            const auto plugin = Content::Plugin::openFile(data / name, error);
            ASSERT_NE(plugin, nullptr) << error.describe();
            const FormIdResolver resolver(plugin->header(), name, order);
            IdCollector collector(resolver, defined);
            Content::WalkOptions options;
            options.mReadData = false;
            ASSERT_FALSE(plugin->walk(collector, options).has_value());
        }

        WeatherIndex index;
        std::uint64_t seen[5] = {}, parsed[5] = {}, failed[5] = {}, walkErrors = 0, odd = 0;
        std::map<std::string, std::uint64_t> oddBy;
        std::map<std::uint32_t, std::uint64_t> colourTypes, slotCounts, layerCounts, flagValues, dalcCounts;
        std::uint64_t weathers = 0, clouds = 0, texturedLayers = 0, wlstEntries = 0, rdwtLists = 0, rdwtEntries = 0,
                      rdwtEmpty = 0, rdwtGlobals = 0, wlstGlobals = 0, sounds = 0, soundsDangling = 0, skyStatics = 0,
                      skyStaticsDangling = 0, unresolved = 0, nam2 = 0, onam = 0, imageSpaces = 0, volumetric = 0,
                      regionWeatherLists = 0;
        std::map<std::int64_t, std::uint64_t> chanceSums;
        std::uint64_t cellsExterior = 0, cellsInterior = 0, xclrCells = 0, xcwtExterior = 0, xccmInterior = 0,
                      xccmExterior = 0, xcimCells = 0, gridCells = 0, regionsDangling = 0, regionRefs = 0;
        std::uint64_t worldsSeen = 0;
        std::vector<CellWeatherLinks> allCells;
        for (const char* name : kMasters)
        {
            WeatherRecords d;
            Content::Error error;
            if (!parseWeatherRecordsFile(data / name, order, WeatherLimits(), d, error))
            {
                std::cout << "SKYRIMSE-WEATHER plugin failed " << error.mMessage << "\n";
                ++walkErrors;
                continue;
            }
            const std::uint32_t types[] = { Content::fourCC("WTHR"), Content::fourCC("CLMT"), Content::fourCC("REGN"),
                Content::fourCC("WRLD"), Content::fourCC("CELL") };
            for (int i = 0; i < 5; ++i)
            {
                const TypeCount& c = d.mCounts[types[i]];
                seen[i] += c.mSeen;
                parsed[i] += c.mParsed;
                failed[i] += c.mFailed;
            }
            odd += d.mNotes.mOddSubrecords;
            for (const auto& [k, v] : d.mNotes.mOddBy)
                oddBy[k] += v;
            unresolved += d.mNotes.mUnresolvedFormIds;
            worldsSeen += d.mWorlds.size();
            for (const WeatherRecord& w : d.mWeathers)
            {
                ++weathers;
                ++colourTypes[w.mColourTypes];
                ++slotCounts[w.mCloudSlots];
                if (w.mLayerCount)
                    ++layerCounts[*w.mLayerCount];
                for (const CloudLayer& l : w.mCloudLayers)
                    if (l.mHasTexture)
                        ++texturedLayers;
                if (w.mData)
                    ++flagValues[w.mData->mFlags];
                ++dalcCounts[static_cast<std::uint32_t>(w.mDirectionalAmbient.size()) * 100
                    + (w.mDirectionalAmbient.empty() || !w.mDirectionalAmbient[0].mHasSpecular ? 24 : 32)];
                for (const WeatherSound& s : w.mSounds)
                {
                    ++sounds;
                    if (s.mSound != kUnresolvedFormId && defined.count(s.mSound) == 0)
                        ++soundsDangling;
                }
                for (FormId s : w.mSkyStatics)
                {
                    ++skyStatics;
                    if (defined.count(s) == 0)
                        ++skyStaticsDangling;
                }
                if (w.mNam2)
                    ++nam2;
                if (w.mOnam)
                    ++onam;
                if (w.mHasImageSpaces)
                    ++imageSpaces;
                if (w.mHasVolumetric)
                    ++volumetric;
            }
            clouds += d.mClimates.size();
            for (const ClimateRecord& c : d.mClimates)
            {
                wlstEntries += c.mWeathers.size();
                for (const WeatherChance& e : c.mWeathers)
                    wlstGlobals += e.mGlobal != kNullFormId ? 1 : 0;
            }
            for (const RegionWeatherRecord& r : d.mRegions)
            {
                if (!r.mHasWeather)
                    continue;
                ++rdwtLists;
                if (r.mWeathers.empty())
                    ++rdwtEmpty;
                std::int64_t sum = 0;
                for (const WeatherChance& e : r.mWeathers)
                {
                    ++rdwtEntries;
                    sum += e.mChance;
                    rdwtGlobals += e.mGlobal != kNullFormId ? 1 : 0;
                }
                if (!r.mWeathers.empty())
                    ++chanceSums[sum];
            }
            for (const CellWeatherLinks& c : d.mCells)
            {
                const bool exterior = c.mWorld != kNullFormId;
                ++(exterior ? cellsExterior : cellsInterior);
                if (!c.mRegions.empty())
                    ++xclrCells;
                for (FormId r : c.mRegions)
                {
                    ++regionRefs;
                    if (defined.count(r) == 0)
                        ++regionsDangling;
                }
                if (exterior && c.mWater != kNullFormId)
                    ++xcwtExterior;
                if (c.mWeatherRegion != kNullFormId)
                    ++(exterior ? xccmExterior : xccmInterior);
                if (c.mImageSpace != kNullFormId)
                    ++xcimCells;
                if (c.mGrid)
                    ++gridCells;
            }
            allCells.insert(allCells.end(), d.mCells.begin(), d.mCells.end());
            index.add(d);
        }
        regionWeatherLists = rdwtLists;

        std::cout << "SKYRIMSE-WEATHER walk errors " << walkErrors << " odd subrecords " << odd << " unresolved ids "
                  << unresolved << "\n";
        for (const auto& [k, v] : oddBy)
            std::cout << "SKYRIMSE-WEATHER odd " << k << " " << v << "\n";
        const char* const names[] = { "WTHR", "CLMT", "REGN", "WRLD", "CELL" };
        for (int i = 0; i < 5; ++i)
            std::cout << "SKYRIMSE-WEATHER " << names[i] << " seen " << seen[i] << " parsed " << parsed[i] << " failed "
                      << failed[i] << "\n";
        std::cout << "SKYRIMSE-WEATHER unique weathers " << index.weatherCount() << " climates " << index.climateCount()
                  << " regions " << index.regionCount() << "\n";
        for (const auto& [k, v] : colourTypes)
            std::cout << "SKYRIMSE-WEATHER colour types " << k << ": " << v << " weathers\n";
        for (const auto& [k, v] : slotCounts)
            std::cout << "SKYRIMSE-WEATHER cloud slots " << k << ": " << v << " weathers\n";
        for (const auto& [k, v] : layerCounts)
            std::cout << "SKYRIMSE-WEATHER LNAM " << k << ": " << v << " weathers\n";
        for (const auto& [k, v] : flagValues)
            std::cout << "SKYRIMSE-WEATHER flags " << k << ": " << v << " weathers\n";
        for (const auto& [k, v] : dalcCounts)
            std::cout << "SKYRIMSE-WEATHER DALC count*100+size " << k << ": " << v << " weathers\n";
        std::cout << "SKYRIMSE-WEATHER textured cloud layers " << texturedLayers << " sounds " << sounds
                  << " (dangling " << soundsDangling << ") sky statics " << skyStatics << " (dangling "
                  << skyStaticsDangling << ") with NAM2 " << nam2 << " with ONAM " << onam << " image-space blocks "
                  << imageSpaces << " volumetric blocks " << volumetric << "\n";
        std::cout << "SKYRIMSE-WEATHER climate weather entries " << wlstEntries << " (globals " << wlstGlobals
                  << "); region weather lists " << regionWeatherLists << " entries " << rdwtEntries << " empty "
                  << rdwtEmpty << " globals " << rdwtGlobals << "\n";
        for (const auto& [k, v] : chanceSums)
            std::cout << "SKYRIMSE-WEATHER region chance sum " << k << ": " << v << " lists\n";
        std::cout << "SKYRIMSE-WEATHER cells exterior " << cellsExterior << " interior " << cellsInterior
                  << " with XCLR " << xclrCells << " region refs " << regionRefs << " (dangling " << regionsDangling
                  << ") XCWT exterior " << xcwtExterior << " XCCM interior " << xccmInterior << " exterior "
                  << xccmExterior << " XCIM " << xcimCells << " with grid " << gridCells << "\n";

        // Worlds after the merge: the climate each ends with.
        std::uint64_t worlds = 0, withOwn = 0, withClimate = 0;
        std::set<FormId> worldIds;
        for (const char* name : kMasters)
        {
            WeatherRecords d;
            Content::Error error;
            if (!parseWeatherRecordsFile(data / name, order, WeatherLimits(), d, error))
                continue;
            for (const WorldSpaceRecord& w : d.mWorlds)
                worldIds.insert(w.mCommon.mId);
        }
        std::string whiterunClimate, tamrielClimate;
        for (FormId id : worldIds)
        {
            const WorldSpaceRecord* w = index.world(id);
            if (w == nullptr)
                continue;
            ++worlds;
            if (w->mClimate != kNullFormId)
                ++withOwn;
            const std::optional<FormId> climate = index.climateOfWorld(id);
            if (climate)
                ++withClimate;
            const ClimateRecord* c = climate ? index.climate(*climate) : nullptr;
            if (w->mCommon.mEditorId == "WhiterunWorld" && c != nullptr)
                whiterunClimate = c->mCommon.mEditorId;
            if (w->mCommon.mEditorId == "Tamriel" && c != nullptr)
                tamrielClimate = c->mCommon.mEditorId;
        }
        std::cout << "SKYRIMSE-WEATHER worlds " << worlds << " with own climate " << withOwn << " with climate after "
                  << "inheritance " << withClimate << " (WhiterunWorld takes the climate of Tamriel: "
                  << (!whiterunClimate.empty() && whiterunClimate == tamrielClimate ? "yes" : "no") << ")\n";

        // The effective list per cell kind, and one deterministic pick.
        std::uint64_t fromRegion = 0, fromClimate = 0, none = 0, interiorFromClimate = 0, rolled = 0;
        for (const CellWeatherLinks& c : allCells)
        {
            const WeatherIndex::EffectiveList list = index.effectiveWeatherList(c);
            const bool exterior = c.mWorld != kNullFormId;
            if (list.mSource == WeatherIndex::EffectiveList::Source::Region)
                ++fromRegion;
            else if (list.mSource == WeatherIndex::EffectiveList::Source::Climate)
                ++(exterior ? fromClimate : interiorFromClimate);
            else if (exterior)
                ++none;
            if (list.mWeathers != nullptr && WeatherIndex::pickWeather(*list.mWeathers, 0.5))
                ++rolled;
        }
        std::cout << "SKYRIMSE-WEATHER cell records: weather list from a region " << fromRegion
                  << ", from the climate (exterior) " << fromClimate << ", from the climate (interior) "
                  << interiorFromClimate << ", none (exterior) " << none << "; a pick at 0.5 succeeds for " << rolled
                  << "\n";

        EXPECT_EQ(walkErrors, 0u);
        EXPECT_EQ(odd, 0u) << "a subrecord of an unexpected size appeared";
        EXPECT_EQ(unresolved, 0u);
        for (int i = 0; i < 5; ++i)
            EXPECT_EQ(failed[i], 0u) << names[i];
        EXPECT_EQ(seen[0], 177u);
        EXPECT_EQ(seen[1], 9u);
        EXPECT_EQ(seen[2], 389u);
        EXPECT_EQ(colourTypes[17], 165u);
        EXPECT_EQ(colourTypes[14], 11u);
        EXPECT_EQ(colourTypes[13], 1u);
        EXPECT_EQ(layerCounts[29], 164u);
        EXPECT_EQ(layerCounts[4], 13u);
        EXPECT_EQ(sounds, 76u);
        EXPECT_EQ(soundsDangling, 4u);
        EXPECT_EQ(skyStatics, 15984u);
        EXPECT_EQ(skyStaticsDangling, 0u);
        EXPECT_EQ(wlstEntries, 10u);
        EXPECT_EQ(wlstGlobals, 0u);
        EXPECT_EQ(rdwtLists, 91u); // RDAT sections of type 3: 88 have an RDWT
        EXPECT_EQ(rdwtEntries, 329u);
        EXPECT_EQ(rdwtEmpty, 3u);
        EXPECT_EQ(rdwtGlobals, 0u);
        EXPECT_EQ(chanceSums[100], 87u);
        EXPECT_EQ(chanceSums[85], 1u);
        EXPECT_EQ(nam2, 11u);
        EXPECT_EQ(onam, 1u);
        EXPECT_EQ(worlds, 49u);
        EXPECT_EQ(withOwn, 20u);
        EXPECT_EQ(withClimate, 42u);
        EXPECT_EQ(whiterunClimate, tamrielClimate);
        EXPECT_FALSE(tamrielClimate.empty());
        EXPECT_EQ(xccmInterior, 314u);
        EXPECT_EQ(xccmExterior, 0u);
        EXPECT_EQ(xclrCells, 33441u);
        EXPECT_EQ(regionRefs, 80355u);
        EXPECT_EQ(regionsDangling, 30u);
        EXPECT_EQ(xcwtExterior, 1246u);
        EXPECT_EQ(xcimCells, 1051u);
        EXPECT_EQ(cellsInterior, 929u);
        EXPECT_EQ(cellsExterior, 75776u);
    }
}
