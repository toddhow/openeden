#include <game_skyrimse/deaths.hpp>

#include <game_skyrimse/combat.hpp>

#include <openeden/game/world_source_service.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <string>

namespace game_skyrimse
{
    using namespace openeden::sdk::game;

    namespace
    {
        constexpr std::uint8_t kFlagPosition = 1;
        constexpr std::uint8_t kFlagRaised = 2;

        bool createdId(FormId reference)
        {
            return reference >= static_cast<FormId>(kCreatedObjectBase);
        }

        bool finiteEntry(const DeathEntry& e)
        {
            return std::isfinite(e.mHours) && std::isfinite(e.mX) && std::isfinite(e.mY) && std::isfinite(e.mZ)
                && std::isfinite(e.mHeading);
        }
    }

    bool deathRespawns(double nowHours, double deathHours, double respawnHours, bool unique, bool essential)
    {
        if (unique || essential || !std::isfinite(nowHours) || !std::isfinite(deathHours)
            || !std::isfinite(respawnHours))
            return false;
        const double since = nowHours - deathHours;
        return since >= 0.0 && since >= respawnHours;
    }

    bool DeathLedger::add(FormId reference, double hours)
    {
        if (reference == 0 || createdId(reference) || !std::isfinite(hours) || mEntries.size() >= kMaxDeathEntries
            || mEntries.count(reference) != 0)
            return false;
        DeathEntry entry;
        entry.mHours = hours;
        mEntries.emplace(reference, entry);
        return true;
    }

