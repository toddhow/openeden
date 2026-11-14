#ifndef OPENMW_MWWORLD_NEUTRALWEATHERSOUND_H
#define OPENMW_MWWORLD_NEUTRALWEATHERSOUND_H

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// The sounds of a game pack's weather (OpenEden task 0127, stage W-S4): plain data and the pure rules that decide what
// plays and when. The sound manager is not touched here (weather.cpp does that); every function takes its inputs as
// arguments and reads no clock, so the tests run without an engine.
//
// What the data says (Skyrim SE, the five masters, VERIFIED by tools in the task log): a WTHR names up to four kinds of
// sound (SNAM: 0 the default loop, 1 precipitation, 2 wind, 3 thunder); each is a sound descriptor with one or several
// file names (alternates). Loops are marked in the descriptor; the wind of a storm is a set of one-shot gusts.
// Everything below that the data does not settle is a named constant with a comment.

namespace MWWorld::NeutralWeatherSound
{
    /// One of a weather's four sounds. `files` are VFS paths in the engine's normalised form ("sound/fx/..."); empty
    /// means the weather has none of that kind. `volume` is 0 to 1 (the descriptor's attenuation turned into a gain by
    /// the pack).
    struct Slot
    {
        std::string name; ///< the descriptor's editor id (logs only)
        std::vector<std::string> files;
        float volume = 1.f;
        bool loop = false;
        [[nodiscard]] bool empty() const { return files.empty(); }
        friend bool operator==(const Slot&, const Slot&) = default;
    };

    struct Set
    {
        Slot ambient; ///< SNAM type 0
        Slot precipitation; ///< type 1
        Slot wind; ///< type 2
        Slot thunder; ///< type 3
        [[nodiscard]] bool any() const
        {
            return !ambient.empty() || !precipitation.empty() || !wind.empty() || !thunder.empty();
        }
        friend bool operator==(const Set&, const Set&) = default;
    };

    // ---- thunder ----------------------------------------------------------------------------------------------------
    // The engine's strike rolls a distance index 0 (closest) to 3 (farthest) for its flash (weather.cpp,
    // Weather::lightningAndThunder). Thunder is heard after the flash: sound travels about 343 m/s, and one index step
    // is taken to be about 550 m (a GUESS, there is nothing to measure it against), so the delay is the time of the
    // nearest strike (about 140 m) plus 1.6 s per step. A farther strike is also quieter by a fifth of the volume a
    // step.
    inline constexpr float kThunderBaseDelaySeconds = 0.4f;
    inline constexpr float kThunderDelayPerStepSeconds = 1.6f;
    inline constexpr float kThunderVolumePerStepFalloff = 0.2f;
    inline constexpr unsigned kThunderMaxDistanceIndex = 3;
    /// Thunder waiting to be heard (a storm can strike several times inside the longest delay).
    inline constexpr std::size_t kMaxPendingThunder = 8;

    [[nodiscard]] float thunderDelaySeconds(unsigned distanceIndex);
    [[nodiscard]] float thunderVolume(float baseVolume, unsigned distanceIndex);

    /// The file of the `counter`-th strike or gust: the files in order, round and round (no random numbers: the weather
    /// code's own draws stay as they are). Empty string when the slot has none.
    [[nodiscard]] const std::string& pickFile(const Slot& slot, std::uint32_t counter);

    /// Thunder that has been struck and is not heard yet. Fixed size, no allocation.
    class DelayedThunder
    {
    public:
        struct Due
        {
            std::uint32_t fileIndex = 0; ///< the strike counter at the strike (pickFile's argument)
            float volume = 0.f;
            unsigned distanceIndex = 0;
        };

        /// False when the queue is full (the thunder is dropped).
        bool push(float delaySeconds, std::uint32_t fileIndex, float volume, unsigned distanceIndex);

        /// Moves time on and copies what became due into `out` (at most `capacity`), earliest due time first. Returns
        /// the count.
        std::size_t advance(float seconds, Due* out, std::size_t capacity);

        void clear() { mCount = 0; }
        [[nodiscard]] std::size_t size() const { return mCount; }

    private:
        struct Entry
        {
            float remaining = 0.f;
            Due due;
        };
        std::array<Entry, kMaxPendingThunder> mEntries{};
        std::size_t mCount = 0;
    };

    // ---- wind gusts -------------------------------------------------------------------------------------------------
    // A weather whose wind sound is not a loop (Skyrim's storm winds are one-shot gusts) plays one now and then: the
    // stronger the wind the shorter the pause. GUESS constants, pinned by tests.
    inline constexpr float kGustSecondsAtNoWind = 30.f;
    inline constexpr float kGustSecondsAtFullWind = 6.f;

    /// Seconds between gusts for the weather's wind (0 to 1, clamped; anything not a number counts as 0).
    [[nodiscard]] float gustIntervalSeconds(float wind);

    class GustTimer
    {
    public:
        /// Advances by `seconds`; true when a gust is due (the timer then starts the next pause). The first gust comes
        /// one interval after `reset`.
        bool advance(float seconds, float wind);
        void reset()
        {
            mElapsed = 0.f;
            mCount = 0;
        }
        [[nodiscard]] std::uint32_t count() const { return mCount; }

    private:
        float mElapsed = 0.f;
        std::uint32_t mCount = 0;
    };

    // ---- loops ------------------------------------------------------------------------------------------------------

    /// What one loop should be doing this frame: the file ("" for silence) and its volume.
    struct LoopWish
    {
        std::string file;
        float volume = 0.f;
        friend bool operator==(const LoopWish&, const LoopWish&) = default;
    };

    /// The loop of a slot: its first file at `slot.volume * blendVolume` (the manager's cross-fade volume, 0 to 1), or
    /// silence when the slot is empty, is not a loop, or the volume is not positive.
    [[nodiscard]] LoopWish loopWish(const Slot& slot, float blendVolume);
}

#endif
