#ifndef OPENMW_MWWORLD_NEUTRALDIALOGUE_H
#define OPENMW_MWWORLD_NEUTRALDIALOGUE_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <openeden/game/dialogue_source.hpp>

// Talking to a neutral actor (task 0141, milestone M-S4 slice 1; docs/tasks/0141-neutral-dialogue-slice1.md).
//
// The pack decides what is said (openeden::sdk::game::DialogueSource); this file only presents it. Slice 1 shows each
// turn in the engine's interactive message box: the actor's line as the message, the topics as buttons, a "More
// topics" button when the list does not fit one box, and Goodbye. The pure part (NeutralDialoguePager) decides what
// the box holds and what a pressed button means; NeutralDialogueSession drives the box from the main thread, one
// poll a frame (Game::run, before the window manager's update). Main thread only. Not saved.

namespace MWWorld
{
    /// What one message box holds.
    struct NeutralDialogueMenu
    {
        std::string message;
        std::vector<std::string> buttons;
    };

    /// What a pressed button means.
    struct NeutralDialogueChoice
    {
        enum class Kind
        {
            None, ///< an index that is not a button
            Topic, ///< `topicId` was chosen
            MoreTopics, ///< the page changed: show the menu again
            Goodbye,
        };
        Kind kind = Kind::None;
        std::uint32_t topicId = 0;
    };

    /// Reads "polls:button,polls:button" (the developer aid OPENEDEN_NEUTRAL_DIALOGUE_PRESS): press `button` at the
    /// poll number `polls` (counted from the start of the conversation, the first poll is 1). Malformed entries are
    /// skipped; the result is sorted by poll.
    [[nodiscard]] std::vector<std::pair<int, int>> parseNeutralDialoguePressScript(const std::string& text);

    class NeutralDialoguePager
    {
    public:
        static constexpr std::size_t kTopicsPerPage = 8;

        /// `goodbyeLabel` and `moreLabel` are already localised by the caller.
        NeutralDialoguePager(std::string goodbyeLabel, std::string moreLabel, std::size_t topicsPerPage = kTopicsPerPage);

        /// A new turn: the page goes back to the first.
        void setTurn(const openeden::sdk::game::DialogueTurn& turn, const std::string& speakerName);

        [[nodiscard]] NeutralDialogueMenu menu() const;
        /// Interprets a pressed button of the menu last returned; a "More topics" press moves to the next page (the
        /// last page wraps to the first).
        [[nodiscard]] NeutralDialogueChoice press(int button);

        /// True when the last turn ended the conversation: the menu then holds the text and one button.
        [[nodiscard]] bool ended() const { return mEnded; }

        [[nodiscard]] std::size_t pageCount() const;

    private:
        std::string mGoodbye;
        std::string mMore;
        std::size_t mPerPage;
        std::string mMessage;
        std::vector<openeden::sdk::game::DialogueTopicChoice> mTopics;
        std::size_t mPage = 0;
        bool mEnded = false;
    };

    /// Runs one conversation at a time through the window manager.
    class NeutralDialogueSession
    {
    public:
        /// Opens the first turn of a conversation the source of game `gameId` has begun. The session keeps the game id,
        /// not the source: every poll looks the source up again, and ends if it is gone (a stopped pack, a new game).
        void start(std::string gameId, const openeden::sdk::game::DialogueTurn& first, const std::string& speakerName);

        /// One poll a frame: reads the pressed button and shows the next turn, or closes. Cheap when idle.
        void update();

        [[nodiscard]] bool active() const { return mActive; }
        /// Ends the conversation and tells the source (if it is still there).
        void close();
        /// Ends the session without calling into any source (the game is going away).
        void abandon();

        /// Test and developer aid: presses button `button` as if the player had, on the next poll.
        void injectPress(int button) { mInjected = button; }

        /// What the session did (for the developer trace and tests).
        [[nodiscard]] std::uint64_t turnsShown() const { return mTurns; }

    private:
        void show();

        bool mActive = false;
        int mInjected = -1;
        int mPolls = 0;
        std::vector<std::pair<int, int>> mScript; ///< from OPENEDEN_NEUTRAL_DIALOGUE_PRESS
        std::string mGameId;
        std::string mName;
        std::optional<NeutralDialoguePager> mPager;
        std::uint64_t mTurns = 0;
    };

    [[nodiscard]] NeutralDialogueSession& neutralDialogueSession();
}

#endif
