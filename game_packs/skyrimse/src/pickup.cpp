#include "game_skyrimse/pickup.hpp"

#include <algorithm>
#include <cmath>

// The pure pickup and pickpocket rules (task 0208): see the header.

namespace game_skyrimse::pickup
{
    namespace
    {
        // splitmix64: the same mixer the pack uses for its other seeded picks.
        std::uint64_t mix(std::uint64_t z)
        {
            z += 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        float finiteOr(float value, float fallback)
        {
            return std::isfinite(value) ? value : fallback;
        }
    }

    // ---- PickupBook -----------------------------------------------------------------------------------------------

    bool PickupBook::markTaken(FormId placement)
    {
        if (placement == kNullFormId || mTaken.count(placement) != 0 || mTaken.size() >= kMaxTaken)
            return false;
        mTaken.insert(placement);
        ++mRevision;
        return true;
    }

    void PickupBook::clear()
    {
        mTaken.clear();
        mAttempts = 0;
        ++mRevision;
    }

    void PickupBook::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mTaken.size()));
        for (const FormId placement : mTaken)
            writer.writeValue(static_cast<std::uint32_t>(placement));
        writer.writeValue(mAttempts);
    }

    bool PickupBook::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::uint32_t count = 0;
        if (!reader.readValue(count))
        {
            error = "the count is missing";
            return false;
        }
        if (count > kMaxTaken || static_cast<std::uint64_t>(count) * 4 > reader.remaining())
        {
            error = "the count does not fit";
            return false;
        }
        std::set<FormId> taken;
        std::uint32_t previous = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t placement = 0;
            if (!reader.readValue(placement))
            {
                error = "a placement is cut short";
                return false;
            }
            if (placement == 0 || placement <= previous)
            {
                error = "the placements are not ascending";
                return false;
            }
            previous = placement;
            taken.insert(static_cast<FormId>(placement));
        }
        std::uint32_t attempts = 0;
        if (!reader.readValue(attempts))
        {
            error = "the attempt count is missing";
            return false;
        }
        if (reader.remaining() != 0)
        {
            error = "bytes follow the pickups";
            return false;
        }
        mTaken = std::move(taken);
        mAttempts = attempts;
        ++mRevision;
        return true;
    }

    bool PickupChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kVersion)
            mRefusal = "the pickups chunk has version " + std::to_string(version) + ", expected 1";
        else
        {
            std::string error;
            if (mBook.read(reader, error))
                return true;
            mRefusal = "the pickups chunk is damaged: " + error;
        }
        if (mSink)
            mSink(mRefusal);
        return false;
    }

    // ---- rules ----------------------------------------------------------------------------------------------------

    std::int32_t placedCount(std::int32_t xcnt)
    {
        if (xcnt <= 0)
            return 1;
        return std::min(xcnt, kMaxPickupCount);
    }

    float pickpocketChance(
        const PickpocketSettings& settings, float skill, std::int32_t value, float weight, std::int32_t count)
    {
        const float s = std::max(0.f, finiteOr(skill, 0.f));
        const double v = static_cast<double>(std::max<std::int32_t>(0, value))
            * static_cast<double>(std::max<std::int32_t>(0, count));
        const float w = std::max(0.f, finiteOr(weight, 0.f)) * static_cast<float>(std::max<std::int32_t>(0, count));
        double chance = static_cast<double>(settings.mBase) + static_cast<double>(settings.mSkillGain) * s
            - static_cast<double>(settings.mValueTerm) * std::sqrt(v)
            - static_cast<double>(settings.mWeightTerm) * static_cast<double>(w);
        if (!std::isfinite(chance))
            chance = static_cast<double>(settings.mMin);
        return static_cast<float>(
            std::clamp(chance, static_cast<double>(settings.mMin), static_cast<double>(settings.mMax)));
    }

    bool pickpocketRoll(std::uint64_t owner, FormId item, std::uint32_t attempt, float chancePercent)
    {
        if (!(chancePercent > 0.f))
            return false;
        if (chancePercent >= 100.f)
            return true;
        const std::uint64_t h = mix(mix(owner) ^ (static_cast<std::uint64_t>(item) << 32) ^ attempt);
        // 53 bits to a double in [0, 100)
        const double r = static_cast<double>(h >> 11) * (100.0 / 9007199254740992.0);
        return r < static_cast<double>(chancePercent);
    }

    float pickpocketTraining(std::int32_t value, std::int32_t count)
    {
        const std::int64_t total = static_cast<std::int64_t>(std::max<std::int32_t>(0, value))
            * static_cast<std::int64_t>(std::max<std::int32_t>(0, count));
        return static_cast<float>(std::max<std::int64_t>(1, total));
    }
}
