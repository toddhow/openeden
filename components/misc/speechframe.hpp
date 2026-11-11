#ifndef OPENEDEN_COMPONENTS_MISC_SPEECHFRAME_H
#define OPENEDEN_COMPONENTS_MISC_SPEECHFRAME_H

// OpenEden facial speech animation (task 0071): the per-frame result of sampling facial curves for a stock head, and how
// it becomes the time of the head's one animation. Here, in the base layer, because MWAnim::Face (engine layer) and the
// face library (formats layer, `Face::SpeechFrame` is an alias of this) both use it and the engine layer may include
// only the base layer (CI/layer_rules.txt).

#include <algorithm>

namespace Misc
{
    /// A blink of at least this much takes the head's single animation value over from talk (see speechAnimTime).
    inline constexpr float sBlinkOverridesTalk = 0.1f;

    struct SpeechFrame
    {
        bool mActive = false; // curves drive this frame
        float mTalk = 0.f; // 0..1
        float mBlink = 0.f; // 0..1
    };

    /// The time to give the head's one animation, from a speech frame and the head's tag times (talk: start/stop,
    /// blink: start/stop). Blink >= sBlinkOverridesTalk wins and maps 0..1 to the first half of the blink range
    /// (open to closed; the head's blink is assumed to close in the middle).
    inline float speechAnimTime(
        const SpeechFrame& frame, float talkStart, float talkStop, float blinkStart, float blinkStop)
    {
        const float blink = std::clamp(frame.mBlink, 0.f, 1.f);
        if (blink >= sBlinkOverridesTalk)
            return blinkStart + (blinkStop - blinkStart) * 0.5f * blink;
        return talkStart + (talkStop - talkStart) * std::clamp(frame.mTalk, 0.f, 1.f);
    }
}

#endif
