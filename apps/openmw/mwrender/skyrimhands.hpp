#ifndef OPENMW_MWRENDER_SKYRIMHANDS_HPP
#define OPENMW_MWRENDER_SKYRIMHANDS_HPP

// Task 0249 (the player's first-person hands, third slice): the pure rules of what the hands do with a bow, a held
// spell and a weapon that appears at its equip clip's own event. Nothing here reads a file or touches the renderer.
//
//   * handsIdleClip: the loop the hands play: the weapon's idle (a bow's, drawn back or not), a spell hand's idle or the
//     hold loop the pack named while a spell is held.
//   * weaponShown: whether the weapon model is in the hand during the equip or unequip clip: the game switches it at the
//     clip's `weaponDraw` / `weaponSheathe` event (animationdata/firstperson.txt: 1HM_Equip weaponDraw 0.5667,
//     1HM_Unequip weaponSheathe 1.0, Bow_Equip weaponDraw 0.5333, Bow_Unequip weaponSheathe 0.6667, H2H_Equip
//     weaponDraw 0), not at the start or the end of the clip.

#include <memory>
#include <string_view>

#include <components/havokpack/clipevents.hpp>

#include "skyrimfirstperson.hpp"

namespace MWRender::SkyrimFirstPerson
{
    inline constexpr std::string_view kDrawEvent = "weaponDraw";
    inline constexpr std::string_view kSheatheEvent = "weaponSheathe";
    /// The weapon animation type of a bow (WEAP DNAM +0 as heldWeaponType).
    inline constexpr std::uint8_t kBowType = 7;

    /// What the equip and unequip clips are doing to the weapon.
    enum class GearPhase : std::uint8_t
    {
        None, ///< nothing: the stance decides
        Equipping, ///< the equip clip was asked for (it may still be decoding) or is playing
        Unequipping, ///< the unequip clip was asked for or is playing
    };

    /// The event of a phase the weapon switches at (kDrawEvent, kSheatheEvent); empty for None.
    [[nodiscard]] inline std::string_view gearEventName(GearPhase phase)
    {
        switch (phase)
        {
            case GearPhase::Equipping:
                return kDrawEvent;
            case GearPhase::Unequipping:
                return kSheatheEvent;
            case GearPhase::None:
                break;
        }
        return {};
    }

    /// When `event` falls in a clip whose meta is `meta` (null with `resolved`: the sidecar or the clip failed, so the
    /// clip has no such event; not `resolved`: still being read). Names compare without regard to case; the first of
    /// that name counts; the time is the stored playback time times the clip's playback speed (animation seconds).
    [[nodiscard]] inline EventTime eventTimeIn(
        const std::shared_ptr<const HavokPack::ClipMeta>& meta, bool resolved, std::string_view event)
    {
        EventTime out;
        if (!resolved)
            return out;
        out.state = EventTime::State::Missing;
        if (!meta)
            return out;
        const auto same = [](std::string_view a, std::string_view b) {
            if (a.size() != b.size())
                return false;
            for (std::size_t i = 0; i < a.size(); ++i)
            {
                const auto lower = [](char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + 32) : c; };
                if (lower(a[i]) != lower(b[i]))
                    return false;
            }
            return true;
        };
        for (const HavokPack::ClipMetaEvent& e : meta->mEvents)
            if (same(e.mName, event))
            {
                out.state = EventTime::State::At;
                out.seconds = meta->animationTime(e.mTime);
                return out;
            }
        return out;
    }

    /// The loop the hands play. `castHoldClip` is the hold loop the pack named (PlayerBodyState::castHoldClip): while
    /// the spell hand is up it wins over the spell hand's idle. `bowDrawing`: the bow's string is held back.
    [[nodiscard]] inline std::string_view handsIdleClip(
        std::uint8_t weaponType, Stance stance, bool bowDrawing, std::string_view castHoldClip)
    {
        if (!castHoldClip.empty())
            return castHoldClip;
        if (weaponType == kBowType && stance == Stance::Weapon)
            return bowDrawing ? "bow_idledrawn" : "bow_idleheld";
        return idleClipFor(weaponType, stance);
    }

    /// Whether the weapon model is in the hand. `clipPlaying`: the equip or unequip clip has started; `elapsed` is the
    /// seconds into it; `event` when its switching event falls. Equipping: hidden until the event (and while the clip has
    /// not started or the event is not known yet), shown from it, shown from the start when the clip has no such event.
    /// Unequipping: shown until the event, hidden after it (shown to the end when there is none, and while the clip has
    /// not started). None: the stance's.
    [[nodiscard]] inline bool weaponShown(
        Stance stance, GearPhase phase, bool clipPlaying, float elapsed, const EventTime& event)
    {
        switch (phase)
        {
            case GearPhase::None:
                return stance == Stance::Weapon;
            case GearPhase::Equipping:
                if (!clipPlaying)
                    return false;
                if (event.state == EventTime::State::Unknown)
                    return false;
                return event.state == EventTime::State::Missing || elapsed >= event.seconds;
            case GearPhase::Unequipping:
                if (!clipPlaying || event.state != EventTime::State::At)
                    return true;
                return elapsed < event.seconds;
        }
        return false;
    }
}

#endif
