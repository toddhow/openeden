#include "neutraldialogue.hpp"

#include <algorithm>
#include <cstdlib>

#include <components/debug/debuglog.hpp>
#include <components/l10n/manager.hpp>
#include <components/worldmodel/store.hpp>

#include <components/files/istreamptr.hpp>
#include <components/fuz/fuz.hpp>
#include <components/misc/resourcehelpers.hpp>
#include <components/resource/resourcesystem.hpp>
#include <components/vfs/manager.hpp>
#include <components/settings/values.hpp>
#include <components/vfs/pathutil.hpp>

#include "../mwbase/environment.hpp"
#include "../mwbase/extensionmanager.hpp"
#include "../mwbase/soundmanager.hpp"
#include "../mwbase/windowmanager.hpp"

namespace MWWorld
{
    std::string neutralServiceNote(const openeden::sdk::game::DialogueTurn& turn, const std::string& speakerName)
    {
        if (turn.service.empty())
            return std::string();
        const std::string& name = turn.speakerName.empty() ? speakerName : turn.speakerName;
        return "service not implemented: " + turn.service + " with " + (name.empty() ? std::string("the speaker") : name);
    }

    NeutralDialoguePager::NeutralDialoguePager(std::string goodbyeLabel, std::string moreLabel, std::size_t topicsPerPage)
        : mGoodbye(std::move(goodbyeLabel))
        , mMore(std::move(moreLabel))
        , mPerPage(std::max<std::size_t>(1, topicsPerPage))
    {
    }

    void NeutralDialoguePager::setTurn(const openeden::sdk::game::DialogueTurn& turn, const std::string& speakerName)
    {
        const std::string& name = turn.speakerName.empty() ? speakerName : turn.speakerName;
        mMessage = name.empty() ? turn.text : name + "\n\n" + turn.text;
        mTopics = turn.topics;
        mPage = 0;
        mEnded = turn.endsConversation;
        if (mEnded)
            mTopics.clear();
    }

    std::size_t NeutralDialoguePager::pageCount() const
    {
        return std::max<std::size_t>(1, (mTopics.size() + mPerPage - 1) / mPerPage);
    }

    NeutralDialogueMenu NeutralDialoguePager::menu() const
    {
        NeutralDialogueMenu menu;
        menu.message = mMessage;
        const std::size_t first = mPage * mPerPage;
        const std::size_t last = std::min(mTopics.size(), first + mPerPage);
        for (std::size_t i = first; i < last; ++i)
            menu.buttons.push_back(mTopics[i].text);
        if (pageCount() > 1)
            menu.buttons.push_back(mMore);
        menu.buttons.push_back(mGoodbye);
        return menu;
    }

    NeutralDialogueChoice NeutralDialoguePager::press(int button)
    {
        NeutralDialogueChoice choice;
        if (button < 0)
            return choice;
        const std::size_t index = static_cast<std::size_t>(button);
        const std::size_t first = mPage * mPerPage;
        const std::size_t onPage = std::min(mTopics.size(), first + mPerPage) - std::min(mTopics.size(), first);
        if (index < onPage)
        {
            choice.kind = NeutralDialogueChoice::Kind::Topic;
            choice.topicId = mTopics[first + index].id;
            return choice;
        }
        std::size_t next = onPage;
        if (pageCount() > 1)
        {
            if (index == next)
            {
                mPage = (mPage + 1) % pageCount();
                choice.kind = NeutralDialogueChoice::Kind::MoreTopics;
                return choice;
            }
            ++next;
        }
        if (index == next)
            choice.kind = NeutralDialogueChoice::Kind::Goodbye;
        return choice;
    }

    void NeutralVoiceQueue::set(std::vector<std::string> paths)
    {
        mPaths.clear();
        mNext = 0;
        mStarted = 0;
        for (std::string& path : paths)
        {
            if (path.empty())
                continue;
            if (mPaths.size() >= kMaxLines)
                break;
            mPaths.push_back(std::move(path));
        }
    }

    void NeutralVoiceQueue::clear()
    {
        set({});
    }

    std::optional<std::string> NeutralVoiceQueue::poll(bool linePlaying)
    {
        if (linePlaying || mNext >= mPaths.size())
            return std::nullopt;
        ++mStarted;
        return mPaths[mNext++];
    }

    std::vector<std::pair<int, int>> parseNeutralDialoguePressScript(const std::string& text)
    {
        std::vector<std::pair<int, int>> out;
        std::size_t pos = 0;
        while (pos <= text.size())
        {
            const std::size_t end = std::min(text.find(',', pos), text.size());
            const std::string entry = text.substr(pos, end - pos);
            const std::size_t colon = entry.find(':');
            if (colon != std::string::npos && colon > 0 && colon + 1 < entry.size())
            {
                const std::string a = entry.substr(0, colon);
                const std::string b = entry.substr(colon + 1);
                const bool digits = std::all_of(a.begin(), a.end(), [](unsigned char c) { return c >= '0' && c <= '9'; })
                    && std::all_of(b.begin(), b.end(), [](unsigned char c) { return c >= '0' && c <= '9'; });
                if (digits && a.size() < 7 && b.size() < 4)
                    out.emplace_back(std::atoi(a.c_str()), std::atoi(b.c_str()));
            }
            pos = end + 1;
        }
        std::stable_sort(out.begin(), out.end(), [](const auto& l, const auto& r) { return l.first < r.first; });
        return out;
    }

