#include "game_services.hpp"

#include <algorithm>
#include <cmath>

namespace openeden::extension
{
    bool RecordServiceImpl::registerRecordType(const openeden::sdk::RecordTypeInfo& info)
    {
        if (info.typeId == 0 || info.sizeBytes == 0 || info.name.empty())
            return false;

        const auto [it, inserted] = mTypes.try_emplace(info.typeId);
        if (!inserted)
            return false;

        Entry& entry = it->second;
        entry.name = std::string(info.name);
        entry.info = info;
        entry.info.name = entry.name; // the node does not move, so the view stays valid
        return true;
    }

    const openeden::sdk::RecordTypeInfo* RecordServiceImpl::findRecordType(std::uint32_t typeId) const
    {
        const auto it = mTypes.find(typeId);
        return it == mTypes.end() ? nullptr : &it->second.info;
    }

    void RecordServiceImpl::clear() noexcept
    {
        mTypes.clear();
    }

    void ContentServiceImpl::registerLoader(openeden::sdk::ContentLoader& loader)
    {
        mLoaders.push_back(&loader);
    }

    openeden::sdk::ContentLoader* ContentServiceImpl::findLoader(const openeden::sdk::ContentHeader& header) const
    {
        for (openeden::sdk::ContentLoader* loader : mLoaders)
        {
            if (loader->probe(header))
                return loader;
        }
        return nullptr;
    }

    bool ScriptHostServiceImpl::declareLanguage(std::string_view name)
    {
        if (name.empty())
            return false;
        return mLanguages.emplace(name).second;
    }

    bool ScriptHostServiceImpl::isLanguageDeclared(std::string_view name) const
    {
        return mLanguages.find(name) != mLanguages.end();
    }

    bool UiServiceImpl::declareWindow(std::string_view id, std::string_view title)
    {
        if (id.empty())
            return false;
        return mWindows.try_emplace(std::string(id), std::string(title)).second;
    }

    bool UiServiceImpl::isWindowDeclared(std::string_view id) const
    {
        return mWindows.find(id) != mWindows.end();
    }

    bool WorldSourceServiceImpl::registerSource(openeden::sdk::game::WorldCellSource& source, std::string& error)
    {
        const std::string gameId(source.gameId());
        for (const Entry& entry : mSources)
        {
            if (entry.mSource == &source)
            {
                error = "the world source of game '" + gameId + "' is already registered";
                return false;
            }
            if (entry.mGameId == gameId)
            {
                error = "another world source is already registered for game '" + gameId + "'";
                return false;
            }
        }
        if (mListener != nullptr && !mListener->sourceRegistered(source, error))
        {
            if (error.empty())
                error = "the host refused the world source of game '" + gameId + "'";
            return false;
        }
        mSources.push_back({ &source, gameId });
        return true;
    }

    void WorldSourceServiceImpl::withdrawSource(openeden::sdk::game::WorldCellSource& source)
    {
        for (auto it = mSources.begin(); it != mSources.end(); ++it)
        {
            if (it->mSource != &source)
                continue;
            const std::string gameId = std::move(it->mGameId);
            mSources.erase(it);
            std::erase_if(mObjectChanges, [&](const ObjectEnableChange& c) { return c.gameId == gameId; });
            std::erase_if(mObjectOps, [&](const ObjectOp& op) { return op.gameId == gameId; });
            if (mListener != nullptr)
                mListener->sourceWithdrawn(gameId);
            return;
        }
    }

    std::size_t WorldSourceServiceImpl::sessionContent(std::vector<openeden::sdk::game::SessionContentFile>& out) const
    {
        const std::vector<SessionContentEntry> kept = filterSessionContent(mSessionContent, mContentFilter);
        for (const SessionContentEntry& entry : kept)
            out.push_back({ entry.name, entry.path });
        return kept.size();
    }

    bool WorldSourceServiceImpl::setObjectEnabled(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId cellHint, openeden::sdk::game::WorldId placement, bool enabled)
    {
        const Entry* entry = nullptr;
        for (const Entry& candidate : mSources)
            if (candidate.mSource == &source)
                entry = &candidate;
        if (entry == nullptr)
            return false;
        for (ObjectEnableChange& pending : mObjectChanges)
        {
            if (pending.placement == placement && pending.gameId == entry->mGameId)
            {
                pending.cell = cellHint;
                pending.enabled = enabled;
                return true;
            }
        }
        if (mObjectChanges.size() >= kMaxPendingObjectChanges)
        {
            ++mDroppedObjectChanges;
            return false;
        }
        mObjectChanges.push_back({ entry->mGameId, cellHint, placement, enabled });
        return true;
    }

    std::size_t WorldSourceServiceImpl::takeObjectEnableChanges(std::vector<ObjectEnableChange>& out)
    {
        const std::size_t count = mObjectChanges.size();
        for (ObjectEnableChange& change : mObjectChanges)
            out.push_back(std::move(change));
        mObjectChanges.clear();
        return count;
    }

    namespace
    {
        bool finiteVec(const openeden::sdk::game::Vec3d& v)
        {
            return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
        }

        bool usableRotation(const openeden::sdk::game::Quatd& q)
        {
            const double norm2 = q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w;
            return std::isfinite(norm2) && norm2 > 1e-12;
        }

        constexpr float kMaxFadeSeconds = 60.f;
    }

    const WorldSourceServiceImpl::Entry* WorldSourceServiceImpl::entryOf(
        const openeden::sdk::game::WorldCellSource& source) const
    {
        for (const Entry& candidate : mSources)
            if (candidate.mSource == &source)
                return &candidate;
        return nullptr;
    }

