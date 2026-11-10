#ifndef OPENEDEN_COMPONENTS_WORLDMODEL_IDTABLE_H
#define OPENEDEN_COMPONENTS_WORLDMODEL_IDTABLE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <openeden/game/world_model.hpp>

// The session table that gives the neutral model its identity (decision 0016, adopted answer 4 of the design document's
// section 7: a session table, not arithmetic on a game's ids). A game's own identity (a Morrowind reference number, an
// editor id, a form id) is mapped to a WorldId by the first request, and the same key gives the same WorldId for the
// rest of the session. Numbers start at 1 (0 is kNoWorldId) and are handed out in the order of first request, so two
// runs that ask in the same order get the same numbers. The table is what stage N6 will write into a save.
//
// Who owns it: whoever creates it (the source that mints ids). Main thread only. Capped: past `maxIds` a new key gets
// kNoWorldId and is counted in `refused()`. Nothing here throws except std::bad_alloc.

namespace NeutralWorld
{
    inline constexpr std::size_t kDefaultMaxIds = 16u * 1024u * 1024u;

    class IdTable
    {
    public:
        /// A key is a namespace tag chosen by the source (cells, bases, placements ... each its own tag) and a byte
        /// string. Two keys with different tags never collide.
        struct Key
        {
            std::uint8_t space = 0;
            std::string bytes;

            friend bool operator==(const Key&, const Key&) = default;
        };

        explicit IdTable(std::size_t maxIds = kDefaultMaxIds)
            : mMaxIds(maxIds)
        {
        }

        /// The id of the key, minting the next number the first time. kNoWorldId when the table is full.
        [[nodiscard]] openeden::sdk::game::WorldId idFor(std::uint8_t space, std::string_view bytes);

        /// The same for a number key (encoded as 8 bytes, most significant first).
        [[nodiscard]] openeden::sdk::game::WorldId idForNumber(std::uint8_t space, std::uint64_t number);

        /// The id of a key without minting; kNoWorldId when it was never asked for.
        [[nodiscard]] openeden::sdk::game::WorldId find(std::uint8_t space, std::string_view bytes) const;
        [[nodiscard]] openeden::sdk::game::WorldId findNumber(std::uint8_t space, std::uint64_t number) const;

        /// The key behind an id, nullptr when the id was never minted. Valid until the next idFor.
        [[nodiscard]] const Key* keyOf(openeden::sdk::game::WorldId id) const;

        /// The number a number key was made from. False when the id has no such key.
        [[nodiscard]] bool numberOf(openeden::sdk::game::WorldId id, std::uint8_t space, std::uint64_t& number) const;

        [[nodiscard]] std::size_t size() const { return mKeys.size(); }
        [[nodiscard]] std::size_t refused() const { return mRefused; }

    private:
        std::size_t mMaxIds;
        std::size_t mRefused = 0;
        std::map<std::pair<std::uint8_t, std::string>, openeden::sdk::game::WorldId> mIds;
        std::vector<Key> mKeys; // index = id - 1
    };
}

#endif