    bool DeathLedger::setPosition(FormId reference, double x, double y, double z, float heading)
    {
        DeathEntry* entry = find(reference);
        if (entry == nullptr || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) || !std::isfinite(heading))
            return false;
        entry->mX = x;
        entry->mY = y;
        entry->mZ = z;
        entry->mHeading = heading;
        entry->mHasPosition = true;
        return true;
    }

    bool DeathLedger::markRaised(FormId reference)
    {
        DeathEntry* entry = find(reference);
        if (entry == nullptr)
            return false;
        entry->mRaised = true;
        return true;
    }

    bool DeathLedger::erase(FormId reference)
    {
        return mEntries.erase(reference) != 0;
    }

    void DeathLedger::clear()
    {
        mEntries.clear();
    }

    const DeathEntry* DeathLedger::find(FormId reference) const
    {
        const auto it = mEntries.find(reference);
        return it == mEntries.end() ? nullptr : &it->second;
    }

    DeathEntry* DeathLedger::find(FormId reference)
    {
        const auto it = mEntries.find(reference);
        return it == mEntries.end() ? nullptr : &it->second;
    }

    std::map<FormId, DeathEntry> DeathLedger::data() const
    {
        std::map<FormId, DeathEntry> out;
        for (const auto& [reference, entry] : mEntries)
        {
            DeathEntry saved;
            saved.mHours = entry.mHours;
            saved.mX = entry.mX;
            saved.mY = entry.mY;
            saved.mZ = entry.mZ;
            saved.mHeading = entry.mHeading;
            saved.mHasPosition = entry.mHasPosition;
            saved.mRaised = entry.mRaised;
            out.emplace(reference, saved);
        }
        return out;
    }

    bool DeathLedger::restore(const std::map<FormId, DeathEntry>& saved)
    {
        if (saved.size() > kMaxDeathEntries)
            return false;
        for (const auto& [reference, entry] : saved)
            if (reference == 0 || createdId(reference) || !finiteEntry(entry))
                return false;
        mEntries.clear();
        for (const auto& [reference, entry] : saved)
        {
            DeathEntry fresh;
            fresh.mHours = entry.mHours;
            fresh.mX = entry.mX;
            fresh.mY = entry.mY;
            fresh.mZ = entry.mZ;
            fresh.mHeading = entry.mHeading;
            fresh.mHasPosition = entry.mHasPosition;
            fresh.mRaised = entry.mRaised;
            mEntries.emplace(reference, fresh);
        }
        return true;
    }

    void DeathLedger::actorGone(FormId reference)
    {
        if (DeathEntry* entry = find(reference))
        {
            entry->mPlaced = false;
            entry->mRestored = false;
        }
    }

    void DeathsChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const std::map<FormId, DeathEntry> data = mGet ? mGet() : std::map<FormId, DeathEntry>();
        const std::size_t count = std::min(data.size(), kMaxDeathEntries);
        writer.writeValue(static_cast<std::uint32_t>(count));
        std::size_t written = 0;
        for (const auto& [reference, entry] : data)
        {
            if (written++ >= count)
                break;
            writer.writeValue(static_cast<std::uint32_t>(reference));
            writer.writeValue(std::bit_cast<std::uint64_t>(entry.mHours));
            std::uint8_t flags = 0;
            if (entry.mHasPosition)
                flags |= kFlagPosition;
            if (entry.mRaised)
                flags |= kFlagRaised;
            writer.writeValue(flags);
            if (entry.mHasPosition)
            {
                writer.writeValue(std::bit_cast<std::uint64_t>(entry.mX));
                writer.writeValue(std::bit_cast<std::uint64_t>(entry.mY));
                writer.writeValue(std::bit_cast<std::uint64_t>(entry.mZ));
                writer.writeValue(std::bit_cast<std::uint32_t>(entry.mHeading));
            }
        }
    }

    bool DeathsChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        const auto refuse = [&](const char* why) {
            mRefusal = std::string("deaths chunk: ") + why;
            return false;
        };
        if (version != kVersion)
        {
            mRefusal = "deaths chunk: version " + std::to_string(version) + " is not understood";
            return false;
        }
        std::uint32_t count = 0;
        if (!reader.readValue(count) || count > kMaxDeathEntries
            || reader.remaining() < static_cast<std::size_t>(count) * 13u)
            return refuse("bad entry count");
        std::map<FormId, DeathEntry> data;
        FormId previous = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t reference = 0;
            std::uint64_t hoursBits = 0;
            std::uint8_t flags = 0;
            if (!reader.readValue(reference) || !reader.readValue(hoursBits) || !reader.readValue(flags))
                return refuse("truncated entry");
            if (reference == 0 || createdId(reference) || (i > 0 && reference <= previous))
                return refuse("placements are not strictly ascending, non-zero and below the created range");
            if ((flags & ~(kFlagPosition | kFlagRaised)) != 0)
                return refuse("unknown flags");
            previous = reference;
            DeathEntry entry;
            entry.mHours = std::bit_cast<double>(hoursBits);
            entry.mRaised = (flags & kFlagRaised) != 0;
            if ((flags & kFlagPosition) != 0)
            {
                std::uint64_t x = 0, y = 0, z = 0;
                std::uint32_t heading = 0;
                if (!reader.readValue(x) || !reader.readValue(y) || !reader.readValue(z) || !reader.readValue(heading))
                    return refuse("truncated position");
                entry.mX = std::bit_cast<double>(x);
                entry.mY = std::bit_cast<double>(y);
                entry.mZ = std::bit_cast<double>(z);
                entry.mHeading = std::bit_cast<float>(heading);
                entry.mHasPosition = true;
            }
            if (!finiteEntry(entry))
                return refuse("a value is not finite");
            data.emplace(reference, entry);
        }
        if (reader.remaining() != 0)
            return refuse("trailing bytes");
        if (mSet && !mSet(data))
            return refuse("the pack's death rules are not running");
        return true;
    }

    void DeathKeeper::noteDeaths(float seconds, CombatRequestWriter& writer, SkyrimCombat& combat)
    {
        mScratch.clear();
        combat.forEachFighter([this](const CombatSubject& subject, const SkyrimCombat::FighterView& view) {
            if (view.mDead && subject.valid() && !subject.player)
                mScratch.push_back(subject);
        });
        const float step = std::isfinite(seconds) && seconds > 0.f ? seconds : 0.f;
        for (const CombatSubject& subject : mScratch)
        {
            const FormId reference = subject.actor.reference;
            if (reference >= static_cast<FormId>(kCreatedObjectBase))
                continue; // a summon or a raised actor is not saved
            DeathEntry* entry = mLedger.find(reference);
            bool fresh = false;
            if (entry == nullptr)
            {
                if (!mLedger.add(reference, mClockValid ? mHours : 0.0))
                    continue;
                entry = mLedger.find(reference);
                entry->mPlaced = true; // it died in this session: nothing to put back
                entry->mRestored = true;
                entry->mRefreshLeft = kDeathRefreshSpan;
                entry->mSinceRefresh = kDeathRefreshEvery; // read at once
                fresh = true;
                if (mTrace)
                    mTrace(
                        "DEATHS noted actor " + std::to_string(reference) + " hours " + std::to_string(entry->mHours));
                if (mHooks.hold)
                    mHooks.hold(reference, true); // a corpse does not walk on by its package
            }
            else if (!entry->mRestored)
                continue; // a saved death still waiting to be put back
            entry->mSinceRefresh += step;
            entry->mRefreshLeft = std::max(0.f, entry->mRefreshLeft - step);
            const float every = entry->mRefreshLeft > 0.f ? kDeathRefreshEvery : kDeathRefreshLate;
            if (entry->mSinceRefresh < every && !fresh)
                continue;
            entry->mSinceRefresh = 0.f;
            SubjectState where;
            if (writer.subjectState(subject, where))
                (void)mLedger.setPosition(reference, where.x, where.y, where.z, where.heading);
        }
    }

    void DeathKeeper::placeCorpses(ActorRequestWriter& writer, SkyrimCombat& combat)
    {
        if (mLedger.size() == 0 || !mHooks.handleOf)
            return;
        mScratchIds.clear();
        for (const auto& [reference, entry] : mLedger.entries())
            if (!entry.mPlaced)
                mScratchIds.push_back(reference);
        for (const FormId reference : mScratchIds)
        {
            DeathEntry* entry = mLedger.find(reference);
            if (entry == nullptr)
                continue;
            const ActorHandle handle = mHooks.handleOf(reference);
            if (!handle.valid())
                continue; // its cell is not loaded
            SkyrimCombat::FighterView view;
            if (!combat.readView(handle, view))
                continue; // the records are not read yet: ask again next update
            const double hours = mHooks.respawnHours ? mHooks.respawnHours() : kDefaultRespawnHours;
            if (mClockValid && deathRespawns(mHours, entry->mHours, hours, view.mUnique, view.mEssential))
            {
                (void)mLedger.erase(reference); // its cell reset: the actor the pack built stands again
                ++mRespawns;
                if (mTrace)
                    mTrace("DEATHS respawn actor " + std::to_string(reference));
                continue;
            }
            if (entry->mHasPosition)
                (void)writer.placeCorpse(handle, entry->mX, entry->mY, entry->mZ, entry->mHeading);
            if (mHooks.hold)
                mHooks.hold(reference, true);
            entry->mPlaced = true;
            if (mTrace)
                mTrace("DEATHS placed actor " + std::to_string(reference) + " at " + std::to_string(entry->mX) + " "
                    + std::to_string(entry->mY) + " " + std::to_string(entry->mZ));
        }
    }

    void DeathKeeper::restoreDeaths(CombatRequestWriter& writer, SkyrimCombat& combat)
    {
        if (mLedger.size() == 0 || !mHooks.handleOf)
            return;
        mScratchIds.clear();
        for (const auto& [reference, entry] : mLedger.entries())
            if (entry.mPlaced && !entry.mRestored)
                mScratchIds.push_back(reference);
        for (const FormId reference : mScratchIds)
        {
            DeathEntry* entry = mLedger.find(reference);
            const ActorHandle handle = mHooks.handleOf(reference);
            if (entry == nullptr || !handle.valid())
                continue;
            if (combat.restoreDead(writer, handle))
            {
                entry->mRestored = true;
                ++mRestoredCount;
                if (mTrace)
                    mTrace("DEATHS restored dead actor " + std::to_string(reference));
            }
        }
    }

    void DeathKeeper::corpseRaised(WorldId reference)
    {
        (void)mLedger.markRaised(static_cast<FormId>(reference));
    }

    void DeathKeeper::ledgerRestored()
    {
        if (!mHooks.enable)
            return;
        for (auto& [reference, entry] : mLedger.entries())
            if (entry.mRaised)
            {
                mHooks.enable(reference); // the corpse a Reanimate spell hid comes back as a corpse
                if (mTrace)
                    mTrace("DEATHS enabled the hidden corpse " + std::to_string(reference));
                entry.mRaised = false;
            }
    }
}
