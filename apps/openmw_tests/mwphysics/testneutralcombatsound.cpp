// Task 0193 (combat stage S5-10): the engine's side of CombatRequestWriter::playSound (the combat bridge): a sound for
// the player reaches the sink with no actor key, a stale actor, an empty or over-long path or a volume that is not a
// number is refused and counted, a sink that throws makes the call return false (nothing reaches the pack), and a call
// made outside combatUpdate is refused.
#include "apps/openmw/mwphysics/neutralcombatbridge.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <functional>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    using namespace openeden::sdk::game;
    using MWPhysics::NeutralActorBridge;
    using MWPhysics::NeutralCombatBridge;

    class Rules final : public CombatRules
    {
    public:
        void onClipEvent(const ClipEvent&) override {}
        void onSweepResult(CombatSubject, std::uint32_t, const SweepHit*, std::size_t) override {}
        void combatUpdate(float, CombatRequestWriter& writer) override
        {
            mWriter = &writer;
            if (mDo)
                mDo(writer);
        }
        std::function<void(CombatRequestWriter&)> mDo;
        CombatRequestWriter* mWriter = nullptr;
    };

    class Sink final : public MWPhysics::NeutralClipSink
    {
    public:
        struct Played
        {
            const void* mKey;
            std::string mFile, mName;
            float mVolume;
        };
        void playClip(const void*, std::string_view, bool) override {}
        void stopClip(const void*) override {}
        void playSound(const void* key, std::string_view file, std::string_view name, float volume) override
        {
            if (mThrow)
                throw std::runtime_error("sink");
            mPlayed.push_back({ key, std::string(file), std::string(name), volume });
        }
        bool mThrow = false;
        std::vector<Played> mPlayed;
    };

    struct BridgeSound : ::testing::Test
    {
        NeutralActorBridge mActors;
        NeutralCombatBridge mBridge{ mActors };
        Rules mRules;
        Sink mSink;
        MWPhysics::NeutralClipMetaFn mMeta = [](const void*) { return HavokPack::ClipProgress(); };

        void frame(std::function<void(CombatRequestWriter&)> doIt)
        {
            mRules.mDo = std::move(doIt);
            mBridge.update(&mRules, 0.1f, mSink, mMeta, nullptr);
        }
        static SoundRequest request(CombatSubject who, std::string_view file = "sound/fx/wpn/swing.wav")
        {
            SoundRequest r;
            r.subject = who;
            r.file = file;
            r.name = "WPNSwingBladeMediumSD";
            r.volume = 0.5f;
            return r;
        }
    };

    TEST_F(BridgeSound, aSoundForThePlayerReachesTheSinkWithNoKey)
    {
        bool ok = false;
        frame([&](CombatRequestWriter& w) { ok = w.playSound(request(CombatSubject::ofPlayer())); });
        EXPECT_TRUE(ok);
        ASSERT_EQ(mSink.mPlayed.size(), 1u);
        EXPECT_EQ(mSink.mPlayed[0].mKey, nullptr);
        EXPECT_EQ(mSink.mPlayed[0].mFile, "sound/fx/wpn/swing.wav");
        EXPECT_EQ(mSink.mPlayed[0].mName, "WPNSwingBladeMediumSD");
        EXPECT_FLOAT_EQ(mSink.mPlayed[0].mVolume, 0.5f);
        EXPECT_EQ(mBridge.stats().mSoundsPlayed, 1u);
    }

    TEST_F(BridgeSound, aStaleActorIsRefusedAndCounted)
    {
        bool ok = true;
        frame([&](CombatRequestWriter& w) { ok = w.playSound(request(CombatSubject::of({ 77, 3 }))); });
        EXPECT_FALSE(ok);
        EXPECT_TRUE(mSink.mPlayed.empty());
        EXPECT_EQ(mBridge.stats().mSoundsRefused, 1u);
    }

    TEST_F(BridgeSound, aBadRequestIsRefused)
    {
        int refused = 0;
        frame([&](CombatRequestWriter& w) {
            refused += !w.playSound(request(CombatSubject::ofPlayer(), ""));
            refused += !w.playSound(request(CombatSubject::ofPlayer(), std::string(kMaxCombatSoundPath + 1, 'a')));
            SoundRequest nan = request(CombatSubject::ofPlayer());
            nan.volume = std::numeric_limits<float>::quiet_NaN();
            refused += !w.playSound(nan);
            refused += !w.playSound(request(CombatSubject{}));
        });
        EXPECT_EQ(refused, 4);
        EXPECT_TRUE(mSink.mPlayed.empty());
        EXPECT_EQ(mBridge.stats().mSoundsRefused, 4u);
    }

    TEST_F(BridgeSound, aVolumeOutsideZeroToOneIsClamped)
    {
        frame([&](CombatRequestWriter& w) {
            SoundRequest loud = request(CombatSubject::ofPlayer());
            loud.volume = 7.f;
            SoundRequest quiet = loud;
            quiet.volume = -2.f;
            (void)w.playSound(loud);
            (void)w.playSound(quiet);
        });
        ASSERT_EQ(mSink.mPlayed.size(), 2u);
        EXPECT_FLOAT_EQ(mSink.mPlayed[0].mVolume, 1.f);
        EXPECT_FLOAT_EQ(mSink.mPlayed[1].mVolume, 0.f);
    }

    TEST_F(BridgeSound, aSinkThatThrowsMakesTheCallFalseAndIsCounted)
    {
        mSink.mThrow = true;
        bool ok = true;
        frame([&](CombatRequestWriter& w) { ok = w.playSound(request(CombatSubject::ofPlayer())); });
        EXPECT_FALSE(ok);
        EXPECT_EQ(mBridge.stats().mWriterFailures, 1u);
    }

    TEST_F(BridgeSound, aCallOutsideCombatUpdateIsRefused)
    {
        frame([](CombatRequestWriter&) {});
        ASSERT_NE(mRules.mWriter, nullptr);
        EXPECT_FALSE(mRules.mWriter->playSound(request(CombatSubject::ofPlayer())));
        EXPECT_TRUE(mSink.mPlayed.empty());
    }
}
