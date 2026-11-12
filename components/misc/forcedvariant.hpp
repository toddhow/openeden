#ifndef OPENEDEN_MISC_FORCEDVARIANT_H
#define OPENEDEN_MISC_FORCEDVARIANT_H

#include <cstdlib>

namespace Misc
{
    /// Developer-only knob (task 0124, stage C2; not a setting): the environment variable
    /// OPENEDEN_FORCE_ANIM_VARIANT=<n> with n >= 1 makes the random animation variant choices (hit, attack and death
    /// group numbers, the wander idle number) take variant n instead of a random one, so that traces of two builds can
    /// be compared exactly. Unset, empty, 0 or invalid: 0 (no forcing; the random number generator is used as before).
    /// Read once.
    inline int forcedAnimVariant()
    {
        static const int value = []() {
            const char* text = std::getenv("OPENEDEN_FORCE_ANIM_VARIANT");
            if (text == nullptr)
                return 0;
            const int parsed = std::atoi(text);
            return parsed > 0 && parsed < 100 ? parsed : 0;
        }();
        return value;
    }
}

#endif
