#include "neutralprojection.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <utility>
#include <vector>

#include <components/esm/defs.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/misc/constants.hpp>
#include <components/misc/convert.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/misc/vsgmath.hpp>

#include "cellstore.hpp"
#include "class.hpp"
#include "esmstore.hpp"
#include "ptr.hpp"
#include "store.hpp"

namespace MWWorld
{
    using namespace openeden::sdk::game;

    namespace
    {
        constexpr std::uint8_t space(MorrowindIdSpace s)
        {
            return static_cast<std::uint8_t>(s);
        }

        WorldId formId(NeutralWorld::IdTable& ids, const ESM::RefId& id)
        {
            if (id.empty())
                return kNoWorldId;
            return ids.idFor(space(MorrowindIdSpace::Form), id.toDebugString());
        }

        WorldId cellId(NeutralWorld::IdTable& ids, const ESM::RefId& id)
        {
            if (id.empty())
                return kNoWorldId;
            return ids.idFor(space(MorrowindIdSpace::Cell), id.toDebugString());
        }

        std::uint64_t placementKey(const ESM::RefNum& refNum)
        {
            return (static_cast<std::uint64_t>(static_cast<std::uint32_t>(refNum.mContentFile)) << 32) | refNum.mIndex;
        }

        // The engine's own rotation (apps/openmw/mwworld/scene.cpp makeDirectNodeRotation): actors turn about Z only,
        // everything else combines the three stored angles with Misc::Convert::makeOsgQuat. This is THE place where
        // Morrowind's stored angles become the neutral quaternion.
        Quatd rotationOf(const ESM::Position& position, bool actor)
        {
            const vsg::dquat q = actor ? Misc::VsgMath::makeRotateFromAxis(position.rot[2], vsg::vec3(0, 0, -1))
                                       : Misc::Convert::makeOsgQuat(position);
            return Quatd{ q.x, q.y, q.z, q.w };
        }

        // The id as text: the string itself for the usual string ids (what the data stores), else the debug text.
        std::string textOf(const ESM::RefId& id)
        {
            try
            {
                return id.getRefIdString();
            }
            catch (const std::exception&)
            {
                return id.toDebugString();
            }
        }

        bool finite(const Vec3d& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool finite(const Quatd& q)
        {
            return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w);
        }
    }

    ObjectKind kindOfRecordType(unsigned int recordType)
    {
        switch (recordType)
        {
            case ESM::REC_STAT:
                return ObjectKind::Static;
            case ESM::REC_DOOR:
                return ObjectKind::Door;
            case ESM::REC_ACTI:
                return ObjectKind::Activator;
            case ESM::REC_CONT:
                return ObjectKind::Container;
            case ESM::REC_LIGH:
                return ObjectKind::Light;
            case ESM::REC_NPC_:
            case ESM::REC_CREA:
                return ObjectKind::Actor;
            case ESM::REC_LEVC:
            case ESM::REC_LEVI:
                return ObjectKind::LeveledList;
            case ESM::REC_ALCH:
            case ESM::REC_APPA:
            case ESM::REC_ARMO:
            case ESM::REC_BOOK:
            case ESM::REC_CLOT:
            case ESM::REC_INGR:
            case ESM::REC_LOCK:
            case ESM::REC_MISC:
            case ESM::REC_PROB:
            case ESM::REC_REPA:
            case ESM::REC_WEAP:
                return ObjectKind::Item;
            default:
                return ObjectKind::Other;
        }
    }

