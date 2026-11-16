#include <game_skyrimse/houses.hpp>

#include <game_skyrimse/actor_mechanics.hpp>

#include <algorithm>

namespace game_skyrimse::houses
{
    bool Ledger::setCellOwner(FormId cell, FormId owner)
    {
        if (cell == kNullFormId || cell == kUnresolvedFormId || owner == kNullFormId || owner == kUnresolvedFormId)
            return false;
        const auto it = mOwners.find(cell);
        if (it == mOwners.end() && mOwners.size() >= kMaxHouseEntries)
            return false;
        if (it != mOwners.end() && it->second == owner)
            return true;
        mOwners[cell] = owner;
        ++mRevision;
        return true;
    }

    std::optional<FormId> Ledger::cellOwner(FormId cell) const
    {
        const auto it = mOwners.find(cell);
        if (it == mOwners.end())
            return std::nullopt;
        return it->second;
    }

    FormId Ledger::effectiveOwner(FormId own, FormId cell) const
    {
        if (own == kPlayerBedOwnershipFaction)
            return kPlayerFactionId;
        if (own != kNullFormId && own != kUnresolvedFormId)
            return own;
        const std::optional<FormId> owner = cellOwner(cell);
        return owner ? *owner : kNullFormId;
    }

    void Ledger::applyToAccess(FormId cell, crime::CellAccess& access) const
    {
        if (const std::optional<FormId> owner = cellOwner(cell))
            access.mOwner = *owner;
    }

    bool Ledger::restore(const std::map<FormId, FormId>& data)
    {
        if (data.size() > kMaxHouseEntries)
            return false;
        mOwners = data;
        ++mRevision;
        return true;
    }

    void HousesChunk::write(openeden::sdk::ChunkWriter& writer)
    {
        const std::map<FormId, FormId> data = mGet ? mGet() : std::map<FormId, FormId>();
        const std::size_t n = std::min(data.size(), kMaxHouseEntries);
        writer.writeValue(static_cast<std::uint32_t>(n));
        std::size_t written = 0;
        for (const auto& [cell, owner] : data)
        {
            if (written++ >= n)
                break;
            writer.writeValue(static_cast<std::uint32_t>(cell));
            writer.writeValue(static_cast<std::uint32_t>(owner));
        }
    }

    bool HousesChunk::read(openeden::sdk::ChunkReader& reader, std::uint32_t version)
    {
        mRefusal.clear();
        const auto refuse = [&](const std::string& why) {
            mRefusal = "houses chunk: " + why;
            return false;
        };
        if (version != kVersion)
            return refuse("version " + std::to_string(version) + " is not understood");
        std::uint32_t n = 0;
        if (!reader.readValue(n) || n > kMaxHouseEntries || reader.remaining() < static_cast<std::size_t>(n) * 8u)
            return refuse("bad count");
        std::map<FormId, FormId> data;
        FormId previous = 0;
        for (std::uint32_t i = 0; i < n; ++i)
        {
            std::uint32_t cell = 0, owner = 0;
            if (!reader.readValue(cell) || !reader.readValue(owner) || cell == 0 || owner == 0
                || (i > 0 && cell <= previous))
                return refuse("cells are not strictly ascending and non-zero");
            previous = cell;
            data[cell] = owner;
        }
        if (reader.remaining() != 0)
            return refuse("trailing bytes");
        if (mSet && !mSet(data))
            return refuse("the house rules are not running");
        return true;
    }
}
