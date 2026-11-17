#ifndef OPENMW_MWWORLD_NEUTRALMUSIC_H
#define OPENMW_MWWORLD_NEUTRALMUSIC_H

// The director of a game pack's music (task 0209, milestone M-S7). It asks the pack's MusicSource which music type fits
// the player's place and state, asks for the next plan of that type, and carries the plan out through a sink (the sound
// manager in the game): files are streamed one after the other, silences are waited for. Pure: it reads no clock (time
// is the sum of the dt it is given), no random source and no engine type, so a test drives it with a fake source and a
// fake sink.
//
// Threads: the main thread only (World::update).
// Determinism: every decision depends on the context given and on the sink's answers; the only thing that depends on
// real audio is when a streamed file ENDS (the sink says it stopped playing), which is why the trace lines carry no
// times.
// Restart on load: nothing is saved; a loaded game starts a fresh director, which asks again (the pick counter starts
// at 0, so the same place gives the same first track as a new game's).

#include <openeden/game/music_source.hpp>

#include <cstdint>
#include <string>

namespace MWWorld
{
    /// Where the director's decisions go. The game's implementation talks to the sound manager.
    class NeutralMusicSink
    {
    public:
        virtual ~NeutralMusicSink() = default;
        /// Streams a file; the music playing now fades out for `fadeSeconds` first (0: it is stopped at once).
        virtual void stream(const std::string& vfsPath, float fadeSeconds) = 0;
        /// Fades the music out and plays nothing next.
        virtual void fadeOut(float fadeSeconds) = 0;
        /// Takes the music channel over (an empty stream with no fade): from now on a script cannot take it.
        virtual void claim() = 0;
        /// Is a music stream playing (or fading) now?
        virtual bool playing() = 0;
        /// One line of the decision log.
        virtual void log(const std::string& line) = 0;
    };

    namespace NeutralMusicConstants
    {
        /// How often the type is asked for, in seconds of dt: a place or a fight changes at most this late.
        inline constexpr float kAskPeriod = 0.25f;
        /// A streamed file that has not been playing for this long is not yet judged "ended" (the stream starts after
        /// the previous one has faded out and a decoder has opened the file).
        inline constexpr float kStartGrace = 0.5f;
        /// A file that ended before this many seconds counts as a failure to play.
        inline constexpr float kQuickEnd = 1.0f;
        /// After this many failures in a row the director waits kRetryPeriod before it tries again.
        inline constexpr int kMaxQuickEnds = 3;
        /// Seconds to wait after a type with no plan, or after repeated failures, before asking again.
        inline constexpr float kRetryPeriod = 5.0f;
        /// The longest silence the director waits for, in seconds (a hostile record cannot park the music for days).
        inline constexpr float kMaxSilence = 600.f;
    }

    class NeutralMusicDirector
    {
    public:
        NeutralMusicDirector(openeden::sdk::game::MusicSource& source, NeutralMusicSink& sink);

        /// One step of `dt` seconds. `cell` is the player's neutral cell id, `gameHour` the game hour.
        void update(float dt, openeden::sdk::game::WorldId cell, float gameHour);

        [[nodiscard]] bool hasType() const { return mHasType; }
        [[nodiscard]] const openeden::sdk::game::MusicTypeDescription& type() const { return mType; }
        [[nodiscard]] std::uint32_t picks() const { return mPick; }
        [[nodiscard]] bool done() const { return mState == State::Done; }

    private:
        enum class State : std::uint8_t
        {
            Idle, ///< no plan running
            Playing, ///< a file of the plan is streaming
            Silent, ///< waiting out a silence
            Done, ///< the type plays one selection and it has played
        };

        void ask(openeden::sdk::game::WorldId cell, float gameHour);
        void enterType(const openeden::sdk::game::MusicTypeDescription& description, openeden::sdk::game::WorldId cell,
            float gameHour);
        void startPlan(float transitionFade, openeden::sdk::game::WorldId cell, float gameHour);
        void beginItem(float transitionFade);
        void advance(float dt, openeden::sdk::game::WorldId cell, float gameHour);
        void itemEnded(openeden::sdk::game::WorldId cell, float gameHour);

        openeden::sdk::game::MusicSource& mSource;
        NeutralMusicSink& mSink;
        bool mClaimed = false;
        bool mHasType = false;
        bool mAsked = false;
        openeden::sdk::game::MusicTypeDescription mType;
        openeden::sdk::game::MusicPlan mPlan;
        std::size_t mIndex = 0;
        State mState = State::Idle;
        float mSinceAsk = 0.f;
        float mItemTime = 0.f; ///< seconds the current item has been going (a file) or is left (a silence)
        float mRetry = 0.f; ///< seconds until a failed plan is tried again
        int mQuickEnds = 0;
        std::uint32_t mPick = 0;
    };
}

#endif
