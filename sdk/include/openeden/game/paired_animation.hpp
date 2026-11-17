#pragma once

#include <cstdint>
#include <string_view>

#include <openeden/game/actor_rules.hpp>
#include <openeden/version.hpp>

// Paired animations (task 0267): one clip file that animates two actors at once (a rider and his horse, a killer and
// his victim). The clip belongs to neither actor's own animation folder, so a game pack names it by a CLIP NAME the
// core understands:
//
//     "~1/<path under meshes/, no extension>"   the tracks of the FIRST actor (the one that started the clip)
//     "~2/<path under meshes/, no extension>"   the tracks of the SECOND actor (the horse, the victim)
//
// for example "~1/actors/sharedkillmoves/human&horse/paired_mount". The clip's own root is the frame both actors stand
// in when it starts: place each actor's object at the SECOND actor's position and heading and let each play its side.
// Poses below are in that frame: x to the second actor's right, y forward, z up, yaw counter-clockwise seen from above,
// radians (the engine's headings are clockwise, 0 faces +Y: heading = secondHeading - yaw).
//
// Two small interfaces of the core's writer, found by dynamic_cast on the ActorRequestWriter (or RidingWriter) the pack
// gets, so no frozen interface changes and a core that has no paired animations is never asked. The first actor, when
// it is the player, plays his side through PlayerBodyState::ridingClip (a clip name that starts with '~' plays once
// and holds its last pose); any other actor through playPairedClip.
//
// Version policy: DRAFT 0.1. Additive changes (a new trailing virtual with a "not supported" default, a trailing
// field) raise the minor number. Threading: main thread only; valid during ActorRules::update() (and, for the riding
// dismount, during RidingRules::dismountNow()).

namespace openeden::sdk::game
{
    inline constexpr Version kPairedAnimationVersion{ 0, 1 };

    struct PairedPose
    {
        double x = 0.0;
        double y = 0.0;
        double z = 0.0;
        double yaw = 0.0;
    };

    /// What a game needs of a paired clip: how long it is, and where its first actor's root is at the start and at the
    /// end, in the shared frame.
    struct PairedClipInfo
    {
        float duration = 0.f; ///< seconds
        PairedPose start;
        PairedPose end;
    };

    enum class PairedStatus : std::uint8_t
    {
        NotReady = 0, ///< the clip is being read; ask again next frame
        Ready = 1,
        Failed = 2, ///< the clip cannot be read or is not a paired clip; it will not become ready
    };

    class PairedWriter
    {
    public:
        virtual ~PairedWriter() = default;

        /// The first actor's start and end poses and the duration of a clip name ("~1/..." or "~2/..."). Starts reading
        /// the clip when nobody has asked for it yet. Never blocks.
        virtual PairedStatus pairedClipInfo(std::string_view clipName, PairedClipInfo& out)
        {
            (void)clipName;
            out = {};
            return PairedStatus::Failed;
        }

        /// Plays a side of a paired clip once on an actor and holds its last pose until the clip is cleared; an empty
        /// name clears it (the actor goes back to its own animation). False for a handle that is not a drawn actor.
        virtual bool playPairedClip(ActorHandle actor, std::string_view clipName)
        {
            (void)actor;
            (void)clipName;
            return false;
        }
    };
}