    namespace
    {
        NeutralMouthSink& mouthSink()
        {
            static NeutralMouthSink sSink;
            return sSink;
        }
    }

    void setNeutralMouthSink(NeutralMouthSink sink)
    {
        mouthSink() = std::move(sink);
    }

    NeutralDialogueSession& neutralDialogueSession()
    {
        static NeutralDialogueSession session;
        return session;
    }

    namespace
    {
        openeden::sdk::game::DialogueSource* findSource(const std::string& gameId)
        {
            MWBase::ExtensionManager* extensions = MWBase::Environment::get().getExtensionManager();
            NeutralWorld::NeutralWorldStore* store = extensions != nullptr ? extensions->neutralWorldStore() : nullptr;
            return dynamic_cast<openeden::sdk::game::DialogueSource*>(store != nullptr ? store->source(gameId) : nullptr);
        }
    }

    void NeutralDialogueSession::start(std::string gameId, const openeden::sdk::game::DialogueTurn& first,
        const std::string& speakerName, const Ptr& speaker)
    {
        if (mActive)
            close();
        const MWBase::Environment& env = MWBase::Environment::get();
        const auto message = [&](const char* key) { return env.getL10nManager()->getMessage("OMWEngine", key); };
        mActive = true;
        mInjected = -1;
        mPolls = 0;
        mTurns = 0;
        const char* script = std::getenv("OPENEDEN_NEUTRAL_DIALOGUE_PRESS");
        mScript = script != nullptr ? parseNeutralDialoguePressScript(script) : std::vector<std::pair<int, int>>{};
        mGameId = std::move(gameId);
        mName = speakerName;
        mPager.emplace(message("NeutralDialogueGoodbye"), message("NeutralDialogueMore"));
        mPager->setTurn(first, mName);
        mSpeaker = speaker;
        show();
        speak(first);
    }

    void NeutralDialogueSession::speak(const openeden::sdk::game::DialogueTurn& turn)
    {
        stopVoice();
        if (mSpeaker.isEmpty() || turn.voice.empty())
            return;
        mVoice.set(turn.voice);
        Log(Debug::Info) << "NTALK voice: " << mVoice.pending() << " line(s) for this turn";
        pollVoice();
    }

    void NeutralDialogueSession::pollVoice()
    {
        if (mSpeaker.isEmpty() || mVoice.pending() == 0)
            return;
        MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
        if (sound == nullptr)
            return;
        const std::optional<std::string> path = mVoice.poll(sound->sayActive(mSpeaker));
        if (!path)
            return;
        // The path is the pack's, relative to the sound folder; say() takes the VFS path, so the caller adds "sound/"
        // (as the Morrowind dialogue does). The decoder reads a .fuz as a lip file plus xWMA audio.
        Log(Debug::Info) << "NTALK voice: line " << mVoice.started() << " " << *path;
        const VFS::Path::Normalized soundPath = Misc::ResourceHelpers::correctSoundPath(VFS::Path::Normalized(*path));
        sound->say(mSpeaker, soundPath);
        Log(Debug::Info) << "NTALK voice: line " << mVoice.started() << " "
                         << (sound->sayActive(mSpeaker) ? "playing" : "did not start");
    }

    void NeutralDialogueSession::driveMouth()
    {
        const NeutralMouthSink& sink = mouthSink();
        if (!sink || mSpeaker.isEmpty())
            return;
        MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager();
        if (sound == nullptr)
            return;
        // The line's own lip data at the playback position when it has some; else the loudness of the line.
        std::string_view file;
        float seconds = 0.f;
        const bool saying = sound->getSayPlayback(mSpeaker, file, seconds);
        TriMorph::VisemeWeights w{};
        if (saying)
        {
            if (file != mLipFile)
                loadLip(std::string(file));
            if (mLip && Lip::sampleVisemes(*mLip, seconds, w))
                mMouth.reset();
            else
                w = TriMorph::toVisemes(mMouth.step(sound->getSaySoundLoudness(mSpeaker)));
        }
        else
        {
            mLipFile.clear();
            mLip.reset();
            w = TriMorph::toVisemes(mMouth.step(0.f)); // the jaw closes
        }
        const bool open = std::any_of(w.begin(), w.end(), [](float x) { return x > 0.f; });
        if (!open && !mMouthOpen)
            return; // closed and staying closed: nothing to send
        sink(mSpeaker, w);
        mMouthOpen = open;
    }

