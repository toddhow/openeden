#include "neutralscenespeech.hpp"

#include <utility>

namespace MWWorld
{
    bool NeutralSceneSpeech::add(const void* actor, std::string text, std::vector<std::string> voice)
    {
        Speaker& speaker = mSpeakers[actor];
        if (speaker.mLines.size() >= kMaxQueuedLines)
        {
            ++mDropped;
            return false;
        }
        speaker.mLines.push_back(Line{ std::move(text), std::move(voice) });
        return true;
    }

    void NeutralSceneSpeech::update(Sink& sink)
    {
        for (auto it = mSpeakers.begin(); it != mSpeakers.end();)
        {
            Speaker& speaker = it->second;
            const void* actor = it->first;
            if (speaker.mOpen)
            {
                const bool playing = sink.active(actor);
                if (speaker.mVoice.pending() > 0)
                {
                    if (const std::optional<std::string> path = speaker.mVoice.poll(playing))
                    {
                        sink.say(actor, *path);
                        ++mFilesStarted;
                    }
                }
                else if (!playing)
                    speaker.mOpen = false;
            }
            if (!speaker.mOpen && !speaker.mLines.empty())
            {
                Line line = std::move(speaker.mLines.front());
                speaker.mLines.pop_front();
                ++mLinesStarted;
                if (!line.mText.empty())
                    sink.subtitle(actor, line.mText);
                speaker.mVoice.set(std::move(line.mVoice));
                speaker.mOpen = speaker.mVoice.pending() > 0;
                if (speaker.mOpen)
                    if (const std::optional<std::string> path = speaker.mVoice.poll(sink.active(actor)))
                    {
                        sink.say(actor, *path);
                        ++mFilesStarted;
                    }
            }
            if (!speaker.mOpen && speaker.mLines.empty())
                it = mSpeakers.erase(it);
            else
                ++it;
        }
    }

    void NeutralSceneSpeech::remove(const void* actor)
    {
        mSpeakers.erase(actor);
    }

    void NeutralSceneSpeech::clear()
    {
        mSpeakers.clear();
    }

    bool NeutralSceneSpeech::busy(const void* actor) const
    {
        const auto it = mSpeakers.find(actor);
        return it != mSpeakers.end() && (it->second.mOpen || !it->second.mLines.empty());
    }

    std::size_t NeutralSceneSpeech::pending() const
    {
        std::size_t total = 0;
        for (const auto& [actor, speaker] : mSpeakers)
            total += speaker.mLines.size() + (speaker.mOpen ? 1 : 0);
        return total;
    }
}
