#include "game_skyrimse/mining.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

// The pure mining and woodcutting rules (task 0224): see the header.

namespace game_skyrimse::mining
{
    namespace
    {
        std::uint64_t mix(std::uint64_t z)
        {
            z += 0x9E3779B97F4A7C15ull;
            z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
            z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
            return z ^ (z >> 31);
        }

        bool sameName(std::string_view a, std::string_view b)
        {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
                if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
                    return false;
            return true;
        }

        const vmad::Property* findProperty(const vmad::Script& script, std::string_view name)
        {
            for (const vmad::Property& p : script.mProperties)
                if (!p.mArray && sameName(p.mName, name))
                    return &p;
            return nullptr;
        }

        FormId formOf(const vmad::Script& script, std::string_view name)
        {
            const vmad::Property* p = findProperty(script, name);
            if (p == nullptr || p->mValue.mKind != vmad::ValueKind::Object)
                return kNullFormId;
            return static_cast<FormId>(p->mValue.mForm);
        }

        int countOf(const vmad::Script& script, std::string_view name, int fallback)
        {
            const vmad::Property* p = findProperty(script, name);
            if (p == nullptr || p->mValue.mKind != vmad::ValueKind::Int)
                return fallback;
            return std::clamp(p->mValue.mInteger, 1, kMaxCount);
        }
    }

    ResourceInfo resourceFromScripts(const std::vector<vmad::Script>& scripts)
    {
        ResourceInfo info;
        for (const vmad::Script& script : scripts)
        {
            if (sameName(script.mName, "MineOreScript"))
            {
                info.mKind = ResourceKind::Ore;
                info.mItem = formOf(script, "Ore");
                info.mToolList = formOf(script, "mineOreToolsList");
                info.mGems = formOf(script, "lItemGems10");
                info.mFailureMessage = formOf(script, "FailureMessage");
                info.mDepletedMessage = formOf(script, "DepletedMessage");
                info.mTotal = countOf(script, "ResourceCountTotal", kDefaultTotal);
                info.mPerDrop = countOf(script, "ResourceCount", kDefaultPerDrop);
                info.mStrikes = countOf(script, "StrikesBeforeCollection", kDefaultStrikes);
                return info;
            }
            if (sameName(script.mName, "ResourceFurnitureScript"))
            {
                info.mKind = ResourceKind::Wood;
                info.mItem = formOf(script, "Resource");
                info.mToolList = formOf(script, "requiredItemList");
                info.mFailureMessage = formOf(script, "FailureMessage");
                info.mPerDrop = countOf(script, "ResourceCount", kDefaultPerDrop);
                info.mMaxPerUse = countOf(script, "MaxResourcePerActivation", kDefaultMaxPerUse);
                return info;
            }
        }
        return info;
    }

    std::string orePrompt(std::string_view activateText, std::string_view name)
    {
        std::string out(activateText.empty() ? kOreVerb : activateText);
        if (!name.empty())
        {
            out += ' ';
            out += name;
        }
        return out;
    }

    std::string depletedName(std::string_view name)
    {
        std::string out(name);
        out += kDepletedSuffix;
        return out;
    }

    std::uint64_t gemSeed(FormId placement, std::uint32_t drop)
    {
        return mix((static_cast<std::uint64_t>(placement) << 32) ^ static_cast<std::uint64_t>(drop) ^ 0x47454Dull);
    }

    // ---- VeinBook -------------------------------------------------------------------------------------------------

    bool VeinBook::depleted(FormId placement, double now) const
    {
        const auto it = mEntries.find(placement);
        if (it == mEntries.end() || it->second.mDue < 0.0)
            return false;
        return now < 0.0 || it->second.mDue > now;
    }

    VeinState VeinBook::state(FormId placement) const
    {
        const auto it = mEntries.find(placement);
        return it == mEntries.end() ? VeinState() : it->second;
    }

    StrikeOutcome VeinBook::strike(FormId placement, const ResourceInfo& info, double regrowAt)
    {
        StrikeOutcome outcome;
        if (placement == kNullFormId || !std::isfinite(regrowAt) || regrowAt < 0.0)
            return outcome;
        auto it = mEntries.find(placement);
        if (it != mEntries.end() && it->second.mDue >= 0.0)
            return outcome; // already depleted
        if (it == mEntries.end())
        {
            if (mEntries.size() >= kMaxVeins)
                return outcome;
            it = mEntries.emplace(placement, VeinState()).first;
        }
        VeinState& s = it->second;
        ++mRevision;
        if (++s.mStrikes < info.mStrikes)
            return outcome;
        s.mStrikes = 0;
        outcome.mDrop = true;
        if (++s.mDrops >= info.mTotal)
        {
            s.mDue = regrowAt;
            outcome.mDepleted = true;
        }
        return outcome;
    }

    void VeinBook::collectGrown(double now, std::vector<FormId>& out)
    {
        if (now < 0.0)
            return;
        for (auto it = mEntries.begin(); it != mEntries.end();)
        {
            if (it->second.mDue >= 0.0 && it->second.mDue <= now)
            {
                out.push_back(it->first);
                it = mEntries.erase(it);
                ++mRevision;
            }
            else
                ++it;
        }
    }

    void VeinBook::clear()
    {
        mEntries.clear();
        ++mRevision;
    }

    void VeinBook::write(openeden::sdk::ChunkWriter& writer) const
    {
        writer.writeValue(static_cast<std::uint32_t>(mEntries.size()));
        for (const auto& [placement, state] : mEntries)
        {
            writer.writeValue(static_cast<std::uint32_t>(placement));
            writer.writeValue(state.mStrikes);
            writer.writeValue(state.mDrops);
            writer.writeValue(state.mDue);
        }
    }

    bool VeinBook::read(openeden::sdk::ChunkReader& reader, std::string& error)
    {
        std::uint32_t count = 0;
        if (!reader.readValue(count))
        {
            error = "the count is missing";
            return false;
        }
        if (count > kMaxVeins || static_cast<std::uint64_t>(count) * 16 > reader.remaining())
        {
            error = "the count does not fit";
            return false;
        }
        std::map<FormId, VeinState> entries;
        std::uint32_t previous = 0;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            std::uint32_t placement = 0;
            VeinState state;
            if (!reader.readValue(placement) || !reader.readValue(state.mStrikes) || !reader.readValue(state.mDrops)
                || !reader.readValue(state.mDue))
            {
                error = "a vein is cut short";
                return false;
            }
            if (placement == 0 || placement <= previous)
            {
                error = "the placements are not ascending";
                return false;
            }
            if (!std::isfinite(state.mDue))
            {
                error = "a due time is not finite";
                return false;
            }
            previous = placement;
            entries.emplace(static_cast<FormId>(placement), state);
        }
        if (reader.remaining() != 0)
        {
            error = "bytes follow the vein list";
            return false;
        }
        mEntries = std::move(entries);
        ++mRevision;
        return true;
    }

    bool MiningChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        if (version != kVersion)
            mRefusal = "the mining chunk has version " + std::to_string(version) + ", expected 1";
        else
        {
            std::string error;
            if (mBook.read(reader, error))
            {
                if (mRestored)
                    mRestored(mBook.entries().size());
                return true;
            }
            mRefusal = "the mining chunk is damaged: " + error;
        }
        if (mSink)
            mSink(mRefusal);
        return false;
    }
}
