// Task 0100 stage N1: the Morrowind projection against the engine's own view of the user's REAL cells. It reads the
// folder named by OPENEDEN_TEST_MORROWIND_DATA ("Data Files": Morrowind, Tribunal and Bloodmoon), loads EVERY cell of
// the store into a CellStore the way the game does, and compares what the engine lists (CellStore::forEachConst, which
// is what Scene::insertCell visits) with the neutral cell: the same objects (by reference number), the same record type
// counts, the same positions and scales, the model the class names. It prints aggregates only (counts); with the
// variable unset it does nothing. No game data is in the repository.
#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include <components/esm3/loadcell.hpp>
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
#include "apps/openmw/mwworld/neutralprojection.hpp"
#include "apps/openmw/mwworld/store.hpp"

namespace
{
    using namespace openeden::sdk::game;

    // The classes the game registers (the executable installs the world-object ones from the Morrowind pack).
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

    TEST(NeutralProjectionReal, everyCellOfTheUsersMorrowindProjectsToWhatTheEngineLists)
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

        // The actors' class asks the environment for the store when it names a model.
        MWBase::Environment environment;
        environment.setESMStore(store);
        registerAllClasses();

        NeutralWorld::IdTable ids;
        MWWorld::ProjectionStats stats;
        const MWWorld::Store<ESM::Cell>& cells = store.get<ESM::Cell>();

        std::size_t cellsChecked = 0, interiors = 0, exteriors = 0, emptyCells = 0;
        std::size_t engineObjects = 0, neutralObjects = 0, dropped = 0;
        std::size_t countMismatches = 0, identityMismatches = 0, typeMismatches = 0, fieldMismatches = 0,
                    modelMismatches = 0, invalid = 0, stateChanged = 0, projectFailures = 0;
        std::map<std::string, std::size_t> perType; // engine record type description -> count
        std::map<int, std::size_t> perKind;
        std::map<int, std::size_t> perCollision;
        std::size_t withOwner = 0, withLock = 0, withTeleport = 0, disabled = 0, scaled = 0, noModel = 0;
        std::size_t biggest = 0;

        for (std::size_t i = 0; i < cells.getSize(); ++i)
        {
            const ESM::Cell* record = cells.at(i);
            MWWorld::CellStore cell(MWWorld::Cell(*record), store, readers);
            cell.load();
            const bool hadState = cell.hasState();

            NeutralWorld::IdTable& table = ids;
            WorldCell neutral;
            std::string error;
            if (!MWWorld::projectCell(cell, table, neutral, error, &stats))
            {
                ++projectFailures;
                ADD_FAILURE() << "projection failed: " << error;
                continue;
            }
            if (cell.hasState() != hadState)
                ++stateChanged;
            ++cellsChecked;
            (record->isExterior() ? exteriors : interiors) += 1;

            // The engine's view.
            std::map<std::uint64_t, MWWorld::ConstPtr> engine;
            std::map<std::string, std::size_t> engineTypes;
            std::map<int, std::size_t> engineKinds;
            std::size_t engineCount = 0;
            cell.forEachConst([&](const MWWorld::ConstPtr& ptr) {
                const ESM::RefNum n = ptr.getCellRef().getRefNum();
                const std::uint64_t key
                    = (static_cast<std::uint64_t>(static_cast<std::uint32_t>(n.mContentFile)) << 32) | n.mIndex;
                engine.emplace(key, ptr);
                ++engineTypes[std::string(ptr.getTypeDescription())];
                ++engineKinds[static_cast<int>(MWWorld::kindOfRecordType(ptr.getType()))];
                ++engineCount;
                return true;
            });

            if (engineCount == 0)
                ++emptyCells;
            engineObjects += engineCount;
            neutralObjects += neutral.objects.size();
            dropped += neutral.droppedObjects;
            biggest = std::max(biggest, neutral.objects.size());
            if (neutral.objects.size() + neutral.droppedObjects != engineCount)
                ++countMismatches;

            std::map<int, std::size_t> neutralKinds;
            std::set<std::uint64_t> neutralKeys;
            for (const WorldObject& o : neutral.objects)
            {
                ++neutralKinds[static_cast<int>(o.kind)];
                ++perKind[static_cast<int>(o.kind)];
                ++perCollision[static_cast<int>(o.collision.source)];
                neutralKeys.insert(o.payload.handle);
                withOwner += o.owner.has_value();
                withLock += o.lock.has_value();
                withTeleport += o.teleport.has_value();
                disabled += o.initiallyDisabled;
                scaled += o.transform.scale != 1.0;
                noModel += o.modelPath.empty();

                const auto found = engine.find(o.payload.handle);
                if (found == engine.end())
                {
                    ++identityMismatches;
                    continue;
                }
                const MWWorld::ConstPtr& ptr = found->second;
                const ESM::Position& p = ptr.getRefData().getPosition();
                if (o.transform.position.x != p.pos[0] || o.transform.position.y != p.pos[1]
                    || o.transform.position.z != p.pos[2] || o.transform.scale != ptr.getCellRef().getScale())
                    ++fieldMismatches;
                if (o.kind != MWWorld::kindOfRecordType(ptr.getType()))
                    ++typeMismatches;
                if (o.initiallyDisabled == ptr.getRefData().isEnabled())
                    ++fieldMismatches;
                // The model: the class's own answer (a hidden marker has none).
                std::string expected;
                try
                {
                    expected = std::string(ptr.getClass().getModel(ptr));
                }
                catch (const std::exception&)
                {
                    expected = o.modelPath; // counted in stats.mModelFailures
                }
                const bool hidden = ptr.getCellRef().getRefId() == "prisonmarker"
                    || ptr.getCellRef().getRefId() == "divinemarker" || ptr.getCellRef().getRefId() == "templemarker"
                    || ptr.getCellRef().getRefId() == "northmarker";
                if (hidden ? !o.modelPath.empty() : o.modelPath != expected)
                    ++modelMismatches;
            }
            if (neutral.droppedObjects == 0)
            {
                std::set<std::uint64_t> engineKeys;
                for (const auto& [key, ptr] : engine)
                    engineKeys.insert(key);
                if (engineKeys != neutralKeys)
                    ++identityMismatches;
                if (neutralKinds != engineKinds)
                    ++typeMismatches;
            }
            for (const auto& [name, count] : engineTypes)
                perType[name] += count;

            std::string problem;
            if (!NeutralWorld::validateCell(neutral, {}, problem))
            {
                ++invalid;
                ADD_FAILURE() << "cell " << record->mName << ": " << problem;
            }
        }

