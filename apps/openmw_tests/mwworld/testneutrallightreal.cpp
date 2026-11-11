// Task 0113 stage N3a-5: the Morrowind projection of EVERY light of the user's real world against the engine's own light
// data. It reads the folder named by OPENEDEN_TEST_MORROWIND_DATA ("Data Files": Morrowind, Tribunal and Bloodmoon),
// loads every cell the way the game does, and for every placed light compares the neutral object's LightComponent with
// the ESM::Light record the engine draws it from (radius, the three colour bytes, Negative, Off By Default, the raw flag
// word, the animation the engine's light controller would run), and the light the engine would build from the component
// (makeNeutralEsmLight) with the original record. Every interior cell's lighting must be the cell record's own ambient,
// sunlight and fog colour. It prints aggregates only; with the variable unset it does nothing. No game data is in the
// repository.
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <string>
#include <vector>

#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadligh.hpp>
#include <components/esm3/readerscache.hpp>
#include <components/worldmodel/store.hpp>

#include "apps/openmw/mwbase/environment.hpp"
#include "apps/openmw/mwclass/activator.hpp"
#include "apps/openmw/mwclass/apparatus.hpp"
#include "apps/openmw/mwclass/armor.hpp"
#include "apps/openmw/mwclass/bodypart.hpp"
#include "apps/openmw/mwclass/book.hpp"
#include "apps/openmw/mwclass/clothing.hpp"
#include "apps/openmw/mwclass/container.hpp"
#include "apps/openmw/mwclass/creature.hpp"
#include "apps/openmw/mwclass/creaturelevlist.hpp"
#include "apps/openmw/mwclass/door.hpp"
#include "apps/openmw/mwclass/ingredient.hpp"
#include "apps/openmw/mwclass/itemlevlist.hpp"
#include "apps/openmw/mwclass/light.hpp"
#include "apps/openmw/mwclass/lockpick.hpp"
#include "apps/openmw/mwclass/misc.hpp"
#include "apps/openmw/mwclass/npc.hpp"
#include "apps/openmw/mwclass/potion.hpp"
#include "apps/openmw/mwclass/probe.hpp"
#include "apps/openmw/mwclass/repair.hpp"
#include "apps/openmw/mwclass/static.hpp"
#include "apps/openmw/mwclass/weapon.hpp"
#include "apps/openmw/mwstate/loading.hpp"
#include "apps/openmw/mwworld/cell.hpp"
#include "apps/openmw/mwworld/cellstore.hpp"
#include "apps/openmw/mwworld/class.hpp"
#include "apps/openmw/mwworld/esmloader.hpp"
#include "apps/openmw/mwworld/esmstore.hpp"
#include "apps/openmw/mwworld/neutrallight.hpp"
#include "apps/openmw/mwworld/neutralprojection.hpp"
#include "apps/openmw/mwworld/store.hpp"

namespace
{
    using namespace openeden::sdk::game;

    void registerAllClasses()
    {
        MWClass::Activator::registerSelf();
        MWClass::Apparatus::registerSelf();
        MWClass::Armor::registerSelf();
        MWClass::BodyPart::registerSelf();
        MWClass::Book::registerSelf();
        MWClass::Clothing::registerSelf();
        MWClass::Container::registerSelf();
        MWClass::Creature::registerSelf();
        MWClass::CreatureLevList::registerSelf();
        MWClass::Door::registerSelf();
        MWClass::Ingredient::registerSelf();
        MWClass::ItemLevList::registerSelf();
        MWClass::Light::registerSelf();
        MWClass::Lockpick::registerSelf();
        MWClass::Miscellaneous::registerSelf();
        MWClass::Npc::registerSelf();
        MWClass::Potion::registerSelf();
        MWClass::Probe::registerSelf();
        MWClass::Repair::registerSelf();
        MWClass::Static::registerSelf();
        MWClass::Weapon::registerSelf();
    }

    struct QuietLoading : MWState::Loading
    {
        bool run(float) override { return false; }
    };

    std::array<std::uint8_t, 3> bytesOf(std::uint32_t packed)
    {
        return { static_cast<std::uint8_t>(packed & 0xff), static_cast<std::uint8_t>((packed >> 8) & 0xff),
            static_cast<std::uint8_t>((packed >> 16) & 0xff) };
    }

