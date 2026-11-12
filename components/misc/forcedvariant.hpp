#ifndef OPENEDEN_MISC_FORCEDVARIANT_H
#define OPENEDEN_MISC_FORCEDVARIANT_H

#include <cstdlib>

namespace Misc
{
    /// The value of OPENEDEN_FORCE_ANIM_VARIANT: a whole number from 1 to 99, else 0 (no forcing). Null, empty, text,
    /// 0, negative and 100 or more are 0.
    inline int parseForcedAnimVariant(const char* text)
    {
        if (text == nullptr)
            return 0;
        char* end = nullptr;
        const long parsed = std::strtol(text, &end, 10);
        if (end == text || *end != '\0')
            return 0;
        return parsed >= 1 && parsed <= 99 ? static_cast<int>(parsed) : 0;
    }

    /// Developer-only knob (task 0124, stage C2; not a setting): the environment variable
    /// OPENEDEN_FORCE_ANIM_VARIANT=<n> with n >= 1 makes the random animation variant choices (hit, attack and death
    /// group numbers, the wander idle number) take variant n instead of a random one, so that traces of two builds can
    /// be compared exactly. Unset, empty, 0 or invalid: 0 (no forcing; the random number generator is used as before).
    /// Read once.
    inline int forcedAnimVariant()
    {
        static const int value = parseForcedAnimVariant(std::getenv("OPENEDEN_FORCE_ANIM_VARIANT"));
        return value;
    }
}

#endif
