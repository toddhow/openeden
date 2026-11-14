#ifndef OPENMW_MWWORLD_NEUTRALDIALOGUE_H
#define OPENMW_MWWORLD_NEUTRALDIALOGUE_H

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <components/lip/lip.hpp>
#include <components/trimorph/mouth.hpp>

#include <openeden/game/dialogue_source.hpp>

#include "ptr.hpp"

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
    /// The log line for a response that is a service the pack cannot run (task 0155), for example
    /// "service not implemented: barter with Hulda"; empty when the turn has no service.
    [[nodiscard]] std::string neutralServiceNote(
        const openeden::sdk::game::DialogueTurn& turn, const std::string& speakerName);

    [[nodiscard]] std::vector<std::pair<int, int>> parseNeutralDialoguePressScript(const std::string& text);

    class NeutralDialoguePager
    {
    public:
        static constexpr std::size_t kTopicsPerPage = 8;

        /// `goodbyeLabel` and `moreLabel` are already localised by the caller.
        NeutralDialoguePager(
            std::string goodbyeLabel, std::string moreLabel, std::size_t topicsPerPage = kTopicsPerPage);

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

    /// The voice lines of the turn on screen, played one after the other (task 0143). Pure: the session tells it
    /// whether the line it started last is still playing and gets back the next path to start. The paths are the pack's
    /// (DialogueTurn::voice), relative to the sound folder.
    class NeutralVoiceQueue
    {
    public:
        static constexpr std::size_t kMaxLines
            = 64; ///< a response has few lines; the rest of a hostile list is dropped

        /// A new turn: whatever was pending is dropped (the session stops the line that is playing).
        void set(std::vector<std::string> paths);
        void clear();

        /// Called once a poll with whether the last started line still plays. Returns the next line to start, or
        /// nothing while one plays or none is left.
        [[nodiscard]] std::optional<std::string> poll(bool linePlaying);

        [[nodiscard]] std::size_t pending() const { return mPaths.size() - mNext; }
        [[nodiscard]] std::size_t started() const { return mStarted; }

    private:
        std::vector<std::string> mPaths;
        std::size_t mNext = 0;
        std::size_t mStarted = 0;
    };

    /// Task 0135 stage S6g: the wait between "the player activated an actor" and "the conversation box opens", while
    /// the pack gets the actor ready (a sleeper stands up; the box pauses the world, so it must be done first).
    /// Pure: the session tells it each frame whether the pack says the actor can talk.
    class NeutralTalkWait
    {
    public:
        /// The longest wait. The quick get-up clip is 1 s (`bed_leftquickexit`); the bound only keeps a clip that never
        /// ends from blocking the talk for good.
        static constexpr float kMaxSeconds = 5.f;

        enum class Result
        {
            Waiting, ///< not ready yet (or nothing is being waited for)
            Ready, ///< the pack says the actor can talk: begin the conversation
            TimedOut, ///< kMaxSeconds passed: begin the conversation anyway
        };

        void begin()
        {
            mActive = true;
            mElapsed = 0.f;
        }
        void clear() { mActive = false; }
        [[nodiscard]] bool active() const { return mActive; }
        [[nodiscard]] float elapsed() const { return mElapsed; }

        /// One frame of unpaused time. A non-positive or non-finite `seconds` counts as 0. Ready wins over a timeout in
        /// the same step. A Ready or TimedOut result ends the wait.
        [[nodiscard]] Result step(float seconds, bool ready);

    private:
        bool mActive = false;
        float mElapsed = 0.f;
    };

    /// Task 0146 (`[Game] neutral lip sync`): where the speaker's mouth weights go (Aah, BigAah, each 0..1). The world
    /// sets it to the renderer while it exists; unset, the drive does nothing.
    using NeutralMouthSink = std::function<void(const Ptr&, const TriMorph::VisemeWeights&)>;
    void setNeutralMouthSink(NeutralMouthSink sink);

    /// Runs one conversation at a time through the window manager.
    class NeutralDialogueSession
    {
    public:
        /// Opens the first turn of a conversation the source of game `gameId` has begun. The session keeps the game id,
        /// not the source: every poll looks the source up again, and ends if it is gone (a stopped pack, a new game).
        /// `speaker` is the object that talks: its voice lines (DialogueTurn::voice, task 0143) play from its head. An
        /// empty Ptr means no voice. The session keeps the Ptr until the conversation ends: the message box is modal.
        void start(std::string gameId, const openeden::sdk::game::DialogueTurn& first, const std::string& speakerName,
            const Ptr& speaker = Ptr());

        /// One poll a frame: reads the pressed button and shows the next turn, or closes. Cheap when idle. `seconds` is
        /// this frame's UNPAUSED time (0 while the game is paused); it only counts down a pending talk (stage S6g).
        void update(float seconds = 0.f);

        /// Stage S6g: the pack was asked to get the actor `reference` ready (DialogueSource::prepareConversation said
        /// not yet). Every update asks readyForConversation; when it says true, or NeutralTalkWait::kMaxSeconds have
        /// passed (logged once), `start` runs (it begins the conversation). The wait ends without `start` when the
        /// speaker's cell is no longer loaded (the pack is told to cancel). One wait at a time: false when one is
        /// pending already.
        bool startWhenReady(
            std::string gameId, openeden::sdk::game::WorldId reference, const Ptr& speaker, std::function<void()> start);
        [[nodiscard]] bool waitingToTalk() const { return mWait.active(); }

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
        /// Starts the voice lines of `turn` (stopping those of the turn before).
        void speak(const openeden::sdk::game::DialogueTurn& turn);
        /// Once a poll: starts the next queued line when the one before has ended.
        void pollVoice();
        void stopVoice();
        /// Task 0146: once a poll, with the setting on: the speaker's jaw follows the loudness of its voice line.
        void driveMouth();
        /// Reads the `.lip` part of the voice file `file` (a VFS path) into mLip, or leaves it empty (logged).
        void loadLip(const std::string& file);

        void updatePendingTalk(float seconds);

        NeutralTalkWait mWait;
        std::string mWaitGameId;
        openeden::sdk::game::WorldId mWaitReference = openeden::sdk::game::kNoWorldId;
        Ptr mWaitSpeaker;
        std::function<void()> mWaitStart;

        Ptr mSpeaker;
        TriMorph::MouthDrive mMouth;
        bool mMouthOpen = false; ///< a nonzero weight was sent and not yet closed
        std::string mLipFile; ///< the voice file whose lip data mLip holds (or failed to give)
        std::optional<Lip::Track> mLip;
        NeutralVoiceQueue mVoice;
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
