#include "neutralmusic.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace MWWorld
{
    namespace
    {
        using namespace openeden::sdk::game;

        std::string number(float value)
        {
            char buffer[32];
            std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(value));
            return buffer;
        }

        constexpr std::uint32_t kPlaysOneSelection = 0x01;
    }

    NeutralMusicDirector::NeutralMusicDirector(MusicSource& source, NeutralMusicSink& sink)
        : mSource(source)
        , mSink(sink)
    {
    }

    void NeutralMusicDirector::update(float dt, WorldId cell, float gameHour)
    {
        if (!std::isfinite(dt) || dt < 0.f)
            return;
        // The channel is taken at the first step, before the pack has read its records: a script's playlist must not
        // fill the seconds in between.
        if (!mClaimed)
        {
            mClaimed = true;
            mSink.claim();
            mSink.log("Neutral music: started");
        }
        if (!mSource.musicReady())
            return;
        mSinceAsk += dt;
        if (!mAsked || mSinceAsk >= NeutralMusicConstants::kAskPeriod)
        {
            mSinceAsk = 0.f;
            mAsked = true;
            ask(cell, gameHour);
        }
        advance(dt, cell, gameHour);
    }

    void NeutralMusicDirector::ask(WorldId cell, float gameHour)
    {
        MusicContext context;
        context.cell = cell;
        context.gameHour = gameHour;
        context.pick = mPick;
        MusicTypeDescription type;
        if (!mSource.musicType(context, type))
        {
            if (mHasType)
            {
                mHasType = false;
                mPlan = {};
                mState = State::Idle;
                mSink.fadeOut(mType.fadeSeconds);
                mSink.log("Neutral music: no type for this place; fading out");
            }
            return;
        }
        if (!mHasType || type.id != mType.id)
            enterType(type, cell, gameHour);
    }

    void NeutralMusicDirector::enterType(const MusicTypeDescription& description, WorldId cell, float gameHour)
    {
        mSink.log("Neutral music: type " + description.editorId + " (" + description.reason + ", priority "
            + std::to_string(description.priority) + ", fade " + number(description.fadeSeconds) + " s, ducking "
            + std::to_string(description.duckingCentiDb) + ", flags " + std::to_string(description.flags) + ")");
        mType = description;
        mHasType = true;
        mRetry = 0.f;
        mQuickEnds = 0;
        startPlan(description.fadeSeconds, cell, gameHour);
    }

    void NeutralMusicDirector::startPlan(float transitionFade, WorldId cell, float gameHour)
    {
        MusicContext context;
        context.cell = cell;
        context.gameHour = gameHour;
        context.pick = mPick;
        MusicPlan plan;
        if (!mSource.musicPlan(mType.id, context, plan) || plan.items.empty())
        {
            mState = State::Idle;
            mPlan = {};
            mRetry = NeutralMusicConstants::kRetryPeriod;
            mSink.log("Neutral music: no track of " + mType.editorId + " can play now");
            // The old music of another type does not go on over a type that has nothing to play.
            if (transitionFade >= 0.f)
                mSink.fadeOut(transitionFade);
            return;
        }
        ++mPick;
        mPlan = std::move(plan);
        mIndex = 0;
        mSink.log("Neutral music: track " + mPlan.trackName + " (" + std::to_string(mPlan.items.size())
            + " items, fade out " + number(mPlan.fadeOutSeconds) + " s"
            + (mPlan.finaleFile.empty() ? "" : ", finale " + mPlan.finaleFile)
            + ", pick " + std::to_string(context.pick) + ", hour "
            + std::to_string(static_cast<int>(std::floor(gameHour))) + ")");
        beginItem(transitionFade);
    }

    void NeutralMusicDirector::beginItem(float transitionFade)
    {
        const MusicItem& item = mPlan.items[mIndex];
        mItemTime = 0.f;
        if (item.kind == MusicItemKind::File)
        {
            mState = State::Playing;
            mSink.log("Neutral music: stream " + item.file);
            mSink.stream(item.file, std::max(0.f, transitionFade));
        }
        else
        {
            mState = State::Silent;
            mItemTime = std::clamp(item.seconds, 0.f, NeutralMusicConstants::kMaxSilence);
            mSink.log("Neutral music: silence " + number(mItemTime) + " s (" + item.name + ")");
            if (transitionFade > 0.f)
                mSink.fadeOut(transitionFade);
        }
    }

    void NeutralMusicDirector::advance(float dt, WorldId cell, float gameHour)
    {
        switch (mState)
        {
            case State::Idle:
                if (mHasType && mRetry > 0.f)
                {
                    mRetry -= dt;
                    if (mRetry <= 0.f)
                    {
                        mRetry = 0.f;
                        startPlan(-1.f, cell, gameHour);
                    }
                }
                break;
            case State::Playing:
                mItemTime += dt;
                if (mItemTime >= NeutralMusicConstants::kStartGrace && !mSink.playing())
                {
                    if (mItemTime < NeutralMusicConstants::kQuickEnd)
                        ++mQuickEnds;
                    else
                        mQuickEnds = 0;
                    itemEnded(cell, gameHour);
                }
                break;
            case State::Silent:
                mItemTime -= dt;
                if (mItemTime <= 0.f)
                    itemEnded(cell, gameHour);
                break;
            case State::Done:
                break;
        }
    }

    void NeutralMusicDirector::itemEnded(WorldId cell, float gameHour)
    {
        if (mQuickEnds >= NeutralMusicConstants::kMaxQuickEnds)
        {
            mQuickEnds = 0;
            mState = State::Idle;
            mRetry = NeutralMusicConstants::kRetryPeriod;
            mSink.log("Neutral music: the files do not play; waiting before the next try");
            return;
        }
        ++mIndex;
        if (mIndex < mPlan.items.size())
        {
            beginItem(0.f);
            return;
        }
        mSink.log("Neutral music: track " + mPlan.trackName + " ended");
        if ((mType.flags & kPlaysOneSelection) != 0)
        {
            mState = State::Done;
            return;
        }
        startPlan(-1.f, cell, gameHour);
    }
}