    void NeutralDialogueSession::loadLip(const std::string& file)
    {
        mLipFile = file;
        mLip.reset();
        try
        {
            const VFS::Manager* vfs = MWBase::Environment::get().getResourceSystem()->getVFS();
            const Files::IStreamPtr stream = vfs->find(VFS::Path::Normalized(file));
            if (!stream)
                return;
            const std::string bytes((std::istreambuf_iterator<char>(*stream)), std::istreambuf_iterator<char>());
            const Fuz::Result parts = Fuz::split(bytes, file);
            if (!parts.mOk || parts.mParts.mLip.empty())
            {
                Log(Debug::Info) << "NLIP no lip data in " << file << (parts.mOk ? "" : ": " + parts.mError);
                return;
            }
            Lip::Result lip = Lip::read(parts.mParts.mLip, file);
            if (!lip.mOk || !lip.mTrack.mTimingKnown)
            {
                Log(Debug::Info) << "NLIP lip data of " << file << " not used: "
                                 << (lip.mOk ? "the timing is not known" : lip.mError);
                return;
            }
            Log(Debug::Info) << "NLIP lip data of " << file << ": " << lip.mTrack.mFrames << " frames, first "
                             << lip.mTrack.mFirst;
            mLip = std::move(lip.mTrack);
        }
        catch (const std::exception& e)
        {
            Log(Debug::Warning) << "Neutral lip sync: " << file << ": " << e.what();
        }
    }

    void NeutralDialogueSession::stopVoice()
    {
        mVoice.clear();
        if (mSpeaker.isEmpty())
            return;
        if (MWBase::SoundManager* sound = MWBase::Environment::get().getSoundManager())
            sound->stopSay(mSpeaker);
    }

    void NeutralDialogueSession::show()
    {
        const NeutralDialogueMenu menu = mPager->menu();
        MWBase::Environment::get().getWindowManager()->interactiveMessageBox(menu.message, menu.buttons);
        ++mTurns;
        Log(Debug::Info) << "NTALK turn " << mTurns << " poll " << mPolls << ": " << menu.buttons.size()
                         << " buttons, message of " << menu.message.size() << " bytes";
    }

    void NeutralDialogueSession::update()
    {
        if (!mActive)
            return;
        openeden::sdk::game::DialogueSource* source = findSource(mGameId);
        if (source == nullptr)
        {
            abandon(); // the pack is gone: nothing to call into
            return;
        }
        ++mPolls;
        pollVoice();
        if (Settings::game().mNeutralLipSync)
            driveMouth();
        int pressed = MWBase::Environment::get().getWindowManager()->readPressedButton();
        for (const auto& [poll, button] : mScript)
        {
            if (poll == mPolls && pressed < 0)
                mInjected = button;
        }
        if (pressed < 0 && mInjected >= 0)
        {
            pressed = mInjected; // OPENEDEN_NEUTRAL_DIALOGUE_PRESS
            mInjected = -1;
        }
        if (pressed < 0)
            return;
        const bool ended = mPager->ended();
        const NeutralDialogueChoice choice = mPager->press(pressed);
        switch (choice.kind)
        {
            case NeutralDialogueChoice::Kind::MoreTopics:
                show();
                return;
            case NeutralDialogueChoice::Kind::Topic:
            {
                openeden::sdk::game::DialogueTurn turn;
                std::string error;
                if (!source->chooseTopic(choice.topicId, turn, error))
                {
                    Log(Debug::Warning) << "Neutral dialogue: " << error;
                    close();
                    return;
                }
                if (const std::string note = neutralServiceNote(turn, mName); !note.empty())
                    Log(Debug::Warning) << "NTALK " << note;
                mPager->setTurn(turn, mName);
                show();
                speak(turn);
                return;
            }
            case NeutralDialogueChoice::Kind::Goodbye:
            case NeutralDialogueChoice::Kind::None:
                (void)ended;
                close();
                return;
        }
    }

    void NeutralDialogueSession::close()
    {
        if (mActive)
        {
            if (openeden::sdk::game::DialogueSource* source = findSource(mGameId))
                source->endConversation();
        }
        abandon();
    }

    void NeutralDialogueSession::abandon()
    {
        if (mActive)
        {
            Log(Debug::Info) << "NTALK closed after " << mTurns << " turns";
            stopVoice();
            if (mMouthOpen && mouthSink() && !mSpeaker.isEmpty())
                mouthSink()(mSpeaker, TriMorph::VisemeWeights{}); // close the mouth of whoever was speaking
        }
        mMouth.reset();
        mMouthOpen = false;
        mLipFile.clear();
        mLip.reset();
        mSpeaker = Ptr();
        mActive = false;
        mInjected = -1;
        mPager.reset();
    }
}
