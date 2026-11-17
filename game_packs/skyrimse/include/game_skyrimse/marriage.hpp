#ifndef OPENEDEN_GAME_SKYRIMSE_MARRIAGE_H
#define OPENEDEN_GAME_SKYRIMSE_MARRIAGE_H

// Task 0270: marriage, first slice. The game's own quests, dialogue and Papyrus do the courting (Maramal's lines,
// RelationshipMarriage stages 5 to 20, the Amulet of Mara bought for 200 gold, "Interested in me, are you?"); this file
// holds the little the pack adds: the names of the settings and forms the survey found, and the parser of the test aid.
//
//   marriage                   [extension.skyrimse] marriage (needs papyrus, inventory): the player's worn and carried items
//                              answer the conversation's conditions (GetEquipped, GetItemCount on the player)
//   marriage test friends      a list of NPC_ base ids (hexadecimal) the player is a Friend (rank 2) of: the real rank comes
//                              from the favour and follower quests this build does not run

#include <game_skyrimse/records.hpp>

#include <string_view>
#include <vector>

namespace game_skyrimse::marriage
{
    inline constexpr std::string_view kMarriageKey = "marriage";
    inline constexpr std::string_view kMarriageTestFriendsKey = "marriage test friends";
    inline constexpr std::size_t kMaxFriends = 64;

    /// RelationshipMarriage (QUST 0x74793, Skyrim.esm, start-game-enabled): the courting quest.
    inline constexpr FormId kRelationshipMarriageQuest = 0x00074793;
    /// ReligiousMaraLove (ARMO 0xC891B): the Amulet of Mara.
    inline constexpr FormId kAmuletOfMara = 0x000C891B;
    /// PotentialMarriageFaction (FACT 0x19809): the 62 NPCs of the five masters who can be asked.
    inline constexpr FormId kPotentialMarriageFaction = 0x00019809;

    /// A list of hexadecimal base ids separated by commas or spaces (an optional 0x prefix). Empty text is an empty list;
    /// anything else that is not an id, a zero id or more than kMaxFriends entries is a failure and `out` is left empty.
    [[nodiscard]] bool parseBaseIdList(std::string_view text, std::vector<FormId>& out);
}

#endif
