#include "idtable.hpp"

namespace NeutralWorld
{
    namespace
    {
        std::string encodeNumber(std::uint64_t number)
        {
            std::string bytes(8, '\0');
            for (int i = 7; i >= 0; --i)
            {
                bytes[static_cast<std::size_t>(i)] = static_cast<char>(number & 0xffu);
                number >>= 8;
            }
            return bytes;
        }
    }

    openeden::sdk::game::WorldId IdTable::idFor(std::uint8_t space, std::string_view bytes)
    {
        auto key = std::make_pair(space, std::string(bytes));
        const auto found = mIds.find(key);
        if (found != mIds.end())
            return found->second;
        if (mKeys.size() >= mMaxIds || mKeys.size() >= 0xffffffffu)
        {
            ++mRefused;
            return openeden::sdk::game::kNoWorldId;
        }
        const auto id = static_cast<openeden::sdk::game::WorldId>(mKeys.size() + 1);
        mKeys.push_back(Key{ space, key.second });
        mIds.emplace(std::move(key), id);
        return id;
    }

    openeden::sdk::game::WorldId IdTable::idForNumber(std::uint8_t space, std::uint64_t number)
    {
        return idFor(space, encodeNumber(number));
    }

    openeden::sdk::game::WorldId IdTable::find(std::uint8_t space, std::string_view bytes) const
    {
        const auto found = mIds.find(std::make_pair(space, std::string(bytes)));
        return found == mIds.end() ? openeden::sdk::game::kNoWorldId : found->second;
    }

    openeden::sdk::game::WorldId IdTable::findNumber(std::uint8_t space, std::uint64_t number) const
    {
        return find(space, encodeNumber(number));
    }

    const IdTable::Key* IdTable::keyOf(openeden::sdk::game::WorldId id) const
    {
        if (id == openeden::sdk::game::kNoWorldId || id > mKeys.size())
            return nullptr;
        return &mKeys[id - 1];
    }

    bool IdTable::numberOf(openeden::sdk::game::WorldId id, std::uint8_t space, std::uint64_t& number) const
    {
        const Key* key = keyOf(id);
        if (key == nullptr || key->space != space || key->bytes.size() != 8)
            return false;
        std::uint64_t value = 0;
        for (const char c : key->bytes)
            value = (value << 8) | static_cast<std::uint8_t>(c);
        number = value;
        return true;
    }
}