        std::cout << "cells projected " << cellsChecked << " (interior " << interiors << ", exterior " << exteriors
                  << ", with no objects " << emptyCells << ", largest " << biggest << " objects)\n";
        std::cout << "objects: engine " << engineObjects << ", neutral " << neutralObjects << ", dropped " << dropped
                  << "\n";
        std::cout << "per record type (engine view):";
        for (const auto& [name, count] : perType)
            std::cout << ' ' << name << '=' << count;
        std::cout << "\nper neutral kind (BaseKind number):";
        for (const auto& [kind, count] : perKind)
            std::cout << ' ' << kind << '=' << count;
        std::cout << "\ncollision hints (None=0, FromModel=1, Box=2):";
        for (const auto& [source, count] : perCollision)
            std::cout << ' ' << source << '=' << count;
        std::cout << "\nwith owner " << withOwner << ", lock " << withLock << ", teleport " << withTeleport
                  << ", disabled " << disabled << ", scaled " << scaled << ", no model " << noModel << "\n";
        std::cout << "projection stats: model failures " << stats.mModelFailures << ", no refnum " << stats.mNoRefNum
                  << ", duplicate refnum " << stats.mDuplicateRefNum << ", dropped by ids " << stats.mDroppedByIds
                  << "; ids minted " << ids.size() << "\n";
        std::cout << "mismatches: count " << countMismatches << ", identity " << identityMismatches << ", type "
                  << typeMismatches << ", field " << fieldMismatches << ", model " << modelMismatches << ", invalid "
                  << invalid << ", state changed " << stateChanged << ", failures " << projectFailures << std::endl;

        EXPECT_GT(cellsChecked, 1000u);
        EXPECT_EQ(countMismatches, 0u);
        EXPECT_EQ(identityMismatches, 0u);
        EXPECT_EQ(typeMismatches, 0u);
        EXPECT_EQ(fieldMismatches, 0u);
        EXPECT_EQ(modelMismatches, 0u);
        EXPECT_EQ(invalid, 0u);
        EXPECT_EQ(stateChanged, 0u);
        EXPECT_EQ(dropped, 0u);
    }

    TEST(NeutralProjectionReal, theNamedCellsGiveTheSameNeutralCellTwiceAndThroughTheCoreStore)
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
        std::vector<std::unique_ptr<MWWorld::CellStore>> keep;
        MWWorld::MorrowindCellSource source(store, ids, [&](const ESM::RefId& id) -> MWWorld::CellStore* {
            const ESM::Cell* record = store.get<ESM::Cell>().search(id);
            if (record == nullptr)
                return nullptr;
            keep.push_back(std::make_unique<MWWorld::CellStore>(MWWorld::Cell(*record), store, readers));
            return keep.back().get();
        });
        NeutralWorld::NeutralWorldStore core;
        std::string error;
        ASSERT_TRUE(core.addSource(source, error)) << error;

        std::vector<WorldId> all;
        ASSERT_EQ(source.listCells(0, 100000, all), source.cellCount());
        EXPECT_EQ(all.size(), store.get<ESM::Cell>().getSize());

        // Balmora (-3, -2), Seyda Neen (-2, -9), Caldera (-3, 2) and Vivec's Hlaalu plaza, and every cell named
        // "Vivec".
        std::vector<const ESM::Cell*> wanted;
        for (const auto& grid : { std::pair<int, int>{ -3, -2 }, { -2, -9 }, { -3, 2 }, { 9, 5 } })
            if (const ESM::Cell* c = store.get<ESM::Cell>().search(grid.first, grid.second))
                wanted.push_back(c);
        for (std::size_t i = 0; i < store.get<ESM::Cell>().getSize(); ++i)
        {
            const ESM::Cell* c = store.get<ESM::Cell>().at(i);
            if (!c->isExterior() && c->mName.find("Vivec") == 0 && wanted.size() < 12)
                wanted.push_back(c);
        }
        ASSERT_GE(wanted.size(), 4u);
        for (const ESM::Cell* record : wanted)
        {
            const WorldId id
                = ids.find(static_cast<std::uint8_t>(MWWorld::MorrowindIdSpace::Cell), record->mId.toDebugString());
            ASSERT_NE(id, kNoWorldId);
            const WorldCell* first = core.cell("morrowind", id, error);
            ASSERT_NE(first, nullptr) << error;
            const WorldCell copy = *first;
            core.clear();
            const WorldCell* second = core.cell("morrowind", id, error);
            ASSERT_NE(second, nullptr) << error;
            EXPECT_EQ(copy, *second) << record->mName;
            std::cout << (record->isExterior() ? "exterior (" + std::to_string(record->getGridX()) + ", "
                        + std::to_string(record->getGridY()) + ")"
                                               : "interior '" + record->mName + "'")
                      << ": " << copy.objects.size() << " objects\n";
        }
    }
}