    TEST(NeutralLightReal, everyPlacedLightOfTheUsersMorrowindMatchesTheEnginesLightData)
    {
        const char* folder = std::getenv("OPENEDEN_TEST_MORROWIND_DATA");
        if (folder == nullptr || *folder == '\0')
        {
            std::cout << "OPENEDEN_TEST_MORROWIND_DATA is not set, skipping" << std::endl;
            return;
        }

        QuietLoading loading;
        MWWorld::ESMStore store;
        ESM::ReadersCache readers;
        std::vector<int> versions(3, 0);
        MWWorld::EsmLoader loader(store, readers, nullptr, versions);
        int index = 0;
        for (const char* name : { "Morrowind.esm", "Tribunal.esm", "Bloodmoon.esm" })
        {
            loader.load(std::filesystem::path(folder) / name, index, loading);
            ++index;
        }
        store.setUp();
        store.validateRecords(readers);

        MWBase::Environment environment;
        environment.setESMStore(store);
        registerAllClasses();

        NeutralWorld::IdTable ids;
        const MWWorld::Store<ESM::Cell>& cells = store.get<ESM::Cell>();

        std::size_t placedLights = 0, mismatches = 0, noComponent = 0, componentOnNonLight = 0, backMismatches = 0,
                    lightingMismatches = 0, interiors = 0, interiorsWithLighting = 0;
        std::map<int, std::size_t> animations;
        std::size_t negative = 0, offByDefault = 0;
        std::int32_t radiusMin = 1 << 30, radiusMax = 0;

        for (std::size_t i = 0; i < cells.getSize(); ++i)
        {
            const ESM::Cell* record = cells.at(i);
            MWWorld::CellStore cell(MWWorld::Cell(*record), store, readers);
            cell.load();
            WorldCell neutral;
            std::string error;
            ASSERT_TRUE(MWWorld::projectCell(cell, ids, neutral, error)) << error;

            if (!record->isExterior())
            {
                ++interiors;
                if (neutral.lighting)
                {
                    ++interiorsWithLighting;
                    // Independent derivation from the cell record itself.
                    const bool same = neutral.lighting->ambient == bytesOf(static_cast<std::uint32_t>(record->mAmbi.mAmbient))
                        && neutral.lighting->directional == bytesOf(static_cast<std::uint32_t>(record->mAmbi.mSunlight))
                        && neutral.lighting->fogColor == bytesOf(static_cast<std::uint32_t>(record->mAmbi.mFog));
                    lightingMismatches += same ? 0 : 1;
                }
                else
                    ++lightingMismatches;
            }
            else if (neutral.lighting)
                ++lightingMismatches;

            std::map<std::uint64_t, MWWorld::ConstPtr> engine;
            cell.forEachConst([&](const MWWorld::ConstPtr& ptr) {
                const ESM::RefNum n = ptr.getCellRef().getRefNum();
                engine.emplace((static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.mContentFile)) << 32) | n.mIndex,
                    ptr);
                return true;
            });

            for (const WorldObject& o : neutral.objects)
            {
                const auto found = engine.find(o.payload.handle);
                if (found == engine.end())
                    continue;
                const MWWorld::ConstPtr& ptr = found->second;
                if (ptr.getType() != ESM::Light::sRecordId)
                {
                    componentOnNonLight += o.light.has_value() ? 1 : 0;
                    continue;
                }
                ++placedLights;
                if (!o.light)
                {
                    ++noComponent;
                    continue;
                }
                const ESM::Light& engineLight = *ptr.get<ESM::Light>()->mBase;
                const std::uint32_t color = static_cast<std::uint32_t>(engineLight.mData.mColor);
                const std::int32_t flags = engineLight.mData.mFlags;
                // The component against the record, field by field (written out here, not through lightOf).
                bool same = o.light->radius == static_cast<double>(engineLight.mData.mRadius)
                    && o.light->red == (color & 0xff) && o.light->green == ((color >> 8) & 0xff)
                    && o.light->blue == ((color >> 16) & 0xff) && o.light->fade == 1.0
                    && o.light->negative == ((flags & ESM::Light::Negative) != 0)
                    && o.light->offByDefault == ((flags & ESM::Light::OffDefault) != 0)
                    && o.light->flags == static_cast<std::uint32_t>(flags);
                // The animation the engine's controller would run (components/mwanimation/light.cpp).
                LightAnimation expected = LightAnimation::None;
                if ((flags & ESM::Light::Flicker) != 0)
                    expected = LightAnimation::Flicker;
                else if ((flags & ESM::Light::FlickerSlow) != 0)
                    expected = LightAnimation::FlickerSlow;
                else if ((flags & ESM::Light::Pulse) != 0)
                    expected = LightAnimation::Pulse;
                else if ((flags & ESM::Light::PulseSlow) != 0)
                    expected = LightAnimation::PulseSlow;
                same = same && o.light->animation == expected;
                mismatches += same ? 0 : 1;

                // And what the engine builds from the component draws like the record: radius, colour, animation bit.
                const ESM::Light back = MWWorld::makeNeutralEsmLight(*o.light);
                const int bits = ESM::Light::Flicker | ESM::Light::FlickerSlow | ESM::Light::Pulse | ESM::Light::PulseSlow
                    | ESM::Light::Negative;
                const int expectedBits = (expected == LightAnimation::None ? 0 : (flags & bits)) | (flags & ESM::Light::Negative);
                const bool backSame = back.mData.mRadius == engineLight.mData.mRadius
                    && (static_cast<std::uint32_t>(back.mData.mColor) & 0xffffff) == (color & 0xffffff)
                    && (back.mData.mFlags & bits) == (expectedBits & bits);
                backMismatches += backSame ? 0 : 1;

                ++animations[static_cast<int>(o.light->animation)];
                negative += o.light->negative;
                offByDefault += o.light->offByDefault;
                radiusMin = std::min(radiusMin, engineLight.mData.mRadius);
                radiusMax = std::max(radiusMax, engineLight.mData.mRadius);
            }
        }

        std::cout << "NEUTRAL-LIGHT-REAL cells " << cells.getSize() << " interiors " << interiors
                  << " interiorsWithLighting " << interiorsWithLighting << " placedLights " << placedLights
                  << " mismatches " << mismatches << " backMismatches " << backMismatches << " noComponent "
                  << noComponent << " componentOnNonLight " << componentOnNonLight << " lightingMismatches "
                  << lightingMismatches << " negative " << negative << " offByDefault " << offByDefault
                  << " radius " << radiusMin << ".." << radiusMax;
        for (const auto& [animation, count] : animations)
            std::cout << " anim" << animation << "=" << count;
        std::cout << std::endl;

        EXPECT_GT(placedLights, 0u);
        EXPECT_EQ(mismatches, 0u);
        EXPECT_EQ(backMismatches, 0u);
        EXPECT_EQ(noComponent, 0u);
        EXPECT_EQ(componentOnNonLight, 0u);
        EXPECT_EQ(lightingMismatches, 0u);
    }
}
