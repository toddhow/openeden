#ifndef OPENEDEN_COMPONENTS_TRIMORPH_MOUTH_H
#define OPENEDEN_COMPONENTS_TRIMORPH_MOUTH_H

// Task 0146 (slice 2): a mouth that follows the loudness of the voice. No .lip data is used (its format is not decoded,
// docs/tasks/0146-skyrim-lip-sync.md): the loudness of the line at its playback position opens the jaw. Pure, standard
// library only. The numbers are named starting values (guesses, judged by frames), not measured facts.

#include <cstdint>

namespace TriMorph
{
    /// Weights of the two morphs the drive moves, each in 0..1, in steps of 1/kSteps so that a still mouth uploads nothing.
    struct MouthWeights
    {
        float mAah = 0.f; ///< "Aah": a half open jaw
        float mBigAah = 0.f; ///< "BigAah": the jaw wide open, only for loud passages

        friend bool operator==(const MouthWeights&, const MouthWeights&) = default;
    };

    inline constexpr int kMouthSteps = 16; ///< weights are multiples of 1/16
    inline constexpr float kLoudnessGain = 4.f; ///< loudness (0..1, as SoundManager::getSaySoundLoudness) to openness
    inline constexpr float kAttack = 0.6f; ///< share of the way to a louder target taken per step (fast: the jaw drops)
    inline constexpr float kRelease = 0.3f; ///< share taken towards a quieter target (slower: the jaw closes)
    inline constexpr float kBigAahFrom = 0.5f; ///< openness above which BigAah starts
    inline constexpr float kAahMax = 0.9f; ///< Aah never exceeds this

    class MouthDrive
    {
    public:
        /// One step per frame. A non-finite or negative loudness counts as silence. Deterministic; no allocation.
        MouthWeights step(float loudness);

        /// Back to a closed mouth.
        void reset() { mOpen = 0.f; }

        [[nodiscard]] float openness() const { return mOpen; }

    private:
        float mOpen = 0.f;
    };

    /// `weight` clamped to 0..1 and rounded to a multiple of 1/kMouthSteps; NaN gives 0.
    [[nodiscard]] float quantise(float weight);
}

#endif
