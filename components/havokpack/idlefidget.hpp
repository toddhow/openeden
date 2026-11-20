#ifndef OPENEDEN_COMPONENTS_HAVOKPACK_IDLEFIDGET_H
#define OPENEDEN_COMPONENTS_HAVOKPACK_IDLEFIDGET_H

// Task 0361 (showcase audit, animation 17): Skyrim's standing idle (mt_idle) is a very slow sway, so a row of guards
// that all loop it look like a stack of statues, whatever their phase. The game breaks this up with one-shot idle
// variants (mt_idle_a_*: a leg shift, a shoulder roll, a sigh, a faster sway, ...) that its behaviour graph plays now
// and then. This is that choice and its timing, pure and deterministic: which variant an actor plays and when come
// from its reference alone (no clock, no random numbers), and the variant is laid over the idle with a short fade in
// and out. Standard library only; callers may use it from any thread.

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace HavokPack::Fidget
{
    // The one-shot variants an actor may play (animations/<male|female>/<name>.hkx, from the listing of
    // Skyrim - Animations.bsa). The scratches and the pants pull are left out: they read as a gag in a crowd, and so
    // is mt_idlelookinga, which lasts 16.7 s (longer than the shortest period). Measured on the user's files
    // (HavokIdleFidgetRealData): the idle's own loop moves a bone at most 9.6 degrees, the variants 9 to 26.
    inline constexpr std::array<std::string_view, 9> kVariants = { "mt_idle_a_sway_fast", "mt_idle_a_leg_shift",
        "mt_idle_a_shoulders", "mt_idle_a_soft_right", "mt_idle_a_left_long", "mt_idle_a_sigh_var1",
        "mt_idle_a_sigh_var2", "mt_idle_a_shakeout", "mt_idlelookingb" };

    struct Schedule
    {
        float mFirst = 0.f; // seconds from the actor's first frame to its first variant
        float mPeriod = 0.f; // seconds from the start of one variant to the start of the next
    };

    /// The schedule of an actor: the first variant 2 to 10 seconds in, then one every 12 to 24 seconds (so an actor
    /// is never mid-variant for more than a third of the time of the longest clips).
    Schedule scheduleFor(std::uint64_t id);

    /// The index in kVariants this actor plays (always the same one: a person has a habit).
    std::size_t variantFor(std::uint64_t id);

    struct Play
    {
        float mWeight = 0.f; // 0 (the plain idle) to 1 (the variant alone)
        float mClipTime = 0.f; // seconds into the variant's clip
    };

    /// What the variant contributes `time` seconds after the actor's first frame, for a clip `duration` seconds long:
    /// nothing before the first start and between variants, otherwise a weight that rises over `ramp` seconds, holds
    /// and falls over `ramp` seconds before the clip ends (a clip shorter than two ramps peaks lower). A clip that
    /// would not fit in the period (plus a 3 s gap) stretches the period: variants never run into each other.
    Play at(const Schedule& schedule, float time, float duration, float ramp = 0.35f);
}

#endif
