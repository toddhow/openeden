#ifndef OPENMW_MWWORLD_NEUTRALSCENESPEECH_H
#define OPENMW_MWWORLD_NEUTRALSCENESPEECH_H

// Task 0194 (milestone M-S6, scenes): the lines a game pack's quest scenes make its actors say. The pack queues a line
// (subtitle text and voice files); this plays them: per actor, one line after the other, each line's voice files one
// after the other, the subtitle shown when the line starts. Pure over an opaque actor key and a `Sink`, so a test
// drives it with a fake. Main thread only; allocation: only when a line is added.

#include "neutraldialogue.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <string>
#include <vector>

namespace MWWorld
{
    class NeutralSceneSpeech
    {
    public:
        class Sink
        {
        public:
            virtual ~Sink() = default;
            /// Does the actor's voice still play?
            virtual bool active(const void* actor) = 0;
            /// Starts a voice file (the pack's path, relative to the sound folder).
            virtual void say(const void* actor, const std::string& path) = 0;
            virtual void subtitle(const void* actor, const std::string& text) = 0;
        };

        static constexpr std::size_t kMaxQueuedLines = 32; ///< per actor; the rest are dropped and counted

        /// Queues a line. False (counted) when the actor's queue is full.
        bool add(const void* actor, std::string text, std::vector<std::string> voice);
        /// Once a frame. Starts the next line of an actor whose last line is over, and the next voice file of a line
        /// whose file ended.
        void update(Sink& sink);
        /// An actor is gone: its lines are dropped.
        void remove(const void* actor);
        /// Task 0273b: every actor's lines are dropped (a load or a new game: the actors are gone). The counters stay.
        void clear();

        /// Task 0235: the actor has a line queued or one that has not finished (the mouth keeps moving while it is).
        [[nodiscard]] bool busy(const void* actor) const;

        [[nodiscard]] std::size_t pending() const;
        [[nodiscard]] std::uint64_t linesStarted() const { return mLinesStarted; }
        [[nodiscard]] std::uint64_t filesStarted() const { return mFilesStarted; }
        [[nodiscard]] std::uint64_t dropped() const { return mDropped; }

    private:
        struct Line
        {
            std::string mText;
            std::vector<std::string> mVoice;
        };
        struct Speaker
        {
            std::deque<Line> mLines;
            NeutralVoiceQueue mVoice;
            bool mOpen = false; ///< a line has started and its files are not all done
        };

        std::map<const void*, Speaker> mSpeakers;
        std::uint64_t mLinesStarted = 0;
        std::uint64_t mFilesStarted = 0;
        std::uint64_t mDropped = 0;
    };

    /// Task 0273b: an actor's cell unloaded. Drops its queued lines, its entry of the speaker table (the `Ptr` kept for
    /// it would dangle) and its mouth driver. `speech` may be null (nothing was ever queued).
    template <class Speakers, class Mouths>
    void forgetNeutralSpeaker(NeutralSceneSpeech* speech, Speakers& speakers, Mouths& mouths, const void* actor)
    {
        if (speech != nullptr)
            speech->remove(actor);
        speakers.erase(actor);
        mouths.erase(actor);
    }
}

#endif
