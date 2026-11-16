#include "game_skyrimse/harvest.hpp"

#include <algorithm>
#include <cmath>

// The pure harvesting rules (task 0221): see the header.

namespace game_skyrimse::harvest
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
    }

    Season seasonOfMonth(int month)
    {
        if (month < 0 || month > 11)
            return Season::Unknown;
        if (month >= 2 && month <= 4)
            return Season::Spring;
        if (month >= 5 && month <= 7)
            return Season::Summer;
        if (month >= 8 && month <= 10)
            return Season::Fall;
        return Season::Winter;
    }

    float chanceIn(const std::array<std::uint8_t, 4>& chances, Season season)
    {
        const std::uint8_t best = *std::max_element(chances.begin(), chances.end());
        if (best == 0)
            return kUnseasonalChance;
        switch (season)
        {
            case Season::Spring:
                return static_cast<float>(chances[0]);
            case Season::Summer:
                return static_cast<float>(chances[1]);
            case Season::Fall:
                return static_cast<float>(chances[2]);
            case Season::Winter:
                return static_cast<float>(chances[3]);
            case Season::Unknown:
                break;
        }
        return static_cast<float>(best);
    }

    bool harvestRoll(FormId placement, std::uint32_t attempt, float chancePercent)
    {
        if (!(chancePercent > 0.f))
            return false;
        if (chancePercent >= 100.f)
            return true;
        const std::uint64_t h = mix(mix(static_cast<std::uint64_t>(placement)) ^ (0x4841525645535421ull + attempt));
        const double r = static_cast<double>(h >> 11) * (100.0 / 9007199254740992.0);
        return r < static_cast<double>(chancePercent);
    }

    std::uint64_t pickSeed(FormId placement, std::uint32_t attempt)
    {
        return mix((static_cast<std::uint64_t>(placement) << 32) ^ static_cast<std::uint64_t>(attempt) ^ 0x50494Bull);
    }

    std::string promptFor(std::string_view activateText, std::string_view name)
    {
        std::string out(activateText.empty() ? kDefaultVerb : activateText);
        if (!name.empty())
        {
            out += ' ';
            out += name;
        }
        return out;
    }

    // ---- HarvestBook ----------------------------------------------------------------------------------------------

    bool HarvestBook::mark(FormId placement, double due)
    {
        if (placement == kNullFormId || !std::isfinite(due) || mEntries.count(placement) != 0
            || mEntries.size() >= kMaxHarvested)
            return false;
        mEntries.emplace(placement, due);
        ++mRevision;
        return true;
    }

    bool HarvestBook::hidden(FormId placement, double now) const
    {
        const auto it = mEntries.find(placement);
        if (it == mEntries.end())
            return false;
        return now < 0.0 || it->second > now;
    }

    void HarvestBook::collectGrown(double now, std::vector<FormId>& out)
    {
        if (now < 0.0)
            return;
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            if (it->second <= now)
            {
                out.push_back(it->first);
                it = mEntries.erase(it);
                ++mRevision;
            }
            else
                ++it;
        }
    }

    void HarvestBook::clear()
    {
        mEntries.clear();
        mAttempts = 0;
        ++mRevision;
    }

    void HarvestBook::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mEntries.size()));
        for (const auto& [placement, due] : mEntries)
        {
            writer.writeValue(static_cast<std::uint32_t>(placement));
            writer.writeValue(due);
        }
        writer.writeValue(mAttempts);
    }

    bool HarvestBook::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::uint32_t count = 0;
        if (!reader.readValue(count))
        {
            error = "the count is missing";
            return false;
        }
        if (count > kMaxHarvested || static_cast<std::uint64_t>(count) * 12 > reader.remaining())
        {
            error = "the count does not fit";
            return false;
        }
        std::map<FormId, double> entries;
        std::uint32_t previous = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t placement = 0;
            double due = 0.0;
            if (!reader.readValue(placement) || !reader.readValue(due))
            {
                error = "a plant is cut short";
                return false;
            }
            if (placement == 0 || placement <= previous)
            {
                error = "the placements are not ascending";
                return false;
            }
            if (!std::isfinite(due))
            {
                error = "a due time is not finite";
                return false;
            }
            previous = placement;
            entries.emplace(static_cast<FormId>(placement), due);
        }
        std::uint32_t attempts = 0;
        if (!reader.readValue(attempts))
        {
            error = "the attempt count is missing";
            return false;
        }
        if (reader.remaining() != 0)
        {
            error = "bytes follow the harvest list";
            return false;
        }
        mEntries = std::move(entries);
        mAttempts = attempts;
        ++mRevision;
        return true;
    }

    bool HarvestChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kVersion)
            mRefusal = "the harvest chunk has version " + std::to_string(version) + ", expected 1";
        else
        {
            std::string error;
            if (mBook.read(reader, error))
            {
                if (mRestored)
                    mRestored(mBook.entries().size());
                return true;
            }
            mRefusal = "the harvest chunk is damaged: " + error;
        }
        if (mSink)
            mSink(mRefusal);
        return false;
    }
}