    bool projectCell(
        const CellStore& cell, NeutralWorld::IdTable& ids, WorldCell& out, std::string& error, ProjectionStats* stats)
    {
        out = WorldCell{};
        ProjectionStats local;
        ProjectionStats& counters = stats != nullptr ? *stats : local;

        if (cell.getState() != CellStore::State_Loaded)
        {
            error = "the cell is not loaded";
            return false;
        }
        const Cell& source = *cell.getCell();

        WorldCell result;
        result.id = cellId(ids, source.getId());
        if (result.id == kNoWorldId)
        {
            error = "the id table is full or the cell has no id";
            return false;
        }
        result.editorId = std::string(source.getNameId());
        result.name = std::string(source.getDisplayName());
        result.interior = !source.isExterior();
        result.hasGrid = source.isExterior();
        result.gridX = source.isExterior() ? source.getGridX() : 0;
        result.gridY = source.isExterior() ? source.getGridY() : 0;

        // What the loop reads from the store; the ids are minted afterwards, in the order of the reference numbers, so
        // the same cell gives the same ids whatever order the store lists its references in.
        struct Item
        {
            std::uint64_t mKey = 0;
            WorldObject mObject;
            ESM::RefId mBase, mOwner, mFaction, mLockKey, mDestination;
            bool mTeleport = false;
        };
        std::vector<Item> items;
        std::uint32_t dropped = 0;
        cell.forEachConst([&](const ConstPtr& ptr) {
            const CellRef& ref = ptr.getCellRef();
            const ESM::RefNum refNum = ref.getRefNum();
            if (refNum.isZeroOrUnset())
            {
                ++counters.mNoRefNum;
                ++dropped;
                return true;
            }

            Item item;
            item.mKey = placementKey(refNum);
            WorldObject& object = item.mObject;
            item.mBase = ref.getRefId();
            object.baseEditorId = textOf(item.mBase);
            object.kind = kindOfRecordType(ptr.getType());

            const Class& cls = ptr.getClass();
            if (!Misc::ResourceHelpers::isHiddenMarker(item.mBase))
            {
                try
                {
                    object.modelPath = std::string(cls.getModel(ptr));
                }
                catch (const std::exception&)
                {
                    ++counters.mModelFailures;
                }
            }
            object.collision.source = object.modelPath.empty() ? CollisionSource::None : CollisionSource::FromModel;

            const ESM::Position& position = ptr.getRefData().getPosition();
            object.transform.position = Vec3d{ position.pos[0], position.pos[1], position.pos[2] };
            object.transform.rotation = rotationOf(position, cls.isActor());
            object.transform.scale = ref.getScale();
            if (!finite(object.transform.position) || !finite(object.transform.rotation)
                || !std::isfinite(object.transform.scale))
            {
                ++dropped;
                return true;
            }

            object.initiallyDisabled = !ptr.getRefData().isEnabled();

            item.mOwner = ref.getOwner();
            item.mFaction = ref.getFaction();
            if (!item.mOwner.empty())
                object.owner = OwnerComponent{ kNoWorldId, -1 };
            else if (!item.mFaction.empty())
                object.owner = OwnerComponent{ kNoWorldId, std::max(ref.getFactionRank(), -1) };

            const int lockLevel = ref.getLockLevel();
            item.mLockKey = ref.getKey();
            if (lockLevel != 0 || !item.mLockKey.empty())
                object.lock = LockComponent{ lockLevel > 0, std::abs(lockLevel), kNoWorldId };

            if (ptr.getType() == ESM::REC_DOOR && ref.getTeleport())
            {
                item.mTeleport = true;
                item.mDestination = ref.getDestCell();
                const ESM::Position destination = ref.getDoorDest();
                TeleportComponent teleport;
                teleport.destination.position = Vec3d{ destination.pos[0], destination.pos[1], destination.pos[2] };
                teleport.destination.rotation = rotationOf(destination, true);
                object.teleport = teleport;
            }

            object.payload.handle = item.mKey;
            items.push_back(std::move(item));
            return true;
        });

        // Ids are minted in the order of the reference numbers; then the objects are listed by id (the WorldCell
        // contract).
        std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) { return a.mKey < b.mKey; });
        result.objects.reserve(items.size());
        for (std::size_t i = 0; i < items.size(); ++i)
        {
            if (i > 0 && items[i].mKey == items[i - 1].mKey)
            {
                ++counters.mDuplicateRefNum;
                ++dropped;
                continue;
            }
            Item& item = items[i];
            WorldObject& object = item.mObject;
            object.id = ids.idForNumber(space(MorrowindIdSpace::Placement), item.mKey);
            if (object.id == kNoWorldId)
            {
                ++counters.mDroppedByIds;
                ++dropped;
                continue;
            }
            object.base = formId(ids, item.mBase);
            if (object.owner)
                object.owner->owner = formId(ids, !item.mOwner.empty() ? item.mOwner : item.mFaction);
            if (object.lock)
                object.lock->key = formId(ids, item.mLockKey);
            if (item.mTeleport)
                object.teleport->destinationCell = cellId(ids, item.mDestination);
            result.objects.push_back(std::move(object));
        }
        std::sort(result.objects.begin(), result.objects.end(),
            [](const WorldObject& a, const WorldObject& b) { return a.id < b.id; });
        result.droppedObjects = dropped;
        out = std::move(result);
        return true;
    }

    MorrowindCellSource::MorrowindCellSource(const ESMStore& store, NeutralWorld::IdTable& ids, CellProvider provider)
        : mStore(store)
        , mIds(ids)
        , mProvider(std::move(provider))
    {
    }

    std::size_t MorrowindCellSource::cellCount() const
    {
        return mStore.get<ESM::Cell>().getSize();
    }

    std::size_t MorrowindCellSource::listCells(std::size_t first, std::size_t maxCount, std::vector<WorldId>& out) const
    {
        const Store<ESM::Cell>& cells = mStore.get<ESM::Cell>();
        const std::size_t size = cells.getSize();
        std::size_t appended = 0;
        for (std::size_t i = first; i < size && appended < maxCount; ++i)
        {
            const ESM::Cell* cell = cells.at(i);
            const WorldId id = cellId(mIds, cell->mId);
            if (id == kNoWorldId)
                break;
            mCells.emplace(id, cell->mId);
            out.push_back(id);
            ++appended;
        }
        return appended;
    }

    WorldId MorrowindCellSource::findCellByEditorId(std::string_view editorId) const
    {
        const ESM::Cell* cell = mStore.get<ESM::Cell>().search(editorId);
        if (cell == nullptr || cell->isExterior())
            return kNoWorldId;
        const WorldId id = cellId(mIds, cell->mId);
        if (id != kNoWorldId)
            mCells.emplace(id, cell->mId);
        return id;
    }

    WorldId MorrowindCellSource::findCellByGrid(WorldId /*world*/, std::int32_t x, std::int32_t y) const
    {
        const ESM::Cell* cell = mStore.get<ESM::Cell>().search(x, y);
        if (cell == nullptr)
            return kNoWorldId;
        const WorldId id = cellId(mIds, cell->mId);
        if (id != kNoWorldId)
            mCells.emplace(id, cell->mId);
        return id;
    }

    double MorrowindCellSource::unitsPerMetre() const
    {
        return static_cast<double>(Constants::UnitsPerMeter);
    }

    bool MorrowindCellSource::buildCell(WorldId cell, WorldCell& out, std::string& error)
    {
        out = WorldCell{};
        const auto known = mCells.find(cell);
        if (known == mCells.end())
        {
            error = "cell " + std::to_string(cell) + " was never listed by this source";
            return false;
        }
        try
        {
            CellStore* store = mProvider ? mProvider(known->second) : nullptr;
            if (store == nullptr)
            {
                error = "no cell store for " + known->second.toDebugString();
                return false;
            }
            if (store->getState() != CellStore::State_Loaded)
                store->load();
            return projectCell(*store, mIds, out, error, &mStats);
        }
        catch (const std::exception& e)
        {
            out = WorldCell{};
            error = std::string("loading the cell failed: ") + e.what();
            return false;
        }
    }
}