    bool WorldSourceServiceImpl::moveObject(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId cellHint, openeden::sdk::game::WorldId placement,
        const openeden::sdk::game::ObjectMove& move)
    {
        const Entry* entry = entryOf(source);
        if (entry == nullptr || placement == openeden::sdk::game::kNoWorldId)
            return false;
        if (!move.toOriginal)
        {
            if (!move.hasPosition && !move.hasRotation)
                return false;
            if (move.hasPosition && !finiteVec(move.position))
                return false;
            if (move.hasRotation && !usableRotation(move.rotation))
                return false;
        }
        if (mObjectOps.size() >= kMaxPendingObjectChanges)
        {
            ++mDroppedObjectChanges;
            return false;
        }
        ObjectOp op;
        op.kind = ObjectOp::Kind::Move;
        op.gameId = entry->mGameId;
        op.cell = cellHint;
        op.placement = placement;
        op.move = move;
        mObjectOps.push_back(std::move(op));
        return true;
    }

    bool WorldSourceServiceImpl::setObjectEnabledFaded(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId cellHint, openeden::sdk::game::WorldId placement, bool enabled, float fadeSeconds)
    {
        const Entry* entry = entryOf(source);
        if (entry == nullptr || placement == openeden::sdk::game::kNoWorldId)
            return false;
        if (mObjectOps.size() >= kMaxPendingObjectChanges)
        {
            ++mDroppedObjectChanges;
            return false;
        }
        ObjectOp op;
        op.kind = ObjectOp::Kind::Enable;
        op.gameId = entry->mGameId;
        op.cell = cellHint;
        op.placement = placement;
        op.enabled = enabled;
        op.fadeSeconds = std::isfinite(fadeSeconds) ? std::clamp(fadeSeconds, 0.f, kMaxFadeSeconds) : 0.f;
        mObjectOps.push_back(std::move(op));
        return true;
    }

    openeden::sdk::game::WorldId WorldSourceServiceImpl::placeObject(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId cellHint, const openeden::sdk::game::WorldObject& prototype)
    {
        using namespace openeden::sdk::game;
        const Entry* entry = entryOf(source);
        if (entry == nullptr || cellHint == kNoWorldId || prototype.base == kNoWorldId)
            return kNoWorldId;
        if (prototype.kind == ObjectKind::Actor || prototype.kind == ObjectKind::LeveledList
            || prototype.kind == ObjectKind::Unknown)
            return kNoWorldId;
        if (!finiteVec(prototype.transform.position) || !usableRotation(prototype.transform.rotation)
            || !(prototype.transform.scale > 0.0) || !std::isfinite(prototype.transform.scale))
            return kNoWorldId;
        if (mObjectOps.size() >= kMaxPendingObjectChanges)
        {
            ++mDroppedObjectChanges;
            return kNoWorldId;
        }
        if (mNextCreatedIndex > kMaxCreatedObjects)
            return kNoWorldId;
        const WorldId id = kCreatedObjectBase + mNextCreatedIndex;
        ++mNextCreatedIndex;
        ObjectOp op;
        op.kind = ObjectOp::Kind::Place;
        op.gameId = entry->mGameId;
        op.cell = cellHint;
        op.placement = id;
        op.object = prototype;
        op.object.id = id;
        mObjectOps.push_back(std::move(op));
        return id;
    }

    openeden::sdk::game::WorldId WorldSourceServiceImpl::placeActor(openeden::sdk::game::WorldCellSource& source,
        openeden::sdk::game::WorldId cellHint, const openeden::sdk::game::WorldObject& prototype)
    {
        using namespace openeden::sdk::game;
        const Entry* entry = entryOf(source);
        if (entry == nullptr || prototype.base == kNoWorldId || prototype.kind != ObjectKind::Actor)
            return kNoWorldId;
        if (!finiteVec(prototype.transform.position) || !usableRotation(prototype.transform.rotation)
            || !(prototype.transform.scale > 0.0) || !std::isfinite(prototype.transform.scale))
            return kNoWorldId;
        if (mObjectOps.size() >= kMaxPendingObjectChanges)
        {
            ++mDroppedObjectChanges;
            return kNoWorldId;
        }
        if (mNextCreatedIndex > kMaxCreatedObjects)
            return kNoWorldId;
        const WorldId id = kCreatedObjectBase + mNextCreatedIndex;
        ++mNextCreatedIndex;
        ObjectOp op;
        op.kind = ObjectOp::Kind::Place;
        op.gameId = entry->mGameId;
        op.cell = cellHint;
        op.placement = id;
        op.object = prototype;
        op.object.id = id;
        mObjectOps.push_back(std::move(op));
        return id;
    }

    std::size_t WorldSourceServiceImpl::takeObjectOps(std::vector<ObjectOp>& out)
    {
        const std::size_t count = mObjectOps.size();
        for (ObjectOp& op : mObjectOps)
            out.push_back(std::move(op));
        mObjectOps.clear();
        return count;
    }

    void WorldSourceServiceImpl::setNextCreatedIndex(std::uint32_t next) noexcept
    {
        // 0 is not an index; past the range nothing more can be minted (placeObject refuses), so it is kept just past
        // it
        mNextCreatedIndex = std::clamp<std::uint32_t>(next, 1u, openeden::sdk::game::kMaxCreatedObjects + 1u);
    }

    void WorldSourceServiceImpl::releaseAll()
    {
        mObjectChanges.clear();
        mObjectOps.clear();
        while (!mSources.empty())
        {
            const std::string gameId = std::move(mSources.back().mGameId);
            mSources.pop_back();
            if (mListener != nullptr)
                mListener->sourceWithdrawn(gameId);
        }
    }
} // namespace openeden::extension
