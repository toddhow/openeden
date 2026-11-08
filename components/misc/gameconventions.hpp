#ifndef OPENEDEN_MISC_GAMECONVENTIONS_H
#define OPENEDEN_MISC_GAMECONVENTIONS_H

// The data conventions of the game being played, as the generic engine asks for them (task 0085, move 1; design doc
// docs/design/game-packs.md): the length unit, the humanoid skeleton's bone names, which model files are skeletons,
// and the first-person naming. They come from the `openeden.game` contract of the compatibility pack
// (`OMW::ExtensionHost` copies them here when the pack has started), so generic code never names a game.
//
// THE ONE PLACE THAT NAMES MORROWIND'S VALUES IS `GameConventions`' defaults below: they are the fallback for a session
// with no compatibility pack (the core must run with zero extensions, and Eden Studio and the tools start without
// one), and they equal what `compat_morrowind` provides, so Morrowind plays the same either way (a test pins this).
//
// Threads: the values are published as an immutable snapshot (`setGameConventions`, called once on the main thread
// when the pack starts and again, reset, when it stops); every reader takes one atomic pointer load and may hold the
// returned reference for ever, because a snapshot is never freed. A reader that started before the pack did sees the
// defaults, which are Morrowind's: with another game, values needed before `ExtensionHost::start()` (the first
// loading screen) are Morrowind's until then (task 0085 stage log, open decision).

#include "constants.hpp"

#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace Misc
{
    struct GameConventions
    {
        // World length units per meter. Equals Constants::UnitsPerMeter bit for bit by default.
        float mUnitsPerMeter = Constants::UnitsPerMeter;

        // The humanoid skeleton's bone names, lower case (the engine looks bones up by lower-cased name).
        std::string mRootBone = "bip01";
        // The root bone of skeletons that have none of the above (creatures). Empty: none.
        std::string mRootBoneFallback = "root bone";
        std::string mHeadBone = "bip01 head";
        std::string mSpine1Bone = "bip01 spine1";
        std::string mSpine2Bone = "bip01 spine2";

        // A model file whose lower-cased base name starts with one of these is an actor skeleton; the NIF loader must
        // not optimise it away because keyframes are added to it afterwards.
        std::vector<std::string> mSkeletonModelPrefixes = { "base_anim", "skin" };

        // Appended to a body part's record id for its first-person version.
        std::string mFirstPersonBodyPartSuffix = ".1st";

        // The first-person humanoid animation source, as the animation context names it.
        std::string mFirstPersonBaseAnimationFile = "xbase_anim.1st.nif";
    };

    namespace Detail
    {
        struct GameConventionsStore
        {
            std::mutex mMutex;
            // Snapshots live until the process ends so that a reference taken from gameConventions() never dangles.
            std::deque<std::unique_ptr<const GameConventions>> mSnapshots;
            std::atomic<const GameConventions*> mCurrent{ nullptr };

            GameConventionsStore()
            {
                mSnapshots.push_back(std::make_unique<const GameConventions>());
                mCurrent.store(mSnapshots.back().get(), std::memory_order_release);
            }
        };

        inline GameConventionsStore& gameConventionsStore()
        {
            static GameConventionsStore store;
            return store;
        }
    }

    /// The conventions of the session's game (Morrowind's when no pack set any). Lock free.
    inline const GameConventions& gameConventions()
    {
        return *Detail::gameConventionsStore().mCurrent.load(std::memory_order_acquire);
    }

    /// Replaces the conventions. Meant for the host at pack start; also used by tests.
    inline void setGameConventions(GameConventions conventions)
    {
        auto& store = Detail::gameConventionsStore();
        const std::lock_guard lock(store.mMutex);
        store.mSnapshots.push_back(std::make_unique<const GameConventions>(std::move(conventions)));
        store.mCurrent.store(store.mSnapshots.back().get(), std::memory_order_release);
    }

    /// Back to the defaults (Morrowind's).
    inline void resetGameConventions()
    {
        setGameConventions(GameConventions{});
    }

    /// True when a model file's lower-cased base name (no directory) names an actor skeleton.
    inline bool isSkeletonModelBaseName(std::string_view lowerCaseBaseName)
    {
        for (const std::string& prefix : gameConventions().mSkeletonModelPrefixes)
            if (!prefix.empty() && lowerCaseBaseName.substr(0, prefix.size()) == prefix)
                return true;
        return false;
    }

    /// The record id of a body part's first-person version when `firstPerson`, else the id itself.
    inline std::string bodyPartIdFor(std::string_view id, bool firstPerson)
    {
        std::string result(id);
        if (firstPerson)
            result += gameConventions().mFirstPersonBodyPartSuffix;
        return result;
    }
}

#endif
